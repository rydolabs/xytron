"""Experimental single-owner, stdin/stdout JSONL worker for trusted Python plugins.

This worker is intentionally NOT a native bridge, authentication endpoint, or
sandbox. A trusted parent launches a separate worker for each frame/session,
assigns owner identity out-of-band, and is responsible for NW-014 checks.
Neither requests nor JS may choose the owner. No network listener is created.
"""
import argparse
import contextlib
import importlib
import inspect
import json
import math
import re
import sys

from .object_host import ObjectHost, ObjectHostError

MAX_LINE = 65536
MAX_ID = 2147483647
_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_]*\Z")


class ProtocolError(ValueError):
    pass


def _unique_pairs(items):
    result = {}
    for name, value in items:
        if name in result:
            raise ProtocolError("Duplicate JSON field")
        result[name] = value
    return result


def _bad_constant(_):
    raise ProtocolError("Non-finite JSON number")


def decode_request(line, last_id):
    try:
        data = json.loads(line, object_pairs_hook=_unique_pairs, parse_constant=_bad_constant)
    except (UnicodeDecodeError, ValueError, RecursionError) as exc:
        raise ProtocolError("Invalid JSON request") from exc
    if type(data) is not dict or set(data) != {"id", "operation", "args"}:
        raise ProtocolError("Unexpected request fields")
    ident = data["id"]
    if type(ident) is not int or not last_id < ident <= MAX_ID:
        raise ProtocolError("Request IDs must increase monotonically")
    if type(data["operation"]) is not str or not 0 < len(data["operation"]) <= 256:
        raise ProtocolError("Invalid operation")
    args = data["args"]
    if type(args) is not list or len(args) > 32 or any(type(x) not in (str, int, float, bool, type(None)) for x in args):
        raise ProtocolError("Unsupported argument encoding")
    if any(type(x) is float and not math.isfinite(x) for x in args):
        raise ProtocolError("Non-finite number")
    if any(type(x) is str and len(x) > MAX_LINE for x in args):
        raise ProtocolError("Argument too large")
    return ident, data["operation"], args


def _reply(out, payload):
    encoded = json.dumps(payload, ensure_ascii=True, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(encoded) + 1 > MAX_LINE:
        encoded = json.dumps({"id": payload.get("id"), "ok": False, "error": "ResultTooLarge"}).encode()
    out.write(encoded + b"\n")
    out.flush()


def serve(plugin_id, module_name, class_names, owner, reader=None, writer=None):
    """Run a worker. The owner is provisioned by a trusted parent, not a request."""
    if not _NAME.fullmatch(plugin_id) or not _NAME.fullmatch(owner):
        raise ValueError("Invalid trusted bootstrap identity")
    if not module_name or not all(_NAME.fullmatch(part) for part in module_name.split('.')):
        raise ValueError("Invalid trusted module name")
    if not class_names or any(not _NAME.fullmatch(name) for name in class_names):
        raise ValueError("Invalid trusted class name")
    reader = reader or sys.stdin.buffer
    writer = writer or sys.stdout.buffer
    # Plugin prints must not corrupt protocol stdout during ordinary calls.
    with contextlib.redirect_stdout(sys.stderr):
        module = importlib.import_module(module_name)
        classes = []
        for name in class_names:
            klass = getattr(module, name, None)
            if not inspect.isclass(klass) or klass.__module__ != module.__name__:
                raise ValueError("Unknown trusted plugin class")
            classes.append(klass)
    # The always-true callback is only appropriate inside this local worker.
    # The trusted NATIVE broker must do NW-014 permission checks before writing
    # to the worker's private pipe. Do NOT expose stdin/stdout to web content.
    host = ObjectHost(plugin_id, classes, lambda _owner, _op: True)
    last_id = 0
    try:
        while True:
            line = reader.readline(MAX_LINE + 2)
            if not line:
                break
            if len(line) > MAX_LINE or not line.endswith(b"\n"):
                _reply(writer, {"id": None, "ok": False, "error": "FrameTooLarge"})
                break  # Cannot safely recover stream framing.
            try:
                ident, op, args = decode_request(line, last_id)
            except ProtocolError:
                _reply(writer, {"id": None, "ok": False, "error": "InvalidRequest"})
                continue
            last_id = ident
            try:
                with contextlib.redirect_stdout(sys.stderr):
                    result = host.invoke(owner, op, *args)
                _reply(writer, {"id": ident, "ok": True, "result": result})
            except ObjectHostError:
                _reply(writer, {"id": ident, "ok": False, "error": "Rejected"})
            except (TypeError, ValueError, OverflowError):
                _reply(writer, {"id": ident, "ok": False, "error": "InvalidResult"})
            except Exception:
                # Never copy exception messages / tracebacks to browser responses.
                _reply(writer, {"id": ident, "ok": False, "error": "PluginError"})
    finally:
        with contextlib.redirect_stdout(sys.stderr):
            host.cleanup(owner)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-id", required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--class", dest="classes", action="append", required=True)
    parser.add_argument("--owner", required=True)
    options = parser.parse_args(argv)
    serve(options.plugin_id, options.module, options.classes, options.owner)


if __name__ == "__main__":
    main()
