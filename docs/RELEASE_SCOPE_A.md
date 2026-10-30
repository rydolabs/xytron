# NativeWeb first usable SDK — Scope A (approved 2026-10-13)

## Decision and product promise

This scope is governed by [PRODUCT_VISION.md](PRODUCT_VISION.md) and
[the owner-confirmed app/backend specification](PRODUCT_MODES_AND_BACKENDS.md).

**APPROVED: Scope A**, by the project owner in conversation on 2026-10-13.
This supersedes the earlier *Runtime + Plugin SDK only* first-release target.
The product is an **Electron/Tauri-category desktop application platform**
whose **common runtime is C/C++**, while app authors choose actual
`cpp`, `python` or `csharp` backend logic. It supports Electron-like
standalone app shell and WebView2-like embeddable controls. Electron's Node.js or
Tauri's Rust is **not** a required native backend. Official Python/C# wrappers
are first-class languages on the same underlying native runtime.

A first usable release must cover CLI-created Web UI apps with actual
**cpp/python/csharp** backends, an OS-native standalone window with
**application menu and context menu**, and embeddable controls, with a real
DLL/SO plugin and **bidirectional JS/native calls and callbacks**, packaged
so a developer can restore/build/run/publish without manually assembling
browser SDKs. See [product vision](PRODUCT_VISION.md).

No release number, `main` merge, or public distribution is authorized by
this scope decision alone.

## Unified module contract

One Main Backend is an app-scoped logical Module. Additional C++,
Python and C# Modules may coexist in the same app. Plugin is the
independently packaged Module delivery mode. The JS API is identical
regardless of the implementer language, Main/feature role and package
form. CLI-generated single-language apps remain simple; optional mixed
module declarations are validated and loaded by the same registry.

Full architectural contract: [MODULE_ARCHITECTURE.md](MODULE_ARCHITECTURE.md).

## Required deliverables (Scope A1)

1. **Core and engines:** C++11 public API, unchanged `nativeweb::WebView`
   abstraction, Linux CEF, Windows WebView2 (default when available), optional
   Windows CEF. Keep `Engine::Auto`.
2. **Main + module registry, bridge and plugins:** One logical Main,
   0..N language-independent feature Modules, separate Plugin delivery,
   unique IDs, safe registration/cleanup and cross-module routing.
    `xytron.foo()` direct calls, `xytron.invoke()`
   compatibility, Promise/error mappings, typed C++ bindings, factory-returned
   native objects, stable versioned C ABI DLL/SO, conservative safe unload.
3. **Security:** trusted *packaged app content* only by default. Deny remote
   documents, non-trusted local files, arbitrary child frames, navigation
   to a new origin, and unauthorized native methods at the native dispatch
   boundary, regardless of injected JS wrappers. Explicit, narrow opt-ins
   must be available for applications that genuinely need them.
   Browser engine *feature capabilities* do **not** imply permission grants.
4. **App shell and embedded controls:** NativeWeb standalone OS window,
   actual application menu and native context menu with click callbacks;
   plus minimal **WPF, WinForms, MFC** and Win32 control samples on Windows
   and GTK3 host sample on Linux. Validate create,
   resize, focus, reload, destroy, and JS-native callback in a real host app.
   Adapters remain thin; no new app framework, message loop mandate, or
   engine-specific public types.
5. **Redistributable developer SDK:** headers, CMake exported target(s),
   appropriate engine libraries/resources, real plugin fixture, sample source
   and documented runtime prerequisites. Include pinned CEF ICU/locales and
   Windows WebView2 Evergreen detection/diagnostics. No source-tree reliance.
   Verify licensing notices and permitted redistribution before publishing
   engine binaries.
6. **Real app vertical slices:** CLI-generated cpp, python and csharp
   standalone desktop apps plus embedded controls. Validate actual native
   menu/context-action callbacks, real backend invocation, C++ DLL/SO
   object creation/call/dispose, denial of untrusted pages, and clean exit
   on Windows and Linux.
7. **.NET-like CLI:** a working `xytron` MVP with `new`, `restore`/
   dependency download, `run`, `build`, `test`, `publish`, `doctor` and
   `plugin new/build`. The same project manifest drives all commands;
   restore is reproducible. Include a freshly generated **new desktop app**.
8. **Bidirectional plugin callbacks:** plugin→JS named call/event/callback
   with Promise/typed results and error propagation, lifecycle-safe callback
   ownership and unsubscribe/unload, tested across all three browsers.
9. **Python/C# backend plus wrapper vertical slices:** user-authored Python
   and C# backend methods called from JS with async/callback semantics,
   supervised startup/shutdown and dependency packaging; official initial
   wrappers expose the same native runtime, with representative
   async/callback/error and ownership behavior. Full feature parity can
   follow after the first usable release.
10. **Docs and verification:** quickstart, packaging/layout, examples,
    origin/capability model, troubleshooting, and Linux/Windows CI artifacts
    accompanied by logs.

## Intentional exclusions from Scope A1

- Designer-grade MFC, WinForms, WPF, Qt and other framework adapters
  **beyond mandatory minimal WPF/WinForms/MFC/Win32/GTK3 controls**
- Electron-wide Desktop API parity **beyond required window/app menu/context
  menu functions**, updater, shell-level installer, code signing
- Advanced CLI templates/registry services beyond the required xytron MVP,
  Sidecar runtime, TypeScript code generation
- TransferBuffer/SharedBuffer zero-copy/streaming, Code - OSS benchmark
- Dynamic plugin hot-reload guarantees beyond safe unload already specified
- Fully general arbitrary JS-function reference semantics beyond the
  required safe bidirectional plugin callback/event vertical slice.
  **The tested plugin→JS callback/event capability itself is mandatory.**

These remain planned features; do not remove existing code or compatibility
to enforce the first-release boundary.

## Scope A1 target platform matrix

- Windows x64: Win32 host, WebView2 primary; CEF optional explicit backend.
- Linux x64: GTK3 host with managed CEF.
- Other OS/CPU architectures and mature designer-level adapters later;
  minimum MFC/WinForms/WPF components and actual Python/C# backend profiles
  are release requirements, not deferred extras.
- Deployment includes a versioned engine/runtime SDK artifact and the
  **xytron CLI** generated standalone desktop app + native host consumer;
  not necessarily a platform-wide system installer.

## Staged macOS and WebKitGTK expansion (outside Scope A1)

Mac ARM64/x64 Core CI, WKWebView implementation, and Linux WebKitGTK 4.1
are additive roadmap items (see [port plan](MACOS_WEBKIT_PORT_PLAN_KO.md)).
They do not change approved Scope A1 release criteria or imply macOS browser
support is already shipped. WKWebView actual-browser E2E is a separate gate.

## Release policy

All required gates in `docs/ACCEPTANCE_CRITERIA.md` must be green.
`docs/RELEASE_TEST_MATRIX.md` defines reproducible positive and negative
test cases. Missing SDK/runtime/CI is a BLOCKED gate, never a PASS.

Only material API/ABI changes, unsafe permission defaults, scope changes,
financial commitments, destructive actions, or final merge/public release
require user approval. Report errors in `docs/ISSUE_LOG.md` and keep
independent workstreams moving.
