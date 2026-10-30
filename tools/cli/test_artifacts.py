#!/usr/bin/env python3
"""No third-party dependencies; regression for generated plugin templates.

The test intentionally exercises the installed xytron CLI, edits generated
user code, compiles it where a toolchain is available, checks ZIP contents
and confirms unsafe manifests are rejected. A JS/native runtime test is a
separate gate.
"""
import argparse
import importlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile
import warnings

from verify_artifact import verify_archive


def execute(exe, cwd, *args, expect=0):
    proc = subprocess.run([str(exe), *map(str, args)], cwd=str(cwd),
                          text=True, capture_output=True)
    if proc.returncode != expect:
        raise AssertionError(f"xytron {args}: expected {expect}, got {proc.returncode}\n"
                             f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
    return proc


def test_all(exe, repo, sdk, require_dotnet):
    checks = 0
    with tempfile.TemporaryDirectory(prefix="xytron-artifacts-") as root:
        root = Path(root)
        execute(exe, root, "new", "app", "workstation")
        assert json.loads((root / "workstation/xytron.json").read_text())["main"]["language"] == "cpp"
        checks += 1

        execute(exe, root, "new", "plugin", "python_job", "--lang", "python")
        python_project = root / "plugins/python_job"
        plugin_file = python_project / "src/python_job/plugin.py"
        plugin_file.write_text(plugin_file.read_text().replace("Hello,", "Modified!"))
        (python_project / "src/python_job/extra.py").write_bytes(b"value = 42\n")
        execute(exe, python_project, "build")
        assert json.loads((python_project / "plugin.json").read_text())["runtime"] == "python"
        sys.path[0:0] = [str(repo / "bindings/python"), str(python_project / "src")]
        try:
            from xytron import discover_exports
            mod = importlib.import_module("python_job.plugin")
            assert mod.hello("Xytron") == "Modified! Xytron!"
            assert discover_exports(mod)["hello"]["kind"] == "function"
        finally:
            del sys.path[:2]
        checks += 1

        execute(exe, python_project, "pack")
        pkg = python_project / "dist/python_job-1.0.0-any.xplugin"
        assert verify_archive(pkg)["runtime"] == "python"
        with zipfile.ZipFile(pkg) as z:
            assert z.testzip() is None
            assert json.loads(z.read("plugin.json"))["entry"] == "python_job.plugin"
            assert b"Modified!" in z.read("python/python_job/plugin.py")
            assert z.read("python/python_job/extra.py") == b"value = 42\n"
            assert not any("__pycache__" in n for n in z.namelist())
        execute(exe, python_project, "pack", expect=1)
        # Load actual packaged source with the Python export SDK, not the
        # original editable source tree. Verify the archive before extraction.
        installed = root / "unpacked-python"
        with zipfile.ZipFile(pkg) as z:
            z.extractall(installed)
        env = dict(os.environ)
        env['PYTHONPATH'] = os.pathsep.join([
            str(installed / "python"), str(repo / "bindings/python")])
        consumer = subprocess.run([
            sys.executable, '-c',
            'from python_job.plugin import hello; '
            'assert hello("Consumer") == "Modified! Consumer!"'
        ], env=env, cwd=str(root), capture_output=True, text=True)
        if consumer.returncode:
            raise AssertionError(f"Packaged Python consumer failed: {consumer.stdout} {consumer.stderr}")
        checks += 1

        # Independent consumer rejects duplicate entries, archive path escapes
        # and manifest mutations even when a freshly repacked ZIP has valid CRC.
        with zipfile.ZipFile(pkg) as z:
            clean = [(n, z.read(n)) for n in z.namelist()]
        def bad_repack(rewrite):
            malformed = root / pkg.name
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                with zipfile.ZipFile(malformed, 'w', compression=zipfile.ZIP_STORED) as z:
                    for n, payload in rewrite(clean):
                        z.writestr(n, payload)
            try:
                verify_archive(malformed)
            except ValueError:
                return
            raise AssertionError('Invalid repacked plugin unexpectedly accepted')
        bad_repack(lambda files: files + [('python/../escape.py', b'bad')])
        bad_repack(lambda files: files + [('plugin.json', files[0][1])])
        bad_repack(lambda files: files + [('plugin.JSON', files[0][1])])
        bad_repack(lambda files: [
            (n, b'{"schemaVersion":2,"metadata":{"schemaVersion":1},"name":"python_job","version":"1.0.0","runtime":"python","entry":"python_job.plugin"}' if n == 'plugin.json' else payload)
            for n, payload in files])
        bad_repack(lambda files: [
            (n, b'{"schemaVersion":1,"name":"python_job","name":"other","version":"1.0.0","runtime":"python","entry":"python_job.plugin"}' if n == 'plugin.json' else payload)
            for n, payload in files])
        # A changed payload with stale ZIP CRC must fail validation.
        raw = bytearray(pkg.read_bytes())
        where = raw.find(b'Modified!')
        assert where > 0
        raw[where] ^= 1
        corrupted = root / pkg.name
        corrupted.write_bytes(raw)
        try:
            verify_archive(corrupted)
        except zipfile.BadZipFile:
            pass
        else:
            raise AssertionError('Corrupted ZIP unexpectedly verified')
        checks += 1

        execute(exe, python_project, "publish", "--output", "releases")
        assert (python_project / "releases" / pkg.name).read_bytes() == pkg.read_bytes()
        execute(exe, python_project, "publish", "--output", "releases", expect=1)
        checks += 1

        # Malformed manifest is not a release, even if recognizable fields remain.
        original = (python_project / "plugin.json").read_text()
        (python_project / "plugin.json").write_text(original[:-3] + ", invalid!")
        execute(exe, python_project, "build", expect=1)
        (python_project / "plugin.json").write_text(original.replace('"runtime": "python"', '"runtime": "unsupported"'))
        execute(exe, python_project, "build", expect=1)
        (python_project / "plugin.json").write_text(original)
        # Schema fields must be resolved at the root rather than by regex.
        doc = json.loads(original)
        doc['schemaVersion'] = 2
        doc['metadata'] = {'schemaVersion': 1}
        (python_project / "plugin.json").write_text(json.dumps(doc))
        execute(exe, python_project, "build", expect=1)
        doc = json.loads(original)
        doc['metadata'] = {'entry': doc.pop('entry')}
        (python_project / "plugin.json").write_text(json.dumps(doc))
        execute(exe, python_project, "build", expect=1)
        doc = json.loads(original)
        doc['entry'] = {'unexpected': 'path'}
        (python_project / "plugin.json").write_text(json.dumps(doc))
        execute(exe, python_project, "build", expect=1)
        (python_project / "plugin.json").write_text(original)
        checks += 1

        # Source syntax errors must prevent a successful build.
        plugin_file.write_text("def bad(:\n")
        execute(exe, python_project, "build", expect=1)
        checks += 1

        if os.name != "nt":
            # No following symlinks outside the source tree during packaging.
            (python_project / "src/python_job/escape.py").symlink_to(root / "workstation/xytron.json")
            # Existing dist name would fail first; change version to get fresh output.
            (python_project / "plugin.json").write_text(original.replace('1.0.0', '1.0.1'))
            execute(exe, python_project, "pack", expect=1)
            (python_project / "src/python_job/escape.py").unlink()
            # The explicit manifest path is also a potential link escape.
            manifest = python_project / "plugin.json"
            manifest.unlink()
            external = root / "outside-manifest.json"
            external.write_text(original.replace('1.0.0', '1.0.2'))
            manifest.symlink_to(external)
            execute(exe, python_project, "pack", expect=1)
            manifest.unlink()
            manifest.write_text(original)
            checks += 1

        execute(exe, root, "new", "plugin", "camera", "--lang", "cpp")
        cpp = root / "plugins/camera"
        execute(exe, cpp, "pack", expect=1)
        if sdk:
            execute(exe, cpp, "build", "--sdk", sdk)
            execute(exe, cpp, "pack")
            native = cpp / "dist/camera-1.0.0-linux-x64.xplugin"
            if os.name == "nt":
                native = cpp / "dist/camera-1.0.0-win-x64.xplugin"
            assert verify_archive(native)["runtime"] == "native"
            with zipfile.ZipFile(native) as z:
                assert z.testzip() is None
                assert any(n.endswith("camera.dll" if os.name == "nt" else "libcamera.so") for n in z.namelist())
            checks += 1

        execute(exe, root, "new", "plugin", "reports", "--lang", "csharp")
        csproj = root / "plugins/reports"
        assert json.loads((csproj / "plugin.json").read_text())["runtime"] == "dotnet"
        if shutil.which("dotnet"):
            execute(exe, csproj, "build")
            execute(exe, csproj, "pack")
            assert verify_archive(csproj / "dist/reports-1.0.0-any.xplugin")["runtime"] == "dotnet"
            with zipfile.ZipFile(csproj / "dist/reports-1.0.0-any.xplugin") as z:
                assert z.testzip() is None
                assert "bin/reports.dll" in z.namelist()
            # No third-party test framework: compile/run a plain .NET console consumer.
            consumer = root / "consumer"
            consumer.mkdir()
            (consumer / "Consumer.csproj").write_text(
                '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                '<OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework>'
                '</PropertyGroup><ItemGroup>'
                '<ProjectReference Include="../plugins/reports/Plugin.csproj" />'
                '</ItemGroup></Project>')
            (consumer / "Program.cs").write_text(
                'using System;\nusing System.Reflection;\n'
                'if (Plugin.Hello("Xytron") != "Hello, Xytron!") throw new Exception("result");\n'
                'var m = typeof(Plugin).GetMethod("Hello");\n'
                'if (m is null || !m.IsDefined(typeof(Xytron.ExportAttribute), false)) '
                'throw new Exception("export missing");\n'
                'Console.WriteLine("CSharp export PASS");\n')
            proc = subprocess.run(["dotnet", "run", "--project", str(consumer / "Consumer.csproj")],
                                  cwd=str(root), capture_output=True, text=True)
            if proc.returncode != 0:
                raise AssertionError(f"C# consumer failed: {proc.stdout}\n{proc.stderr}")
            checks += 1
        elif require_dotnet:
            raise AssertionError(".NET SDK missing in required CI runtime")
        else:
            print("SKIP: C# build/consumer requires dotnet SDK")
        checks += 1

    print(f"PASS: {checks} artifact/template cases")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--xytron", type=Path, required=True)
    p.add_argument("--repo", type=Path, required=True)
    p.add_argument("--sdk", type=Path)
    p.add_argument("--require-dotnet", action="store_true")
    args = p.parse_args()
    test_all(args.xytron.resolve(), args.repo.resolve(),
             args.sdk.resolve() if args.sdk else None, args.require_dotnet)
