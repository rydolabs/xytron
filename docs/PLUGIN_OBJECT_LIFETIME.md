# Plugin native objects: C ABI and unload baseline

## Status (2026-10-12)

Implemented in the core plugin module and tested with a real Linux `.so`:

- Optional **size-gated** extensions to the existing C ABI v1 host table:
  `register_object_factory` and `register_object_method`.
- `PluginModule::createObject(relativeType, runtime)` registers a typed native
  handle and its instance methods in `NativeObjectRuntime`.
- Instance callbacks use `nw_plugin_object_call_v1(user_data, object, args,
  argc, result, error)`. No C++ STL/RTTI types cross the DLL/SO boundary.
- `NativeObjectRuntime::call(handle, method, args)` acquires an object lease
  **before** leaving its entry lock, so concurrent `release(handle)` does not
  free a running instance. The same method dispatch is used by WebView's
  existing `__native_object.call` JS bridge.
- An object retains the provider module until its registered `destroy` callback
  finishes, even if `PluginModule` has already been destroyed.
- `PluginModule::unload()` quiesces new entries by unregistering functions and
  rejecting factory calls. While retained/queued/running callables or live
  objects still exist it raises `nativeweb::Error("plugin_busy", ...)`.
  The caller must drain/release those resources and **retry** `unload()`.
  Upon success the plugin `shutdown` callback runs and the module-owned
  library lease is released. Repeated successful unloads are no-ops.
- Destroying `PluginModule` retains the legacy **detach** behavior: it drops
  registrations and its own lease without forcing unsafe DLL/SO unloading.

## C ABI registration example

```c
/* Called by the plugin's register_api implementation. */
if (host->struct_size >=
    offsetof(nw_host_api_v1, register_object_method) +
    sizeof(host->register_object_method) &&
    host->register_object_factory && host->register_object_method) {
    host->register_object_factory(host->host_context,
        "camera", create_camera, destroy_camera, context);
    host->register_object_method(host->host_context,
        "camera", "capture", capture_camera, context);
}
```

**Registration contract:** The object factory is registered before its
methods. Relative type and method names are a single alphanumeric/`_`/`-`
segment. Instance pointer validity is restricted to the duration of the
callback; plugins must not schedule work using an unmanaged instance pointer.
Object destroy callbacks must be safe to run after the main plugin module
wrapper is gone; they run before the library lease is dropped.

**JS contract:** When the `NativeObjectHandle` is returned through WebView's
existing object bridge, JavaScript's proxy dispatches `await object.capture()`
to `__native_object.call`. `object.dispose()` calls
`__native_object.release`. The **WebView plugin loading/factory exposure** path
(`xytron.camera.open()` returning that handle) is not yet implemented;
`PluginModule` and `NativeObjectRuntime` are currently internal layers.

## Boundaries / follow-up work

1. Wire a plugin manager to `WebView` so factory-created handles can travel
   from `xytron.*` into the same `NativeObjectRuntime` instance.
2. Add an actual CEF/WebView2 end-to-end test and Windows DLL verification for
   the new object methods/unload behavior.
3. Define owner-scoped listeners/events, asynchronous native method callbacks,
   and a managed C++ wrapper for plugin object factories.
4. The current `unload()` guards NativeWeb-managed callables/objects, not raw
   pointers a plugin illicitly exports or threads it creates on its own.
   Plugin authors must drain their own resources in their shutdown protocol.
5. Quiescing is conservative: once a busy unload is attempted, new factory
   creation and new function lookups remain disabled; it is not rolled back.
