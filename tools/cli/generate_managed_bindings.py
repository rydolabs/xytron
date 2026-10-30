#!/usr/bin/env python3
"""Generate Xytron metadata and JS/TS bindings from Python source (AST only).

This never imports or executes the plugin. Class wrapper operations describe a
future managed-host bridge protocol; they are not yet implemented in Core.
"""
import argparse
import ast
import json
from pathlib import Path
import re
import sys

IDENT = re.compile(r'[A-Za-z][A-Za-z0-9_]*\Z')
RESERVED = {'break','case','catch','class','const','continue','debugger','default',
            'delete','do','else','enum','export','extends','false','finally',
            'for','function','if','import','in','instanceof','new','null','return',
            'super','switch','this','throw','true','try','typeof','var','void',
            'while','with','yield','await','let','static','constructor','then',
            '__proto__','prototype'}
TYPES = {'str':'string', 'int':'number', 'float':'number', 'bool':'boolean',
         'bytes':'Uint8Array', 'None':'void'}


def fail(message):
    raise ValueError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail(f'duplicate manifest key: {key}')
        result[key] = value
    return result


def identifier(s):
    if not isinstance(s, str) or not IDENT.fullmatch(s) or s in RESERVED:
        fail(f'unsupported JS export identifier: {s!r}')
    return s


def annotation(node):
    if isinstance(node, ast.Name) and node.id in TYPES:
        return TYPES[node.id]
    if isinstance(node, ast.Constant) and node.value is None:
        return 'void'
    fail('all exported Python parameters and returns need supported annotations: '
         'str, int, float, bool, bytes, None')


def decorated(node, name):
    return any(isinstance(d, ast.Name) and d.id == name or
               isinstance(d, ast.Attribute) and d.attr == name and
               isinstance(d.value, ast.Name) and d.value.id == 'xytron'
               for d in node.decorator_list)


def has_decorator(node, name):
    return any(isinstance(d, ast.Name) and d.id == name
               for d in node.decorator_list)


def signature(node, skip_self=False, constructor=False):
    a = node.args
    if a.posonlyargs or a.vararg or a.kwarg or a.kwonlyargs or a.defaults or a.kw_defaults:
        fail(f'unsupported argument convention/default: {node.name}')
    args = a.args
    if skip_self:
        if not args or args[0].arg != 'self' or args[0].annotation is not None:
            fail(f'instance method must take unannotated self: {node.name}')
        args = args[1:]
    names = [identifier(p.arg) for p in args]
    types = [annotation(p.annotation) for p in args]
    if 'void' in types:
        fail('void is not a parameter type')
    if constructor:
        if node.returns is not None and annotation(node.returns) != 'void':
            fail('constructor must return None')
        return {'arguments':types, 'parameterNames':names}
    return {'arguments':types, 'parameterNames':names,
            'returns':annotation(node.returns)}


def exported_class(node):
    name = identifier(node.name)
    constructor = {'arguments':[], 'parameterNames':[]}
    methods = []
    properties = []
    setters = {}
    members = {m.name:m for m in node.body if isinstance(m, (ast.FunctionDef, ast.AsyncFunctionDef))}
    for m in members.values():
        for d in m.decorator_list:
            if isinstance(d, ast.Attribute) and d.attr == 'setter' and isinstance(d.value, ast.Name):
                setters[d.value.id] = m
    seen = set()
    for m in node.body:
        if isinstance(m, (ast.FunctionDef, ast.AsyncFunctionDef)) and m.name == '__init__':
            if not isinstance(m, ast.FunctionDef) or has_decorator(m, 'staticmethod') or has_decorator(m, 'classmethod'):
                fail('unsupported class constructor')
            constructor = signature(m, skip_self=True, constructor=True)
        elif isinstance(m, (ast.FunctionDef, ast.AsyncFunctionDef)) and decorated(m, 'export'):
            if not isinstance(m, ast.FunctionDef):
                fail(f'async exported class member unsupported: {name}.{m.name}')
            if has_decorator(m, 'staticmethod') or has_decorator(m, 'classmethod'):
                fail(f'static/class member unsupported: {name}.{m.name}')
            member_name = identifier(m.name)
            if member_name in seen or member_name in {'create','dispose'}:
                fail(f'duplicate or reserved class member: {member_name}')
            seen.add(member_name)
            if has_decorator(m, 'property'):
                getter = signature(m, skip_self=True)
                if getter['arguments'] or getter['returns'] == 'void':
                    fail(f'property getter signature invalid: {member_name}')
                setter = setters.get(member_name)
                writable = setter is not None
                if setter:
                    if not isinstance(setter, ast.FunctionDef):
                        fail(f'async property setter unsupported: {member_name}')
                    s = signature(setter, skip_self=True)
                    if s['arguments'] != [getter['returns']] or s['returns'] != 'void':
                        fail(f'property setter type mismatch: {member_name}')
                properties.append({'name':member_name, 'type':getter['returns'],
                                   'readable':True, 'writable':writable})
            else:
                methods.append({'name':member_name, **signature(m, skip_self=True)})
    if not methods and not properties:
        fail(f'exported class requires explicit @export methods or properties: {name}')
    for prop in properties:
        cap = prop['name'][0].upper()+prop['name'][1:]
        for synthetic in ('get'+cap, 'set'+cap):
            if synthetic in seen or synthetic in {'create','dispose'}:
                fail(f'generated accessor clashes with exported method: {synthetic}')
    return {'name':name, 'constructor':constructor,
            'methods':sorted(methods,key=lambda x:x['name']),
            'properties':sorted(properties,key=lambda x:x['name'])}


