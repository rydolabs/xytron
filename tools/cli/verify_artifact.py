#!/usr/bin/env python3
"""Independent, read-only validation of experimental Xytron .xplugin packages.

Validates archive framing, paths, CRC and v1 manifest/entry compatibility.
This is NOT a digital signature, safe-to-execute verdict, dependency resolver,
or proof that a language host can load the plugin.
"""
import argparse
import json
import re
from pathlib import Path, PurePosixPath
import stat
import sys
import zipfile

NAME = re.compile(r"[a-z][a-z0-9_]*\Z")
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+\Z")
MAX_FILES = 1024
MAX_TOTAL = 64 * 1024 * 1024
MAX_FILE = 32 * 1024 * 1024


def unique_dict(pairs):
    output = {}
    for key, val in pairs:
        if key in output:
            raise ValueError(f"Duplicate JSON key: {key}")
        output[key] = val
    return output


def verify_archive(path):
    path = Path(path)
    with zipfile.ZipFile(path, 'r') as zf:
        infos = zf.infolist()
        if not infos or len(infos) > MAX_FILES:
            raise ValueError('Invalid number of archive entries')
        entries = set()
        folded = set()
        total = 0
        for item in infos:
            name = item.filename
            p = PurePosixPath(name)
            if (not name or '\\' in name or '\x00' in name or ':' in name or
                    name.startswith('/') or name.endswith('/') or
                    any(part in ('', '.', '..') for part in name.split('/')) or
                    str(p) != name):
                raise ValueError(f'Unsafe path: {name!r}')
            if name in entries or name.casefold() in folded:
                raise ValueError(f'Duplicate or case-colliding entry: {name}')
            if item.flag_bits & 1 or item.compress_type != zipfile.ZIP_STORED:
                raise ValueError(f'Unsupported archive encoding: {name}')
            if item.create_system == 3 and stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError(f'Symlink entry forbidden: {name}')
            if item.file_size > MAX_FILE or item.file_size < 0:
                raise ValueError(f'Oversized archive entry: {name}')
            total += item.file_size
            if total > MAX_TOTAL:
                raise ValueError('Oversized archive')
            entries.add(name)
            folded.add(name.casefold())
        if 'plugin.json' not in entries:
            raise ValueError('Missing plugin.json')
        if zf.getinfo('plugin.json').file_size > 65536:
            raise ValueError('Oversized plugin manifest')
        # Read all entries and check CRC. Do not extract untrusted content.
        for item in infos:
            with zf.open(item, 'r') as stream:
                while stream.read(65536):
                    pass
        doc = json.loads(zf.read('plugin.json'), object_pairs_hook=unique_dict)
        if not isinstance(doc, dict) or type(doc.get('schemaVersion')) is not int or doc['schemaVersion'] != 1:
            raise ValueError('Unsupported manifest schemaVersion')
        name, version, runtime, entry = (doc.get(k) for k in ('name', 'version', 'runtime', 'entry'))
        if not isinstance(name, str) or not NAME.fullmatch(name):
            raise ValueError('Invalid plugin name')
        if not isinstance(version, str) or not VERSION.fullmatch(version):
            raise ValueError('Invalid plugin version')
        if runtime == 'native':
            if not isinstance(entry, dict):
                raise ValueError('Native entry must be a platform mapping')
            required = {'win-x64': f'bin/win-x64/{name}.dll',
                        'linux-x64': f'bin/linux-x64/lib{name}.so'}
            if entry != required:
                raise ValueError('Unexpected native binary entry paths')
            active = [rid for rid, p in required.items() if p in entries]
            if len(active) != 1:
                raise ValueError('Expected exactly one target native binary')
            rid = active[0]
        elif runtime == 'dotnet':
            if entry != f'bin/{name}.dll' or entry not in entries:
                raise ValueError('Missing .NET entry assembly')
            rid = 'any'
        elif runtime == 'python':
            if entry != f'{name}.plugin' or f'python/{name}/plugin.py' not in entries:
                raise ValueError('Missing Python import entry')
            if f'python/{name}/__init__.py' not in entries:
                raise ValueError('Missing Python package initializer')
            rid = 'any'
        else:
            raise ValueError(f'Unsupported runtime: {runtime!r}')
        expected_name = f'{name}-{version}-{rid}.xplugin'
        if path.name != expected_name:
            raise ValueError(f'Package name must be {expected_name}')
        return {'name': name, 'version': version, 'runtime': runtime,
                'platform': rid, 'files': len(infos)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    try:
        print('PASS:', json.dumps(verify_archive(args.archive), sort_keys=True))
    except (ValueError, OSError, zipfile.BadZipFile, RuntimeError) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
