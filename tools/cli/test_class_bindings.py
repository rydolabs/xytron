#!/usr/bin/env python3
"""Class descriptor and JS mock-host contract regression.

Mock-host testing does NOT prove that an actual Python/.NET object runtime exists.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def invoke(*args, cwd=None, ok=True):
    run = subprocess.run([str(a) for a in args], cwd=cwd, capture_output=True, text=True)
    if (run.returncode == 0) != ok:
        raise AssertionError(f'{args!r}: expected success {ok}, got {run.returncode}\n{run.stdout}\n{run.stderr}')
    return run


def check(project, runtime, klass, getter, setter):
    out=project/'bindings'
    meta=json.loads((out/'exports.json').read_text(encoding='utf-8'))
    assert meta['runtime']==runtime and meta['schemaVersion']==1
    assert len(meta['classes'])==1
    c=meta['classes'][0]
    assert c['name']==klass
    assert c['constructor']=={'arguments':['string'], 'parameterNames':['name']}
    assert len(c['methods'])==1
    assert c['methods'][0]['arguments']==['number']
    assert 'secret' not in json.dumps(c)
    assert len(c['properties'])==1 and c['properties'][0]['writable']
    ts=(out/'index.d.ts').read_text()
    js=(out/'index.mjs').read_text()
    assert f'static create(name: string): Promise<{klass}>' in ts
    assert f'get{getter}()' in ts and f'set{setter}(' in ts
    assert '#handle;' in js and 'already disposed' in js
    return c


def mock_browser(out, klass, method, getter, setter, plugin):
    if not shutil.which('node'):
        print('SKIP Node mock (node unavailable)')
        return
    property_name = json.loads((out/'exports.json').read_text(encoding='utf-8'))['classes'][0]['properties'][0]['name']
    code=r'''
const assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url');
(async()=>{
  const [file, klass, method, getter, setter, plugin, propertyName] = process.argv.slice(1);
  const calls=[];
  globalThis.xytron={invoke:async (...args)=>{
    calls.push(args);
    if(args[0]===`${plugin}.${klass}.create`) return 'opaque-31';
    return args[0].includes('.method.')? 91: undefined;
  }};
  const mod=await import(pathToFileURL(file).href);
  const Type=mod[klass];
  const obj=await Type.create('name');
  assert.equal(await obj[method](9),91);
  await obj[getter](); await obj[setter](50);
  await obj.dispose(); await obj.dispose();
  assert.deepEqual(calls,[
    [`${plugin}.${klass}.create`,'name'],
    [`${plugin}.${klass}.method.${method}`,'opaque-31',9],
    [`${plugin}.${klass}.get.${propertyName}`,'opaque-31'],
    [`${plugin}.${klass}.set.${propertyName}`,'opaque-31',50],
    [`${plugin}.${klass}.dispose`,'opaque-31']]);
  assert.throws(()=>obj[method](1),/already disposed/);
  assert.equal(typeof obj.secret,'undefined');
})().catch(e=>{console.error(e);process.exit(1)});
'''
    invoke('node','-e',code,str(out/'index.mjs'),klass,method,getter,setter,plugin,property_name)


def run(xytron,require_dotnet):
    count=0
    with tempfile.TemporaryDirectory(prefix='xytron-class-') as temp:
        root=Path(temp)
        invoke(xytron,'new','plugin','example_py','--lang','python',cwd=root)
        py=root/'plugins/example_py'
        source=py/'src/example_py/plugin.py'
        source.write_text('''from xytron import export
raise RuntimeError("AST analyzer must not import this module")
@export
def hello(name: str) -> str:
    return name
@export
class Player:
    def __init__(self, name: str) -> None:
        self.name = name
    @export
    def attack(self, amount: int) -> int:
        return amount
    def secret(self) -> int:
        return 1
    @property
    @export
    def health(self) -> int:
        return 100
    @health.setter
    def health(self, value: int) -> None:
        pass
''',encoding='utf-8')
        invoke(xytron,'bindings','generate','--out','bindings',cwd=py)
        check(py,'python','Player','Health','Health'); count+=1
        mock_browser(py/'bindings','Player','attack','getHealth','setHealth','example_py');count+=1
        invoke(xytron,'pack',cwd=py)
        with zipfile.ZipFile(py/'dist/example_py-1.0.0-any.xplugin') as z:
            assert 'bindings/index.mjs' in z.namelist()
            assert '"classes"' in z.read('bindings/exports.json').decode()
        count+=1
        # Constructor defaults must fail closed, not silently produce invalid descriptors.
        source.write_text(source.read_text().replace('name: str) -> None','name: str = "") -> None'),encoding='utf-8')
        invoke(xytron,'bindings','generate','--out','bad_constructor',cwd=py,ok=False);count+=1
        source.write_text(source.read_text().replace('name: str = ""','name: str').replace('def health(self) -> int:', 'def health(self) -> str:'),encoding='utf-8')
        invoke(xytron,'bindings','generate','--out','bad_setter',cwd=py,ok=False);count+=1
        # Unexported class member does not appear in descriptor.
        source.write_text(source.read_text().replace('def health(self) -> str:', 'def health(self) -> int:'),encoding='utf-8')
        invoke(xytron,'bindings','generate','--out','fresh',cwd=py);count+=1

        invoke(xytron,'new','plugin','example_cs','--lang','csharp',cwd=root)
        cs=root/'plugins/example_cs'
        if shutil.which('dotnet'):
            (cs/'src/Player.cs').write_text('''using Xytron;
[Export]
public sealed class Player {
    [Export] public Player(string name) { Name=name; }
    public string Name { get; }
    [Export] public int Health {get;set;}=100;
    [Export] public int Attack(int amount) { Health-=amount; return Health; }
    public int Secret() => 17;
}
''',encoding='utf-8')
            invoke(xytron,'build',cwd=cs)
            invoke(xytron,'bindings','generate','--out','bindings','--trusted',cwd=cs)
            check(cs,'dotnet','Player','Health','Health');count+=1
            mock_browser(cs/'bindings','Player','Attack','getHealth','setHealth','example_cs');count+=1
            invoke(xytron,'pack',cwd=cs)
            with zipfile.ZipFile(cs/'dist/example_cs-1.0.0-any.xplugin') as z:
                assert 'bindings/exports.json' in z.namelist()
            count+=1
            # Explicit opt-in: class public method without Export must not be exposed.
            assert 'Secret' not in (cs/'bindings/exports.json').read_text();count+=1
        elif require_dotnet:
            raise AssertionError('.NET SDK missing on CI')
        else:
            print('SKIP C# class reflector: .NET SDK not installed')
    print(f'PASS: {count} class binding checks')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--xytron',type=Path,required=True)
    p.add_argument('--require-dotnet',action='store_true')
    a=p.parse_args()
    run(a.xytron.resolve(),a.require_dotnet)
