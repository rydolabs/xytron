# NativeWeb / Xytron — Desktop shell, embed controls, backend profiles

**Owner clarification: 2026-10-13.** This document supersedes narrower
claims that the product is *only* a native embedding SDK, that Python/C#
only provide wrappers, or that CLI/shell menus are outside first release.

## Product category and two presentation modes

NativeWeb/Xytron is an **Electron/Tauri-category desktop app platform**.
The shared native foundation is C/C++11, with CEF/WebView2 web-renderer
backends and a secured JS/native bridge. Browser-engine choice is separate
from app backend-language choice.

**Additive platform roadmap (2026-10-16):** macOS uses system WKWebView as the
preferred engine (macOS 15 ARM64/Intel Core CI first); Linux CEF stays the
primary engine with WebKitGTK 4.1/GTK3 as a future opt-in alternative. These
adapters are not implemented by the existing CEF/WebView2 source code and are
not new Scope A1 release gates. See [platform port plan](MACOS_WEBKIT_PORT_PLAN_KO.md).

**Standalone mode (Electron-like):** NativeWeb owns the desktop application's
top-level OS window and lifecycle, app menu bar, **native context menu** and
the user events for menu commands. Initial shell API includes window
create/title/show/resize/close, application-menu items (IDs, labels,
enabled/checked, shortcuts), contextual popup menus, click callbacks,
and basic dialog/clipboard primitives. An HTML menu simulation does **not**
count as a native menu. Later APIs: tray, updater, notification, session,
system integration. Command events must respect security/capability scopes.

**Embedded-control mode (WebView2-like):** The host application owns its
window/message loop/menus. Thin NativeWeb controls integrate with
**WPF, WinForms, MFC**, Win32 and GTK3; Qt and further host controls can
follow. Required basic controls support parent handle/widget, bounds,
focus, reload, navigation, event callbacks and safe disposal. They do not
force a new application framework or replace the host app's menu logic.

Both modes share the SAME C/C++ runtime, JS API, plugin manager, security
policy and engine selection, rather than separate JS bridge products.

## User-selected backend profiles

| `xytron --backend` | User-authored backend | Typical application | Hosting requirements |
| --- | --- | --- | --- |
| `cpp` | C/C++ native code | industrial devices, cameras, CAD, fast compute | In-process C++11, easy DLL/SO plugin with versioned C ABI |
| `python` | Actual Python application logic | LLM/AI/agents, data, local inference | Managed Python backend runtime/process or binding, async callbacks/streams, environment/package restore |
| `csharp` | Actual C#/.NET application logic | WPF/WinForms legacy, enterprise services | Managed .NET backend host or in-process wrapper, Task/events and runtime/package restore |

**Python and C# are NOT merely C++ wrapper utilities.** They are choices
for authoring the application's actual backend, with first-class JS
calls, callback/events, cancellation, errors and lifecycle. Native
Python/.NET wrappers also remain available for embedding. Process
separation or FFI is an implementation choice; no need to hard-code a
single language-host transport before its platform tests. For LLM CI,
use deterministic offline streaming or a fake model; never require a
paid API key.

## C++ plugin workflow and reverse callback

A C++ developer can run `xytron plugin new camera --language cpp` and
`xytron plugin build camera` to produce a DLL/SO. Minimal C++ metadata
registers functions and object methods without handwritten CEF IPC.
JS calls typed plugin functions and receives promises/native-object
handles. Plugins must ALSO invoke JS handlers or emit events, receive
asynchronous results/errors and safely dispose callback subscriptions.
The C ABI uses opaque tokens/IDs, never raw JS pointers, with correct
worker/UI dispatch, navigation/destroy cancellation and unload rules.

The previous real-browser test verifies JS→plugin calls and object
lifetime **only**; plugin-owned callback/event support remains a
separate unimplemented release gate.

## CLI: dotnet-like, but for web-based native desktop apps

Product-facing command is **`xytron`**, as specified by the owner.
Older `xweb` working-name examples are historical; retain any
compatibility alias only if an implemented command needs migration.
Targets below are examples, **not executed/implemented commands**:

```bash
xytron new desktop ai-studio --backend python --ui react
xytron new desktop legacy --backend csharp --ui vanilla
xytron new desktop camera --backend cpp --ui vue
xytron new control embedded --host wpf
xytron restore
xytron run
xytron build --configuration release
xytron test
xytron publish --runtime win-x64
xytron plugin new camera --language cpp
xytron plugin build camera
xytron doctor
```

Use one manifest and lock file for engine selection/version,
compiler/backend SDK, dependencies, scripts, plugins, UI assets,
security grants, shell/windows/menu metadata and packaging. Restore
downloads pinned dependencies into a repeatable cache with offline
diagnostics. Run starts the frontend and selected backend; publish
contains platform resources and valid runtime dependencies.

## First usable release requirements

- Real standalone app shell: window + native app menu + native context
  menu, including JS and chosen backend menu-action callbacks.
- Real native embedding in Win32/GTK3 plus minimum WPF/WinForms/MFC
  controls, retaining existing app lifecycle and menu ownership.
- Generated, runnable `cpp`, `python`, `csharp` desktop app profiles;
  test at least one real JS↔backend async/callback in each.
- Real C++ plugin binary + JS↔native callback/event and lifecycle.
- Native-side default-deny origin/frame/method permissions.
- Working xytron new/restore/run/build/test/publish/doctor/plugin commands,
  engine and backend dependency packaging on clean Windows/Linux.
- Existing C++11 Core and three-engine plugin E2E stay green, but
  cannot substitute for shell/language-profile/host-control E2E.

Full Electron Desktop API parity, a designer, complex CLI ecosystems,
full Python/C# API parity, advanced zero-copy and Code-OSS remain later
phases. No `main` merge or public release without explicit approval.

## Owner-approved Main/Module/Plugin relationship

A desktop application has exactly one logical **Main Backend** (one
`main` role Module) and 0..N **feature Modules**, of any language.
A **Plugin** is an independently packaged/loaded Module. A Plugin may
be written in C++, Python or C#; C++ DLL/SO is the first implementation.
The common JS API has no different prefix or calling semantics for Main
versus feature/Plugin Modules. Mixed-language feature modules can coexist
with any Main language and can call one another through the Core dispatcher.

See [unified module architecture](MODULE_ARCHITECTURE.md) for IDs, roles,
isolation, authorization, lifecycle and test cases.
