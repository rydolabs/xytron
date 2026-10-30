# NativeWeb Scope A1 — first usable SDK acceptance criteria

**Approved 2026-10-13:** First deliverable is an **Electron/Tauri-category desktop development platform with C/C++ Core** and **cpp/python/csharp user-selected app backends**.
It supports standalone OS window/app menu/context and embedded controls,
bidirectional plugin callbacks and redistributable engine SDK. A Core +
Plugin SDK-only target is insufficient. See [RELEASE_SCOPE_A.md](RELEASE_SCOPE_A.md) and
[RELEASE_TEST_MATRIX.md](RELEASE_TEST_MATRIX.md).

## Previously verified foundation (not sufficient for release)

| Gate | Status | Evidence |
| --- | --- | --- |
| Linux/Windows C++11 Core Regression | PASS for covered tests | CI 38322632990 |
| Linux CEF plugin object E2E | PASS | CI 38322633033 |
| Windows CEF plugin object E2E + parallel full build | PASS | CI 38322633033 |
| Windows WebView2 plugin object E2E | PASS | CI 38322633033 |
| Linux lifetime/ASan | PASS for covered tests | Linux ASan 21/21; repeated regression evidence in HANDOFF |
| Relocatable Core SDK consumer | PASS for Core only | Linux/Windows CI 38322633018 |
| App-scoped Main/Module Registry foundation | CORE verified (not JS routing) | Linux/Windows Core Regression 38334300674 |
| Native xytron scaffolds + generated C++ plugin compile | PASS for scaffolding and DLL/SO build only | CLI CI 38334470209, both OS |
| Exact method / frame policy value foundation | CORE verified (not actual CEF/WebView2 enforcement) | Core Regression 38334609344, both OS |
| Strict hosted local entry URL guard | Initial browser-side guard + positive JS plugin E2E PASS; negative origin/frame E2E still missing | Browser 38336744516 Linux CEF / Windows CEF / Windows WebView2 |
| Internal Module→BridgeRuntime qualified call | In-process stub handlers + unload-generation rejection PASS; actual Python/.NET transport missing | Core 38336744611 both OS |
| CLI embedded feature Module scaffolds | cpp/python/csharp source and manifest updates PASS; full CLI app execution missing | CLI 38336744554 both OS |

## New incremental coverage (not full release gates)

