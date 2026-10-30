#!/usr/bin/env python3
"""Real child-process JSONL IPC regression, not a browser security/E2E test."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PYDIR = ROOT / 'bindings/python'
FIXTURE_DIR = ROOT / 'tests/ipc'
MAX_FRAME = 65536


class Child:
    def __init__(self, argv, env=None):
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, env=env)
        self.next_id = 1

    def raw(self, frame):
        self.proc.stdin.write(frame + b'\n')
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise AssertionError('Child unexpectedly exited; no protocol response')
        return json.loads(line)

    def call(self, operation, args=None):
        ident = self.next_id
        self.next_id += 1
        result = self.raw(json.dumps({'id': ident, 'operation': operation, 'args': args or []}).encode())
        assert result['id'] == ident, result
        return result

    def end(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            raise
        err = self.proc.stderr.read().decode(errors='replace')
        rest = self.proc.stdout.read()
        assert self.proc.returncode == 0, f'child failed: {err}'
        assert not rest, f'child wrote unexpected protocol output {rest!r}'
        return err


def python_child():
    env = dict(os.environ)
    env['PYTHONPATH'] = os.pathsep.join((str(PYDIR), str(FIXTURE_DIR)))
    return Child([sys.executable, '-m', 'xytron.host_process', '--plugin-id', 'example_py',
                  '--module', 'python_fixture', '--class', 'Player', '--owner', 'session_1'], env)


def test_endpoint(make_child, plugin, method, prop):
    worker = make_child()
    prefix = plugin + '.Player.'
    try:
        handle = worker.call(prefix + 'create', ['Ada'])
        assert handle['ok'] and handle['result'].startswith('xy_'), handle
        token = handle['result']
        response = worker.call(prefix + 'method.' + method, [token, 9])
        assert response.get('ok') and response.get('result') == 91, response
        assert worker.call(prefix + 'get.' + prop, [token])['result'] == 91
        assert worker.call(prefix + 'set.' + prop, [token, 27])['ok']
        assert worker.call(prefix + 'get.' + prop, [token])['result'] == 27
        assert worker.call(prefix + 'method.Secret', [token]) == {
            'id': worker.next_id-1, 'ok': False, 'error': 'Rejected'}
        assert worker.call(prefix + 'method.' + method, [token, True])['error'] == 'Rejected'
        assert worker.call(prefix + 'get.' + prop, ['invented-handle'])['error'] == 'Rejected'
        assert worker.call(prefix + 'get.' + prop, [token, 'unexpected'])['error'] == 'Rejected'
        # The worker must not accept a user-supplied owner identity.
        extra = json.dumps({'id': worker.next_id, 'operation': prefix+'get.'+prop,
                            'args': [token], 'owner': 'attacker'}).encode()
        assert worker.raw(extra)['error'] == 'InvalidRequest'
        # An extreme exponent must not become an infinite native float.
        exp = ('{"id":55,"operation":"'+prefix+'method.'+method+'","args":["x",1e10000]}').encode()
        assert worker.raw(exp)['error'] == 'InvalidRequest'
        # Replayed and duplicate IDs are rejected.
        replay = json.dumps({'id': 1, 'operation': prefix+'get.'+prop, 'args': [token]}).encode()
        assert worker.raw(replay)['error'] == 'InvalidRequest'
        duplicate = b'{"id":55,"id":56,"operation":"'+prefix.encode()+b'get.'+prop.encode()+b'","args":[]}'
        assert worker.raw(duplicate)['error'] == 'InvalidRequest'
        assert worker.call(prefix + 'dispose', [token])['ok']
        assert worker.call(prefix + 'dispose', [token])['error'] == 'Rejected'
        # EOF must release any undisposed object from the trusted session.
        assert worker.call(prefix + 'create', ['StillAlive'])['ok']
        stderr = worker.end()
        assert 'CLOSED_' in stderr and 'PLUGIN_LOG' in stderr, stderr
    finally:
        if worker.proc.poll() is None:
            worker.proc.kill()
            worker.proc.wait(timeout=10)
    return 1


def test_isolation():
    a, b = python_child(), python_child()
    try:
        token = a.call('example_py.Player.create', ['Alice'])['result']
        assert b.call('example_py.Player.get.health', [token])['error'] == 'Rejected'
        assert a.call('example_py.Player.get.health', [token])['result'] == 100
        assert 'CLOSED_PYTHON' in a.end()
        b.end()
    finally:
        for x in (a, b):
            if x.proc.poll() is None:
                x.proc.kill(); x.proc.wait(timeout=10)


def test_oversize():
    child = python_child()
    try:
        result = child.raw(b'x' * (MAX_FRAME + 16))
        assert result['error'] == 'FrameTooLarge'
        child.end()
    finally:
        if child.proc.poll() is None:
            child.proc.kill(); child.proc.wait(timeout=10)


def test_unexported_bootstrap():
    env = dict(os.environ)
    env['PYTHONPATH'] = os.pathsep.join((str(PYDIR), str(FIXTURE_DIR)))
    result = subprocess.run([sys.executable, '-m', 'xytron.host_process',
         '--plugin-id', 'example_py', '--module', 'python_fixture',
         '--class', 'Unmarked', '--owner', 'session_1'], env=env,
         input=b'', capture_output=True, timeout=15)
    assert result.returncode != 0
    assert not result.stdout, result.stdout


def test_node_generated_bridge():
    if not __import__('shutil').which('node'):
        raise AssertionError('Node required for IPC JS integration test')
    with tempfile.TemporaryDirectory(prefix='xytron-ipc-') as tmp:
        project = Path(tmp)
        src = project / 'src/example_py'
        src.mkdir(parents=True)
        (project/'plugin.json').write_text(json.dumps({'schemaVersion':1, 'name':'example_py',
            'runtime':'python', 'entry':'example_py.plugin'}), encoding='utf-8')
        (src/'plugin.py').write_text((FIXTURE_DIR/'python_fixture.py').read_text(), encoding='utf-8')
        out = project/'bindings'
        generate = subprocess.run([sys.executable, str(ROOT/'tools/cli/generate_managed_bindings.py'),
                                  '--project', str(project), '--out', str(out)],
                                  capture_output=True, text=True)
        assert generate.returncode == 0, generate.stderr
        result = subprocess.run(['node', str(FIXTURE_DIR/'test_generated_bridge.mjs'), str(out/'index.mjs'),
                                 sys.executable, str(PYDIR), str(FIXTURE_DIR)],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'PASS: generated JS to real Python subprocess' in result.stdout, result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dotnet-dll', type=Path)
    args = parser.parse_args()
    checks = 0
    checks += test_endpoint(python_child, 'example_py', 'hit', 'health')
    checks += 1; test_isolation()
    checks += 1; test_oversize()
    checks += 1; test_unexported_bootstrap()
    checks += 1; test_node_generated_bridge()
    if args.dotnet_dll:
        assert args.dotnet_dll.is_file(), args.dotnet_dll
        checks += test_endpoint(lambda: Child(['dotnet', str(args.dotnet_dll)]), 'example_cs', 'Hit', 'Health')
    print(f'PASS: {checks} real subprocess IPC checks')


if __name__ == '__main__':
    main()
