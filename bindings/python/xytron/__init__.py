"""Python user-facing export marker and LOCAL metadata discovery.

This does not run a NativeWeb language host or create a JS transport.
Only decorated public functions and classes become visible to discovery.
"""
import inspect

__all__ = ["export", "discover_exports"]


def export(value):
    if not (inspect.isfunction(value) or inspect.isclass(value)):
        raise TypeError("@xytron.export requires a function or class")
    if value.__name__.startswith("_"):
        raise ValueError("Private identifiers cannot be exported")
    setattr(value, "__xytron_export__", True)
    return value


def discover_exports(module):
    """Return safe registration metadata, never invoke arbitrary code."""
    discovered = {}
    for name, obj in vars(module).items():
        if name.startswith("_") or not getattr(obj, "__xytron_export__", False):
            continue
        if name != getattr(obj, "__name__", None):
            # Do not automatically expose aliases/reexports of another API.
            continue
        if name in discovered:
            raise ValueError("Duplicate Xytron export: " + name)

        if inspect.isfunction(obj):
            discovered[name] = {"kind": "function", "name": name}
        elif inspect.isclass(obj):
            methods = []
            properties = []
            for member_name, member in vars(obj).items():
                if member_name.startswith("_"):
                    continue
                if inspect.isfunction(member) and getattr(member, "__xytron_export__", False):
                    methods.append(member_name)
                elif isinstance(member, property) and member.fget and getattr(member.fget, "__xytron_export__", False):
                    properties.append(member_name)
            discovered[name] = {
                "kind": "class",
                "name": name,
                "methods": sorted(methods),
                "properties": sorted(properties),
            }
    return discovered