- **Actual unauthorized frame negative scenario:** an independently loaded
  local iframe attempts native `test.checkpoint`. The runner fails on any
  native side effect, and newer tests require the child script's ready
  message before a timeout can count. Initial complete
  [Linux CEF/Windows CEF/WebView2 run 38342611291](https://github.com/nssang00/chatgpt-write-test/actions/runs/38342611291)
  PASS; latest handshake
  [38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515)
  revalidates. All-method/iframe/navigation authorization remains BLOCKER.
- **CLI generated DLL/SO via command:** `xytron plugin build NAME --sdk PREFIX`
  PASSED Windows/Linux with real installed Core SDK:
  [CLI 38343029145](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343029145).
  Core-only SDK; run/restore/publish still BLOCKER.

## New compulsory Scope A1 gates

| Gate | Definition of done | Status |
| --- | --- | --- |
| MAIN/MODULE/PLUGIN | One Main + 0..N mixed-language Modules, Plugin packaged subtype, unified JS namespace, authenticated cross-module routing and lifecycle | NOT COMPLETE — BLOCKER |
| SECURITY | Native-boundary default deny for remote/untrusted docs, files, frames, redirects and ungranted methods; explicit narrow grants; real three-engine negative E2E | NOT COMPLETE — BLOCKER |
| CLI NEW/RESTORE/RUN/BUILD/TEST/PUBLISH | Working .NET-like xytron CLI MVP from fresh project through engine/SDK download to packaged run; useful diagnostics | NOT COMPLETE — BLOCKER |
| OS DESKTOP SHELL | Native windows, OS app/context menus, command callbacks and security tested on real browsers | NOT COMPLETE — BLOCKER |
| PYTHON BACKEND | Actual Python-written application logic from CLI project, async streaming and runtime packaging | NOT COMPLETE — BLOCKER |
| C# BACKEND | Actual .NET-written application logic from CLI project, Task/event callbacks and runtime packaging | NOT COMPLETE — BLOCKER |
| PLUGIN CALLBACKS | Actual DLL/SO plugin ↔ JS async callback/event/reference, error, unsubscribe and destroy/unload safety in all browsers | NOT COMPLETE — BLOCKER |
| PYTHON/C# WRAPPERS | Python and C# initial official wrapper API with async/handle/error/callback CI cases | NOT COMPLETE — BLOCKER |
| GREENFIELD APP | CLI-generated new native desktop app runs packaged UI+plugin on fresh Windows/Linux runtime | NOT COMPLETE — BLOCKER |
| WPF/WINFORMS/MFC CONTROL | Real minimal embedded controls and lifecycle/dispatch tests | NOT COMPLETE — BLOCKER |
| WIN32 HOST | Embedded native HWND sample, real Win WebView2/CEF bridge/plugin and create/resize/focus/reload/destroy verified | NOT COMPLETE — BLOCKER |
| GTK3 HOST | Embedded GTK3 app sample, Linux CEF bridge/plugin and lifecycle verified | NOT COMPLETE — BLOCKER |
| DISTRIBUTION | Engine-backed portable SDK, resources/runtime prerequisites, CMake install and relocatable app+plugin tests on clean Windows/Linux environments | NOT COMPLETE — BLOCKER |
| FAILURE BEHAVIOR | Missing/unsupported runtime, invalid/missing plugin, denied origins, shutdown races, deterministic errors | PARTIAL — BLOCKER |
| ADOPTION | Quickstart is sufficient to build/run an installed consumer without repo source/undocumented setup | NOT COMPLETE — BLOCKER |
| THIRD PARTY | Engine and SDK redistribution notices/licenses checked before external release | NOT COMPLETE — RELEASE BLOCKER |
| FINAL REVIEW | All required tests green and open P0/P1 blockers closed; permission/security exceptions and release explicitly approved | NOT COMPLETE |

**Preserve:** C++11 public baseline; `Engine::Auto`, `nativeweb::WebView`,
direct `xytron.foo()`, stable C ABI plugin and safe unload. Do not weaken
security to make a smoke test green.

**Deferred from this first deliverable:** extended/designer-grade MFC/WinForms/WPF/Qt adapters
(but not basic WPF/WinForms/MFC controls), full Python/C# wrapper parity,
advanced CLI plugin registries,
Sidecar, full Desktop API parity beyond native windows/menus/context menus,
advanced shared/transfer buffer, Code - OSS,
full installer/code signing. Existing APIs must not be removed.

**Interpretation:** Real plugin E2E PASS does not imply trusted-origin policy,
host integration or engine-inclusive package PASS. A release gate may only be
excluded by explicit approval. The current GitHub PR stays draft; `main`
and external releases require approval.

### 2026-10-13 latest executable CI evidence

At `4ff17af2`: Browser negative child-frame scenario and original
plugin lifecycle 3-engine E2E [38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515)
PASS; C++11 Core Linux/Windows [38343176859](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176859)
PASS; native `xytron plugin build` + generated DLL/SO Linux/Windows
[38343176992](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176992)
PASS. This is incremental verification, **not first-usable-SDK release acceptance**.

## Incremental CI evidence — same-entry iframe and SDK readiness

- Verified nested frame reloading *identical entry HTML*, with
  executable JS handshake and forbidden native side-effect assertion:
  [all-engine browser E2E 38354623946](https://github.com/nssang00/chatgpt-write-test/actions/runs/38354623946)
  PASSED.
- Real `xytron doctor --sdk` checking installed Core SDK and CMake:
  [Linux+Windows CLI 38355060435](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060435)
  PASSED, alongside `xytron plugin build` and real DLL/SO build.
- Other tested foundational tracks remain green: Core
  [38355060575](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060575),
  installed Core consumer
  [38355060701](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060701).
- **Still blocking full acceptance:** NW-014 full permission enforcement,
  actual standalone app shell/menus, Python/C# backend runtime,
  `xytron restore/run/publish`, SDK+CEF/WebView2 distribution and
  host controls.

## 2026-10-14 cross-language user registration slices

- **Verified C++11 free function authoring B0**:
  `XYTRON_MODULE(m) / m.function` inside actual DLL/SO;
  plugin.json-derived name/version, no duplicated user C++ id;
  C ABI v1 and NATIVEWEB_PLUGIN compatibility preserved.
  [Core 38481436246](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481436246)
  and [CLI 38481853765](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853765)
  PASS Windows/Linux, including installed SDK and real compiled
  plugin DLL/SO load/call.
- **Verified local authoring metadata only:** Python export decorator
  and C# ExportAttribute reflection unit tests
  [38481659399](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481659399)
  PASS Windows/Linux. Real language-host execution/JS RPC is a BLOCKER.
- **Release BLOCKERS unchanged:** C++ fluent class/objects/properties,
  PluginManager catalog/import/lifecycle, JavaScript ESM proxy,
  Python/.NET actual hosts, NW-014 full authorization and app runtime.
  `xytron plugin build` output is not yet packaged to plugin.json
  platform `entry` locations (NW-032).

### Real packaged app:// resource E2E — partial product milestone

CEF Linux/Windows and WebView2 Windows use actual app:// environment schemes and handlers; full `app://` plugin browser test and asset semantics 3/3 PASS [#38525221064](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221064) at source `d540df4a`. Core #38525221058 and CLI #38525221109 both OS PASS. This is **resource serving acceptance**, not a standalone desktop distribution release: NW-014 native auth, file-picking policy and `xytron run/build/publish` remain **BLOCKERS**.
