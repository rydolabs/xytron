# Xytron — Cross-Language Binding and PluginManager Contract

Status: **owner-accepted developer API direction, 2026-10-14**.
Implementation status is per-gate; this is NOT a claim that every API
below already exists.

Source design: user-supplied `Xytron_Cross_Language_Plugin_Binding.md`
(sections 1–11). This document preserves that source's registration model
while reconciling it with the existing **C++11 NativeWeb Core** and
**one Main + zero-or-more Modules + Plugin = packaged Module** product
architecture. The source's examples are design/pseudocode unless a
test below says otherwise.

## What the developer writes — three equivalent authoring surfaces

C++ (`include/xytron/plugin.hpp`):
```cpp
#include <xytron/plugin.hpp>
int add(int a, int b) { return a+b; }
XYTRON_MODULE(m) {
    m.function("add", &add);
    // Future gate: fluent class registration below is NOT in first slice.
    // m.class_<Player>("Player").constructor<>()
    //  .method("move", &Player::move)
    //  .property("hp", &Player::getHp, &Player::setHp);
}
```

C# (.NET host/bridge pending):
```csharp
using Xytron;
public static class Functions {
    [Export] public static int Add(int a, int b) => a + b;
}
[Export] public class Player { public int Hp { get; set; } = 100; }
```

Python (Python host/bridge pending):
```python
import xytron
@xytron.export
def add(a: int, b: int) -> int:
    return a + b
@xytron.export
class Player:
    def __init__(self): self.hp = 100
```

Export markers make an API **discoverable**, NOT authorized. Public members
of exported classes must be explicitly enumerated by binding-policy rules;
no private/underscore/internal member, arbitrary reflection, dynamic
dunder attribute or caller-supplied object pointer may be invoked.

## Main, Module, Plugin are roles, not languages

- Exactly one `main` role Module per app; started with the app (cpp,
  python or csharp). Zero or more `feature` Modules can use ANY of
  those languages simultaneously. Core is infrastructure, not Main.
- Plugin is a Module that has a standalone catalog package + loader,
  subject to version, identity, capabilities and code-lifetime checks.
- PluginManager only discovers/loads/owns independently packaged plugins.
  It is NOT a replacement for the app-scoped Module Registry, which
  routes both Main and feature/Plugin APIs.
- Module IDs are app-scoped, unique and stable. A duplicate ID, duplicate
  Main or duplicate exported function/class name is a deterministic error.

## Manifest and identity

**One authoritative plugin identity per plugin: `plugin.json`.** It owns
`name`, `version`, `runtime` (`native`, `python`, `dotnet`),
platform `entry` mapping and eventual method/capability metadata.
The app's `xytron.json` references an installed plugin package by path
or ID and separately declares Main and embedded Modules; it does not
silently rename the plugin. Historical `module.json` scaffolds are
legacy and need an explicit migration rule, not another source of truth.

```json
{
  "schemaVersion": 1, "name": "game", "version": "1.0.0",
  "runtime": "native",
  "entry": { "win-x64": "bin/win-x64/game.dll",
             "linux-x64": "bin/linux-x64/libgame.so" }
}
```

C ABI v1 requires an exported descriptor's `id` at plugin init time.
The C++ `XYTRON_MODULE(m)` interface has **no repeated name argument**:
the build tool reads `plugin.json` and defines a compile-time ID for
the shim. Loader still validates the exported ID against catalog and
app permission grants. Existing `NATIVEWEB_PLUGIN(Type, "id")` remains
supported; no ABI v1 breaking change. Never blindly trust an unverified
plugin manifest path or compiled ID.

## Internal representation and transport

- Keep **C++11** public and internal Core baseline. Reuse
  `nativeweb::Any`, `VariantList`, existing C++11 callable binding
  and `nw_value_v1` C ABI, NOT `std::any`, `if constexpr`, or
  C++17-only source sample as written.
- Language adapters map C++ templates / .NET Reflection / Python
  introspection into an app-scoped invoker. All JS calls return
  Promise-style async results; errors map to structured codes/messages.
- Core C ABI boundary accepts only stable C structs, opaque handles,
  callbacks; no `std::function` or C++ `Any` crossing DLL/SO binary
  boundaries. Python/.NET separate managed host process is the initial
  default (isolation/dependencies), with optional in-process wrappers
  as a distinct integration mode. Direct embedding is not implied by
  the source's CPython/.NET pseudocode.
- JS default: `await xytron.game.add(10,20)`. Native ES Module syntax
  `import { add, Player } from "game"` is a target requiring a generated
  static export proxy/metadata available before ESM link; loading native
  implementation can remain lazy. Do not claim this works until real
  CEF/WebView2 browser tests pass.
- Class binding target: `Player.create()`, methods, properties and
  dispose via verified opaque object handles; no JS-visible raw native
  pointers. Property reads/writes are async accessors unless explicitly
  implemented with a consistent proxy contract.

