#!/usr/bin/env python3
"""Cross-platform C++11 broker -> real Python/.NET subprocess integration."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True)
parser.add_argument('--dotnet-dll', type=Path)
args = parser.parse_args()

binary = args.binary.resolve(strict=True)
fixture = args.fixture.resolve(strict=True)
cmd = [str(binary), sys.executable, str(fixture)]
if args.dotnet_dll:
    dll = args.dotnet_dll.resolve(strict=True)
    dotnet = shutil.which('dotnet')
    if not dotnet:
        raise SystemExit('FAIL: .NET executable not found')
    cmd.extend([str(Path(dotnet).resolve(strict=True)), str(dll)])
result = subprocess.run(cmd, text=True, capture_output=True, timeout=40)
print(result.stdout, end='')
if result.returncode:
    print(result.stderr, file=sys.stderr)
    raise SystemExit(result.returncode)
print('PASS: C++ broker subprocess test, Python' + (' + .NET' if args.dotnet_dll else ''))
