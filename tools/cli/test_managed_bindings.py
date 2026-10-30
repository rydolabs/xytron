#!/usr/bin/env python3
"""S2 managed function schema regression for scaffolded Python and C# plugins."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def call(exe, cwd, *args, ok=True):
    p = subprocess.run([str(exe), *args], cwd=str(cwd), capture_output=True, text=True)
    if (p.returncode == 0) != ok:
        raise AssertionError(f'{args} expected ok={ok}, got {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p


def check_bindings(project, runtime, name, expected):
    root = project / 'bindings'
    meta = json.loads((root / 'exports.json').read_text(encoding='utf-8'))
    assert meta['schemaVersion'] == 1 and meta['runtime'] == runtime and meta['pluginId'] == name
    assert len(meta['functions']) == 1 and meta['functions'][0] == expected
    assert f'function {expected["name"]}(' in (root / 'index.d.ts').read_text()
    assert f'bridge.invoke("{name}.{expected["name"]}", ...args)' in (root / 'index.mjs').read_text()


def run(exe, require_dotnet):
    checks = 0
    with tempfile.TemporaryDirectory(prefix='xytron-s2-managed-') as dirname:
        root = Path(dirname)
        call(exe, root, 'new', 'plugin', 'python_job', '--lang', 'python')
        project = root / 'plugins/python_job'
        source = project / 'src/python_job/plugin.py'
        original = source.read_text(encoding='utf-8')
        # Top-level poison makes accidental imports during metadata extraction fail.
        source.write_text("raise RuntimeError('metadata must never import source')\n" + original,
                          encoding='utf-8')
        call(exe, project, 'bindings', 'generate', '--out', 'bindings')
        check_bindings(project, 'python', 'python_job',
                       {'name':'hello','arguments':['string'], 'parameterNames':['name'], 'returns':'string'})
        checks += 1
        call(exe, project, 'bindings', 'generate', '--out', 'bindings', ok=False)
        checks += 1
        call(exe, project, 'pack')
        with zipfile.ZipFile(project / 'dist/python_job-1.0.0-any.xplugin') as z:
            assert set(['bindings/exports.json','bindings/index.d.ts','bindings/index.mjs']).issubset(z.namelist())
        checks += 1
        (project / 'bindings/exports.json').unlink()
        source.write_text(original.replace('name: str', 'name'), encoding='utf-8')
        call(exe, project, 'bindings', 'generate', '--out', 'other', ok=False)
        checks += 1
        source.write_text(original + '\n@export\ndef hello(x: str) -> str: return x\n', encoding='utf-8')
        call(exe, project, 'bindings', 'generate', '--out', 'duplicate', ok=False)
        checks += 1
        source.write_text('from xytron import export\n@export\nclass Player: pass\n', encoding='utf-8')
        call(exe, project, 'bindings', 'generate', '--out', 'classes', ok=False)
        checks += 1
        source.write_text(original.replace('name: str', 'name: list[str]'), encoding='utf-8')
        call(exe, project, 'bindings', 'generate', '--out', 'unsupported', ok=False)
        checks += 1
        source.write_text(original, encoding='utf-8')

        call(exe, root, 'new', 'plugin', 'reports', '--lang', 'csharp')
        cs = root / 'plugins/reports'
        call(exe, cs, 'bindings', 'generate', '--out', 'bindings', ok=False)
        checks += 1
        if shutil.which('dotnet'):
            call(exe, cs, 'build')
            call(exe, cs, 'bindings', 'generate', '--out', 'bindings', '--trusted')
            check_bindings(cs, 'dotnet', 'reports',
                           {'name':'Hello','arguments':['string'], 'parameterNames':['name'], 'returns':'string'})
            checks += 1
            call(exe, cs, 'pack')
            with zipfile.ZipFile(cs / 'dist/reports-1.0.0-any.xplugin') as z:
                assert 'bindings/index.mjs' in z.namelist()
            checks += 1
            src = cs / 'src/Plugin.cs'
            content = src.read_text(encoding='utf-8')
            src.write_text(content.replace('[Export]\n    public static string Hello',
                '[Export]\n    public static int Hello(int count) => count;\n    [Export]\n    public static string Hello'),
                encoding='utf-8')
            call(exe, cs, 'build')
            call(exe, cs, 'bindings', 'generate', '--out', 'duplicates', '--trusted', ok=False)
            checks += 1
        elif require_dotnet:
            raise AssertionError('dotnet SDK missing on CI runner')
        else:
            print('SKIP C# reflector: .NET 8 SDK not available')
    print(f'PASS: {checks} managed binding regression cases')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--xytron', type=Path, required=True)
    p.add_argument('--require-dotnet', action='store_true')
    args = p.parse_args()
    run(args.xytron.resolve(), args.require_dotnet)