## Lifecycle, loading and security

Catalog discovers a validated `plugin.json` at startup without
executing code. State machine: Discovered -> Loading -> Loaded;
failure -> Failed; Disabled is explicit. Concurrent same-name imports
coalesce into one load, circular imports fail deterministically and
retry policy is explicit. Native DLL/SO code and in-flight invokers,
object handles, callbacks remain leased until safe cleanup. First
release may delay actual code unload until app shutdown.

**Native-dispatch permissions are required after lookup, before invoke**,
including object methods, events, cross-module invocation and class
constructors; explicit exports alone confer zero permissions. An
untrusted iframe/page must not gain access by doing a named `import`.
Enforce caller identity from browser host, not JS envelope metadata.
NW-014 continues to block release until per-method grants,
navigation/revocation and three-engine negative E2E are complete.

## Incremental implementation and evidence

- **B0**: C++11 adapter `XYTRON_MODULE(m)` plus `m.function()`;
  build-provided ID sourced from `plugin.json`, stable C ABI v1;
  real DLL/SO load + invocations + errors, old macro regression.
- **B1**: full versioned manifest parser/catalog, paths/duplicates,
  lazy/native plugin loader states with concurrency and lifetime tests.
- **B2**: C++ `class_<T>` constructor/method/property/fluent binding,
  opaque object ownership and async-safe method dispatch.
- **B3**: Python decorator + C# attribute and local export discovery tests,
  then managed host transport/async callbacks and mixed-language E2E.
- **B4**: JS static ESM proxy generation, exports known before link,
  real 3-engine import/class callback E2E and strict capabilities.
- **B5**: CLI restore/run/publish and actual shipped desktop app /
  embedded controls using the same catalog and Module Registry.

The gates in [RELEASE_TEST_MATRIX.md](RELEASE_TEST_MATRIX.md) separate
implemented unit/ABI slices from real managed-language/browser success.

## Implemented B0 C++ function registration (source; CI pending)

`include/xytron/plugin.hpp` provides C++11 `XYTRON_MODULE(m)` +
`m.function()` as a shim onto the original
`nativeweb_plugin_init_v1` and `nativeweb::PluginApi::bind`. The ID
and semantic version originate in plugin.json and are injected at
compile time by generated CMake (requires CMake 3.19+ for JSON).
The Core build remains CMake 3.16+ / C++11. CLI-created plugin source
does not repeat its plugin name and no duplicate module.json is written.

New C++11 regression loads a real DLL/SO, invokes typed functions,
checks propagated structured errors and registration cleanup. Old
NATIVEWEB_PLUGIN remains supported without source changes.
Classes/properties, ESM native named imports, runtime Python/.NET hosts
and generalized PluginManager catalog are **not** B0 features.

## B3 local authoring API foundations (independent from C++ B0)

- `bindings/python/xytron/__init__.py`: actual `@xytron.export` and
  local `discover_exports(module)` metadata enumeration. Undecorated,
  private and alias members are not exposed. Python 3.11 unittest runs
  separately on Windows and Linux.
- `bindings/dotnet/Xytron.Binding`: actual `[Export]` attribute and
  `ExportDiscovery` reflection metadata for approved public classes
  and static methods. .NET 8 Console smoke checks names, methods,
  properties and no unintended private API export.
- `.github/workflows/language-bindings.yml` verifies both authoring
  surfaces without a browser engine. Neither is connected to managed
  backend process/JS transport/permissions yet; A1-BIND-06/07 remain
  PARTIAL when tests pass. No claim of live cross-language plugins.

### CLI-generated plugin ABI end-to-end verification

The CI workflow compiles a plugin created by `xytron plugin new` and
`xytron plugin build --sdk`, loads the generated DLL/SO with
`PluginModule`, validates `plugin.json` name/version, invokes
`camera.hello()`, and confirms safe unregister. A compile-only plugin
test does NOT substitute for this binary load test. Real JS/ESM
imports remain a separate gate.

## Verified first-slice evidence — 2026-10-14

B0 SDK includes `include/xytron/plugin.hpp` and C++11
`XYTRON_MODULE(m)` + `m.function`; build-time id/version come from
plugin.json. A real standalone DLL/SO fixture is loaded by existing
PluginModule and returns typed values/errors in
[Core 38481436246](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481436246)
on Windows/Linux. The installed SDK new plugin template is generated,
compiled and **loaded/called** in
[CLI 38481853765](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853765)
on both systems. Old `NATIVEWEB_PLUGIN` remains.

B3 preliminary registration metadata: Python `@xytron.export`
and .NET `[Export]` reflection/discovery pass on both platforms in
[38481659399](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481659399).
These pieces have NO actual JavaScript transport. Class binding is
still illustrative in this document, not a working
`m.class_<Player>(...)` API. The `plugin.json` entry paths describe
future packaged locations; the current `xytron plugin build` output
is a CMake build directory and it does not yet install binaries under
those entry paths (NW-032).
