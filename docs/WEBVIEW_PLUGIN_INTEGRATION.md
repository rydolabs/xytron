# WebView plugin integration (initial C++11 facade)

```cpp
nativeweb::WebView view;
const std::string id = view.loadPlugin("./libobject_factory_plugin.so");
view.bindPluginFactory("camera.open", id, "counter");
// Once the WebView is created and its page has loaded:
// JS: const camera = await xytron.camera.open();
// JS: await camera.value(); await camera.dispose();
// C++ unload must wait for JS disposal / in-flight object calls.
view.unloadPlugin(id); // throws Error("plugin_busy", ...) while objects are alive
```

`xytron.camera.open()` uses the existing `NativeObjectHandle` response serialization and `__native_object.call`/`__native_object.release` routes; no new renderer bridge primitive is necessary. The factory is bound under the name chosen by the embedding application. Ordinary C ABI function registrations use plugin-provided names.

**Historical note:** The original facade supported only no-argument factories, unsynchronized plugin management, and post-registration duplicate checks. These limitations were addressed by the subsequent extensions below; CEF/WebView2 E2E and Windows DLL tests remain unverified.

## Argument-aware factories (ABI v1 optional extension)

`nw_host_api_v1::register_object_factory_args` is appended after
`register_object_method`. Plugins MUST check `host->struct_size` before reading
it. Existing `register_object_factory` callbacks remain unchanged and reject
nonempty argument lists with `plugin_factory_arguments_unsupported`.

Argument-aware constructors receive `nw_value_v1` views valid only while the
callback runs; on failure, return null and optionally fill `nw_error_v1`.
`WebView::createPluginObject(id, type, args)` and methods registered with
`WebView::bindPluginFactory` forward the supplied argument list. The current
value types follow ABI v1 (`bool`, `int32`, `double`, `string`, `binary`, null).

WebView plugin-manager operations use a mutex to serialize concurrent lifecycle
changes; the `PluginModule` protects in-flight construction and unload with its
existing lease and quiescing state. **Historical limitations addressed in the next extension:** duplicate plugin
IDs and queued callback ownership. Actual CEF/WebView2 browser end-to-end and
Windows DLL CI remain unverified.

## Duplicate IDs and queued callback lifetime (2026-10-13)

- `PluginModule` accepts an optional host-side ID validator. It reads the
  descriptor and validates the ID **before** `register_api()` can alter any
  `BindingRegistry` entry. `WebView::loadPlugin()` passes a validator under
  `pluginsMutex_`; concurrent conflicting loads return `plugin_duplicate`.
  Discovery still calls the plugin's `nativeweb_plugin_init_v1()` to read its
  descriptor, so init/shutdown callbacks **must not** mutate an existing
  instance's process-global state unsafely. This is not a pre-`dlopen` check.
- Internal native-object bridge callbacks capture a shared object runtime
  state instead of a raw `WebView::Impl*`. Factory callbacks also capture this
  state and a weak plugin reference. Shutdown closes creation under a mutex,
  then clears native handles. In-flight object methods retain an object and
  library lease until they complete; late responses are suppressed by the
  response sink and async dispatch shutdown.
- `plugin.webview_facade` tests duplicate loads, including four concurrent
  attempts, using an exported registration counter to assert no duplicate
  `register_api()` side effects. `plugin.async_lifetime` uses a fake browser
  backend to destroy `WebView` during worker-invoked plugin methods/factories.
- **Not covered:** browser-engine-thread affinity/reentrancy, arbitrary
  plugin-created background threads, or real CEF/WebView2 browser E2E. Public
  WebView API calls from multiple threads outside the plugin-manager subset
  are not generally guaranteed safe.
