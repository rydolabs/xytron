"""Experimental in-process host for explicitly exported Python classes.

A trusted transport MUST supply the owner identity (never take it from JS args),
perform NW-014 frame/origin permission checks, and start the host in the
appropriate isolated process. This module is not a JS transport or sandbox.
"""

import inspect
import secrets
import threading


class ObjectHostError(RuntimeError):
    """Invalid or unauthorized managed-object operation."""


class ObjectHost:
    """Dispatch Xytron class calls through a per-owner opaque handle table.

    The authorization callback is mandatory and must fail closed. Owner IDs
    must be issued by the trusted native bridge, not supplied by untrusted JS.
    """

    def __init__(self, plugin_id, classes, authorize, max_objects=1024):
        if not isinstance(plugin_id, str) or not plugin_id or '.' in plugin_id:
            raise ValueError('Invalid plugin ID')
        if not callable(authorize):
            raise ValueError('Explicit authorization callback is required')
        if not isinstance(max_objects, int) or isinstance(max_objects, bool) or max_objects <= 0:
            raise ValueError('Invalid object limit')
        self.plugin_id = plugin_id
        self._authorize = authorize
        self._limit = max_objects
        self._lock = threading.RLock()
        self._objects = {}
        self._classes = {}
        for cls in classes:
            if not inspect.isclass(cls) or not cls.__dict__.get('__xytron_export__', False):
                raise ValueError('Object hosts require explicitly exported classes')
            if cls.__name__.startswith('_') or cls.__name__ in self._classes:
                raise ValueError('Private or duplicate exported class')
            self._classes[cls.__name__] = cls

    @staticmethod
    def _arguments(target, args):
        try:
            signature = inspect.signature(target)
            bound = signature.bind(*args)
        except (ValueError, TypeError) as exc:
            raise ObjectHostError('Invalid argument count') from exc
        for name, value in bound.arguments.items():
            annotation = signature.parameters[name].annotation
            if annotation is inspect.Signature.empty:
                continue
            if annotation is int:
                okay = type(value) is int and abs(value) <= 2**53 - 1
            elif annotation is float:
                okay = type(value) in (int, float) and type(value) is not bool
            elif annotation in (str, bool, bytes):
                okay = type(value) is annotation
            elif annotation is None or annotation is type(None):
                okay = value is None
            else:
                raise ObjectHostError('Unsupported exported argument annotation')
            if not okay:
                raise ObjectHostError('Invalid argument type')

    def invoke(self, owner, operation, *args):
        if not isinstance(owner, str) or not owner:
            raise ObjectHostError('Missing trusted owner')
        if not isinstance(operation, str) or not operation.startswith(self.plugin_id + '.'):
            raise ObjectHostError('Unknown plugin operation')
        if self._authorize(owner, operation) is not True:
            raise ObjectHostError('Permission denied')
        parts = operation[len(self.plugin_id) + 1:].split('.')
        if len(parts) < 2 or len(parts) > 3:
            raise ObjectHostError('Invalid operation')
        cls = self._classes.get(parts[0])
        if cls is None:
            raise ObjectHostError('Unexported class')
        action = parts[1]
        with self._lock:
            if action == 'create' and len(parts) == 2:
                if len(self._objects) >= self._limit:
                    raise ObjectHostError('Object limit exceeded')
                self._arguments(cls, args)
                instance = cls(*args)
                handle = 'xy_' + secrets.token_urlsafe(32)
                self._objects[handle] = (owner, cls, instance)
                return handle
            if not args or type(args[0]) is not str:
                raise ObjectHostError('Missing object handle')
            handle = args[0]
            entry = self._objects.get(handle)
            if entry is None or entry[0] != owner or entry[1] is not cls:
                raise ObjectHostError('Unknown or foreign object handle')
            obj = entry[2]
            rest = args[1:]
            if action == 'dispose' and len(parts) == 2:
                if rest:
                    raise ObjectHostError('dispose takes only a handle')
                del self._objects[handle]
                self._close(obj)
                return None
            if len(parts) != 3 or parts[2].startswith('_'):
                raise ObjectHostError('Unexported member')
            name = parts[2]
            if action == 'method':
                member = cls.__dict__.get(name)
                if not inspect.isfunction(member) or not getattr(member, '__xytron_export__', False):
                    raise ObjectHostError('Unexported method')
                bound = getattr(obj, name)
                self._arguments(bound, rest)
                return bound(*rest)
            if action in ('get', 'set'):
                prop = cls.__dict__.get(name)
                if not isinstance(prop, property) or prop.fget is None or not getattr(prop.fget, '__xytron_export__', False):
                    raise ObjectHostError('Unexported property')
                if action == 'get':
                    if rest:
                        raise ObjectHostError('Property get takes no arguments')
                    return prop.fget(obj)
                if prop.fset is None:
                    raise ObjectHostError('Read-only property')
                self._arguments(prop.fset.__get__(obj, cls), rest)
                prop.fset(obj, *rest)
                return None
            raise ObjectHostError('Unknown class operation')

    @staticmethod
    def _close(obj):
        # A conventional close() hook is optional; it is NOT exposed to JS.
        close = getattr(obj, 'close', None)
        if callable(close):
            close()

    def cleanup(self, owner):
        """Invalidate all handles for a disconnected frame/session."""
        if not isinstance(owner, str) or not owner:
            raise ObjectHostError('Missing trusted owner')
        with self._lock:
            stale = [(handle, entry[2]) for handle, entry in self._objects.items() if entry[0] == owner]
            for handle, obj in stale:
                del self._objects[handle]
                try:
                    self._close(obj)
                except Exception:
                    pass  # Disconnect cleanup must invalidate all handles.
            return len(stale)

    @property
    def live_count(self):
        with self._lock:
            return len(self._objects)
