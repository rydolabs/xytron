#!/usr/bin/env python3
"""LOCAL-ONLY developer desktop staging. Never publish these files publicly.

This copies an already-built, explicitly selected trusted engine host and
backend into a relocatable local test folder. It does not verify third-party
redistribution rights, engine sandbox, installed Python/.NET prerequisites,
full dependencies, notarization or end-user security.
"""
import argparse
import os
from pathlib import Path
import shutil
import stat
import sys

SKIP_DIRS = {"__pycache__", ".pytest_cache"}
RUNTIME_EXT = {".dll", ".so", ".pak", ".bin", ".dat", ".json", ".manifest"}
MAX_FILES = 1800
# Pinned CEF Linux distributions can contain a >1 GiB libcef.so.
# Bound a single file, but do not reject a valid distribution.
MAX_BYTES = 3 * 1024 * 1024 * 1024


def checked_file(path: Path, *, project: bool = False) -> Path:
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"Missing, non-file or symlink input: {path}")
    if project and path.stat().st_nlink != 1:
        raise ValueError(f"Hard-linked project input is forbidden: {path}")
    return path


def checked_tree_files(root: Path, *, project: bool = False):
    if not root.is_dir() or root.is_symlink():
        raise ValueError(f"Missing or linked input directory: {root}")
    files = []
    for folder, dirs, names in os.walk(root, followlinks=False):
        directory = Path(folder)
        for name in list(dirs):
            p = directory / name
            if p.is_symlink():
                raise ValueError(f"Symlink directory forbidden: {p}")
            if name in SKIP_DIRS:
                dirs.remove(name)
        for name in names:
            p = directory / name
            if name.endswith(".pyc"):
                continue
            checked_file(p, project=project)
            rel = p.relative_to(root)
            if len(rel.parts) > 16:
                raise ValueError("Excessive bundle input depth")
            files.append(rel)
            if len(files) > MAX_FILES:
                raise ValueError("Too many files in local bundle")
    return sorted(files)


def copy_one(source: Path, output: Path, *, project: bool = False):
    checked_file(source, project=project)
    if source.stat().st_size > MAX_BYTES:
        raise ValueError(f"Oversized local bundle input: {source}")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or output.is_symlink():
        raise ValueError(f"Bundle destination collision: {output}")
    shutil.copy2(source, output, follow_symlinks=False)


def copy_tree(root: Path, destination: Path, *, project: bool = False):
    for name in checked_tree_files(root, project=project):
        copy_one(root / name, destination / name, project=project)


def runtime_file(rel: Path, executable_name: str):
    if rel.as_posix() == executable_name:
        return True
    if rel.suffix.lower() not in RUNTIME_EXT:
        return False
    # Ignore unrelated integration fixtures and arbitrary user content.
    return len(rel.parts) == 1 or (len(rel.parts) == 2 and rel.parts[0] == "locales")