def python_exports(project, plugin_id):
    src = project / 'src' / plugin_id
    if not src.is_dir() or src.is_symlink():
        fail('missing/unsafe Python source directory')
    funcs, classes = [], []
    for path in sorted(src.rglob('*.py')):
        if path.is_symlink() or not path.is_file():
            fail('symlinked Python source is not allowed')
        if '__pycache__' in path.parts:
            continue
        tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
        for node in tree.body:
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) or not decorated(node,'export'):
                continue
            if isinstance(node, ast.ClassDef):
                classes.append(exported_class(node))
            elif isinstance(node, ast.FunctionDef):
                funcs.append({'name':identifier(node.name), **signature(node)})
            else:
                fail(f'only synchronous exports supported: {path}:{node.lineno}')
    return funcs, classes


def validate_function(item, all_names):
    name = identifier(item['name'])
    if name in all_names:
        fail(f'overloaded or duplicate exports are unsupported: {name}')
    all_names.add(name)
    args, params, result = item['arguments'], item['parameterNames'], item['returns']
    if not isinstance(args,list) or not isinstance(params,list) or len(args)!=len(params):
        fail('invalid parameter metadata')
    for n,t in zip(params,args):
        identifier(n)
        if t not in TYPES.values() or t=='void':
            fail(f'unsupported parameter type: {t}')
    if result not in TYPES.values():
        fail(f'unsupported return type: {result}')


def signature_ts(item):
    return ', '.join(f'{n}: {t}' for n,t in zip(item['parameterNames'],item['arguments']))


