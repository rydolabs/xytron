"""Trusted per-project Python Main worker entrypoint.

The native owner process passes a canonical backend directory as its only
argument. That directory is NOT supplied by JS; only explicitly exported
Main methods are served by Xytron's existing JSONL object host.
"""
import os
import sys

from xytron.host_process import serve


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Expected trusted backend directory")
    backend = os.path.realpath(sys.argv[1])
    if not os.path.isfile(os.path.join(backend, "main.py")):
        raise SystemExit("Missing Python Main backend")
    sys.path.insert(0, backend)
    serve("app", "main", ["Main"], "app_session")


if __name__ == "__main__":
    main()
