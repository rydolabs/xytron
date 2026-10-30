# NativeWeb unified Main Backend / Module / Plugin architecture

**Status:** Accepted architectural target by owner on 2026-10-13.
**Implementation status:** contract documented; initial registry work separate
from the runtime integration. No new engine E2E is implied.

## Logical model

- **Core:** C++11 runtime handles window/shell, WebView2/CEF, permission,
  IPC, dispatch, object handles, inter-language transport and lifecycle.
  Core is **not** the application's Main Backend.
- **Main Backend:** exactly **one designated logical Main Module** per
  Xytron desktop application. Its language is selected at `xytron new`
  time: `cpp`, `python` or `csharp`. It implements primary business logic,
  but it does not acquire unrestricted native permissions and does not
  directly own the Core's renderer lifecycle.
- **Module:** one logical set of APIs registered in an **application-scoped**
  Module Registry with a stable module ID, language, role, lifecycle and
  capability metadata. 0..N additional feature modules can coexist and
  can use ANY of the three supported languages, including the Main language.
- **Plugin:** a Module with independently buildable/deployable package,
  versioned descriptor and loader/unloader. C++ DLL/SO is the first plugin
  delivery model; Python and C# plugin packages are supported as a longer-
  term plugin *distribution* mode. A Module embedded in the app need not
  be a Plugin. A Plugin cannot opt out of Module authorization/lifetime.

**Invariant:** Every Plugin is a Module, not every Module a Plugin; Main
Backend is a Module with role `main`. Language and role are orthogonal
to packaging. Default is one Main + zero plugins; mixed languages are optional.

## Proposed application manifest contract (illustrative, not implemented)

```json
{
  "schemaVersion": 1,
  "app": { "id": "ai-studio", "mode": "desktop", "engine": "auto" },
  "main": { "id": "app", "language": "python", "entry": "backend/main.py" },
  "modules": [
    { "id": "camera", "language": "cpp", "delivery": "plugin", "path": "plugins/camera" },
    { "id": "erp", "language": "csharp", "delivery": "embedded", "path": "modules/erp" }
  ]
}
```

A manifest declaration does NOT automatically grant access. The native
runtime must authenticate executable/frame/caller identity and enforce
explicit method/module capabilities, including module-to-module calls.

## JavaScript ABI: backend-language independent

```js
const frame = await xytron.camera.capture(); // C++ DLL/SO plugin
const result = await xytron.ai.analyze(frame); // Python main/feature module
await xytron.erp.save(result); // C# feature module
xytron.on("ai.progress", payload => showProgress(payload));
```

The UI sees one JS contract (Promises, typed values, objects, errors,
events/callbacks/stream). A module implementation can move from Python
to C++ without changing its JS API when the declared contract remains
compatible. Main uses the same registry and transport contract; no
special unversioned `invokeMain` path is required.

## Registry and loader responsibilities

- Scope registry **per application**, not a mutable global across all
  windows/apps. Multiple WebViews share the authorized app registry while
  security grants are scoped to actual frame/window/caller identity.
- Require valid, unique IDs; refuse duplicate registrations, a second Main,
  package/ID conflict and reserved `xytron` or `__native_*` names.
- Store role: `main` or `feature`; language: cpp/python/csharp;
  delivery: embedded or plugin; origin/version/manifest permission metadata
  evolves separately.
- Plugin loader manages package/version verification, origin-safe method
  registration, module code lifetime, task leases, safe unloaded state.
- Module adapter sends requests to in-process native DLL/SO, managed Python
  or .NET process as needed. Host process crash cannot crash routing logic
  silently; pending calls receive structured errors.
- Module-to-module traffic goes through the same Core dispatcher and
  capability checker, so Python may call C++ and C# may call Python
  without introducing direct cross-language binding for every pair.
- Async callbacks/events/streams use scoped opaque tokens; revocation on
  navigation, unload or destroy prevents late native code from touching
  dead JavaScript contexts.

## Execution strategy

- C++ plugin: in-process DLL/SO by default for low-overhead device access,
  with versioned stable C ABI and conservative safe unload.
- Python and C#: supervised language-host process **recommended default**
  for dependency/runtime isolation; advanced in-process wrappers remain
  separately supported. The user-facing JS API must not expose the difference.
- Main Backend crash policy: Core keeps window alive long enough to report
  backend failure; configurable restart/exit policy later. No silent retry
  that hides a test failure.
- One Main is an application-level logical identity, not necessarily one
  process: an app may host many worker processes/threads behind it.

## Architectural test matrix (see RELEASE_TEST_MATRIX.md)

1. One Main (cpp/python/csharp), 0..N feature modules; roles independent
   of languages and of independent plugin packaging.
2. Missing Main, duplicate Main, duplicate IDs, invalid/reserved IDs,
   malformed manifest → deterministic diagnostic (no partial registration).
3. Per-app isolation: two apps may use same module ID independently;
   one module cannot call a second app's registry.
4. Lifecycle: unload with outstanding callback/object returns busy or waits
   under defined policy; after remove no new invocation, no UAF.
5. Mixed-language app: Python Main + C++ camera plugin + C# ERP module,
   JS direct calls, module-to-module calls, events/callbacks, shared errors.
6. Security: default denied remote frame/module-to-module calls without
   explicit grant; no Core/Plugin implicit bypass.
7. CLI: initial single-language app simple; `xytron module add` grows it
   without changing the default Main; `xytron plugin new/build` handles
   independent packages; publish includes only declared dependencies.
8. Recovery: module process crash, restart, callback cancellation and
   import/runtime diagnostics do not corrupt other modules.

## First implementation slices

**S0:** internal registry value model + duplicate/Main uniqueness + scoped
RAII registration/cleanup tests, C++11 Linux/Windows CI.
**S1:** manifest schema/parser and CLI generated scaffolds, safe validation.
**S2:** connect Module Registry to bridge dispatch, auth and PluginModule.
**S3:** Python/C# managed hosts and C++ plugin reverse callback transport.
**S4:** OS shell, host controls, package/CLI and mixed-language real E2E.

The S0 registry is **not** evidence that modules already route through the
real bridge or that Python/C#/CLI projects can execute. Those remain
explicit NOT IMPLEMENTED release gates.

## Cross-language developer binding design

See [CROSS_LANGUAGE_BINDING.md](CROSS_LANGUAGE_BINDING.md). Users declare
C++ `XYTRON_MODULE(m)`, Python `@xytron.export` or C# `[Export]`,
with the same JS Promise methods irrespective of language. Packaged
Plugin identity is authoritative in `plugin.json`. Main is still a
Module; Plugin remains a packaged feature, not a separate backend role.
Implementation gates A1-BIND-01..11 distinguish free-function C++ ABI
proof from future class, ESM, Python/.NET host and security proofs.