def render(plugin_id, runtime, functions, classes=None):
    classes = classes or []
    if not functions and not classes:
        fail('no exported functions or classes found')
    names = set()
    dts = ['// Generated by Xytron; edit the source plugin, not this file.']
    mjs = ['// Generated by Xytron; requires the Xytron window bridge.',
           '// Class calls require a managed object host (not yet available).']
    for item in sorted(functions, key=lambda x:x['name']):
        validate_function(item,names)
        name=item['name']
        dts.append(f'export declare function {name}({signature_ts(item)}): Promise<{item["returns"]}>;')
        mjs.extend([f'export function {name}(...args) {{',
                    '  const bridge = globalThis.xytron;',
                    "  if (!bridge || typeof bridge.invoke !== 'function')",
                    "    throw new Error('Xytron bridge unavailable');",
                    f'  return bridge.invoke({json.dumps(plugin_id+"."+name)}, ...args);', '}'])
    for cls in sorted(classes,key=lambda c:c['name']):
        name = identifier(cls['name'])
        if name in names:
            fail(f'class name collides with another export: {name}')
        names.add(name)
        construct=cls['constructor']
        for ty in construct['arguments']:
            if ty not in TYPES.values() or ty == 'void':
                fail(f'unsupported constructor argument type: {ty}')
        dts.extend([f'export declare class {name} {{', '  private constructor();',
                    f'  static create({signature_ts(construct)}): Promise<{name}>;'])
        mjs.extend([f'export class {name} {{', '  #handle;',
                    '  constructor(handle) { this.#handle = handle; }',
                    '  static async create(...args) {',
                    "    const bridge = globalThis.xytron;",
                    "    if (!bridge || typeof bridge.invoke !== 'function') throw new Error('Xytron bridge unavailable');",
                    f'    const handle = await bridge.invoke({json.dumps(plugin_id+"."+name+".create")}, ...args);',
                    "    if (typeof handle !== 'string' || !handle) throw new Error('Invalid managed object handle');",
                    f'    return new {name}(handle);', '  }',
                    '  #active() {',
                    "    if (this.#handle === null) throw new Error('Xytron object already disposed');",
                    "    const bridge = globalThis.xytron;",
                    "    if (!bridge || typeof bridge.invoke !== 'function') throw new Error('Xytron bridge unavailable');",
                    '    return bridge;', '  }'])
        members=set()
        for method in cls['methods']:
            validate_function(method,members)
            method_name=method['name']
            if method_name in {'create','dispose'}:
                fail('class method conflicts with lifecycle API')
            dts.append(f'  {method_name}({signature_ts(method)}): Promise<{method["returns"]}>;')
            mjs.extend([f'  {method_name}(...args) {{',
                        '    const bridge = this.#active();',
                        f'    return bridge.invoke({json.dumps(plugin_id+"."+name+".method."+method_name)}, this.#handle, ...args);', '  }'])
        for prop in cls['properties']:
            identifier(prop['name'])
            if prop['type'] not in TYPES.values() or prop['type']=='void':
                fail('invalid class property type')
            cap=prop['name'][0].upper()+prop['name'][1:]
            for action in ('get','set'):
                if action=='set' and not prop['writable']:
                    continue
                fname=action+cap
                if fname in members or fname in {'create','dispose'}:
                    fail('class accessor conflicts with method name')
                members.add(fname)
                params='' if action=='get' else f'value: {prop["type"]}'
                result=prop['type'] if action=='get' else 'void'
                dts.append(f'  {fname}({params}): Promise<{result}>;')
                mjs.extend([f'  {fname}({"" if action=="get" else "value"}) {{',
                            '    const bridge = this.#active();',
                            f'    return bridge.invoke({json.dumps(plugin_id+"."+name+"."+action+"."+prop["name"])}, this.#handle{", value" if action=="set" else ""});', '  }'])
        dts.extend(['  dispose(): Promise<void>;', '}'])
        mjs.extend(['  async dispose() {',
                    '    if (this.#handle === null) return;',
                    '    const bridge = this.#active();',
                    f'    await bridge.invoke({json.dumps(plugin_id+"."+name+".dispose")}, this.#handle);',
                    '    this.#handle = null;', '  }', '}'])
    doc={'schemaVersion':1,'runtime':runtime,'pluginId':plugin_id,
         'functions':sorted(functions,key=lambda x:x['name'])}
    if classes:
        doc['classes']=sorted(classes,key=lambda x:x['name'])
    return (json.dumps(doc,indent=2,ensure_ascii=True)+'\n',
            '\n'.join(dts)+'\n','\n'.join(mjs)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,default=Path('.'))
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    project=args.project.resolve()
    if not project.is_dir() or not (project/'plugin.json').is_file():
        fail('plugin project / manifest missing')
    manifest=json.loads((project/'plugin.json').read_text(encoding='utf-8'),object_pairs_hook=unique)
    if not isinstance(manifest,dict) or type(manifest.get('schemaVersion')) is not int or manifest['schemaVersion']!=1:
        fail('invalid plugin manifest version')
    plugin_id,runtime=manifest.get('name'),manifest.get('runtime')
    if not isinstance(plugin_id,str) or not re.fullmatch(r'[a-z][a-z0-9_]*',plugin_id):
        fail('invalid plugin name')
    if runtime!='python':
        fail('Python AST generator only supports python runtime')
    funcs,classes=python_exports(project,plugin_id)
    meta,dts,mjs=render(plugin_id,runtime,funcs,classes)
    out=args.out
    if not out.is_absolute():
        out=project/out
    if out.exists() and out.is_symlink():
        fail('symlink output directory forbidden')
    out.mkdir(parents=True,exist_ok=True)
    targets=[out/'exports.json',out/'index.d.ts',out/'index.mjs']
    if any(p.exists() or p.is_symlink() for p in targets):
        fail('refusing to overwrite existing generated bindings')
    for p,content in zip(targets,(meta,dts,mjs)):
        p.write_text(content,encoding='utf-8',newline='\n')
    print(f'Generated {runtime} typed bindings for {len(funcs)} functions, {len(classes)} classes: {out}')


if __name__=='__main__':
    try:
        main()
    except (ValueError,OSError,SyntaxError,json.JSONDecodeError) as err:
        print('xytron bindgen: '+str(err),file=sys.stderr)
        sys.exit(1)