def local_bundle(app: Path, host: Path, output_name: str, plugin: str):
    if not output_name or len(output_name) > 64 or output_name[0] not in "abcdefghijklmnopqrstuvwxyz" or any(
        c not in "abcdefghijklmnopqrstuvwxyz0123456789_-" for c in output_name
    ):
        raise ValueError("Bundle output must be a safe project identifier")
    if not app.is_dir() or app.is_symlink():
        raise ValueError("Invalid generated app root")
    exe = "xytron_desktop_host.exe" if os.name == "nt" else "xytron_desktop_host"
    if host.name != exe:
        raise ValueError("Explicit engine host executable has unexpected name")
    checked_file(host)
    runtime = host.parent
    runtime_files = checked_tree_files(runtime)
    if Path(exe) not in runtime_files:
        raise ValueError("Browser host missing from runtime folder")
    import json
    manifest = json.loads(checked_file(app / "xytron.json", project=True).read_text(encoding="utf-8"))
    lang = manifest["main"]["language"]
    if lang not in {"cpp", "python", "csharp"}:
        raise ValueError("Invalid Main language")
    if manifest["ui"]["entry"] != "app://desktop/index.html":
        raise ValueError("Unsupported UI entry")
    sdk_lock = checked_file(app / ".xytron/core.lock", project=True).read_text(encoding="utf-8").splitlines()
    if len(sdk_lock) != 3 or sdk_lock[0] != "xytron-core-lock-v1":
        raise ValueError("Invalid installed Core SDK lock")
    sdk = Path(sdk_lock[1])
    # The CLI already validates this lock against the installed SDK config.
    project_dist = app / "dist"
    if project_dist.is_symlink():
        raise ValueError("Linked dist destination forbidden")
    project_dist.mkdir(exist_ok=True)
    target = project_dist / output_name
    stage = project_dist / (output_name + ".stage")
    if target.exists() or target.is_symlink() or stage.exists() or stage.is_symlink():
        raise ValueError("Refusing to overwrite existing local bundle")
    stage.mkdir()
    try:
        copy_tree(app / "ui", stage / "ui", project=True)
        for rel in runtime_files:
            if runtime_file(rel, exe):
                copy_one(runtime / rel, stage / "runtime" / rel)
        if lang == "cpp":
            build = app / ".xytron-build/out"
            dll = "xytron_app_backend.dll" if os.name == "nt" else "libxytron_app_backend.so"
            candidate = build / "Release" / dll if os.name == "nt" else build / dll
            if os.name == "nt" and not candidate.is_file():
                candidate = build / dll
            copy_one(candidate, stage / "backend" / dll, project=True)
        elif lang == "python":
            copy_tree(app / "backend", stage / "backend", project=True)
            copy_one(sdk / "share/xytron/python/app_worker.py",
                     stage / "python/app_worker.py")
            copy_tree(sdk / "share/xytron/python/xytron",
                      stage / "python/xytron")
        else:
            copy_tree(app / ".xytron-build/dotnet",
                      stage / "backend", project=True)
        if lang != "cpp":
            copy_one(app / ".xytron-build/managed_operations.txt",
                     stage / "backend/managed_operations.txt", project=True)
        feature = ""
        if plugin:
            rel = Path(plugin)
            if rel.is_absolute() or ".." in rel.parts or "." in rel.parts or len(rel.parts) > 16:
                raise ValueError("Unsafe relative plugin path")
            if not all(part and all(c.isalnum() or c in "-_." for c in part) for part in rel.parts):
                raise ValueError("Unsafe plugin path characters")
            src = app / rel
            checked_file(src, project=True)
            feature = src.name
            copy_one(src, stage / "plugins" / feature, project=True)
        args = ['"--ui={ROOT}ui/index.html"']
        if lang == "cpp":
            args.append('"--backend-plugin={ROOT}backend/' + dll + '"')
        else:
            args.append('"--backend-language=' + lang + '"')
            args.append('"--managed-operations={ROOT}backend/managed_operations.txt"')
            if lang == "python":
                args.extend(['"--python-runner={ROOT}python/app_worker.py"',
                             '"--python-project={ROOT}backend"'])
            else:
                args.append('"--dotnet-assembly={ROOT}backend/MainBackend.dll"')
        if feature:
            args.append('"--plugin-path={ROOT}plugins/' + feature + '"')
        if os.name == "nt":
            app_id = manifest["app"]["id"]
            if (not isinstance(app_id, str) or not app_id or
                any(c not in "abcdefghijklmnopqrstuvwxyz0123456789_-" for c in app_id)):
                raise ValueError("Invalid app id for WebView2 profile")
            win_args = [x.replace("{ROOT}", "%ROOT%") for x in args]
            cmd = ('@echo off\r\nsetlocal\r\nset "ROOT=%~dp0"\r\n'
                   'if not defined LOCALAPPDATA set "LOCALAPPDATA=%TEMP%"\r\n'
                   'set "WEBVIEW2_USER_DATA_FOLDER=%LOCALAPPDATA%\\Xytron\\' +
                   app_id + '\\' + output_name + '\\WebView2"\r\n'
                   'if not exist "%WEBVIEW2_USER_DATA_FOLDER%" mkdir "%WEBVIEW2_USER_DATA_FOLDER%" >nul 2>&1\r\n'
                   'pushd "%ROOT%runtime"\r\n'
                   '"%ROOT%runtime\\xytron_desktop_host.exe" ' +
                   " ".join(win_args).replace("/", "\\") +
                   ' %*\r\nset "CODE=%ERRORLEVEL%"\r\npopd\r\nexit /b %CODE%\r\n')
            (stage / "launch.cmd").write_bytes(cmd.encode("utf-8"))
        else:
            unix_args = [x.replace("{ROOT}", "$ROOT/") for x in args]
            shell = ('#!/bin/sh\nset -eu\n'
                     'ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                     'cd "$ROOT/runtime"\n'
                     'export LD_LIBRARY_PATH="$ROOT/runtime"\n'
                     'exec "$ROOT/runtime/xytron_desktop_host" --no-sandbox ' +
                     " ".join(unix_args) + ' "$@"\n')
            entry = stage / "launch.sh"
            entry.write_text(shell, encoding="utf-8")
            entry.chmod(entry.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        (stage / "DO_NOT_REDISTRIBUTE.txt").write_text(
            "LOCAL-ONLY Xytron developer bundle. Do not publish.\n"
            "Browser binaries may be test-only with sandbox disabled.\n"
            "Public redistribution licenses, notices, managed runtime closure,\n"
            "code signing, installers and clean-machine acceptance are NOT approved.\n",
            encoding="utf-8",
        )
        stage.rename(target)
    except BaseException:
        # Deliberately leave staged contents for forensic inspection. No
        # recursive delete that could race a local file-system attacker.
        raise
    print(f"LOCAL-ONLY desktop staging PASS: {target}", flush=True)
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--app", type=Path, default=Path.cwd())
    p.add_argument("--host", type=Path, required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--plugin", default="")
    args = p.parse_args()
    try:
        return local_bundle(args.app.absolute(), args.host.resolve(strict=True),
                            args.output, args.plugin)
    except (ValueError, KeyError, OSError, TypeError) as error:
        print("Local bundle refused:", error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
