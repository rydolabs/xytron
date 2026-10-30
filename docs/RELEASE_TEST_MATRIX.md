# Scope A1 release verification matrix

**Rule:** real host + real browser + real DLL/SO where the scenario crosses
those boundaries. A mock or header-only compile does not satisfy an E2E gate.

| ID | Scenario / expected result | Windows WebView2 | Windows CEF | Linux CEF | Status |
| --- | --- | --- | --- | --- | --- |
| A1-CORE-01 | C++11 Release build + CTest regressions pass | Required | Shared Windows Core | Required | Existing baseline PASS |
| A1-PLUG-01 | Real DLL/SO factory(77), methods, errors, dispose, busy/unload | Required | Required | Required | PASS: run 38322633033 |
| A1-SEC-01 | Trusted packaged entry loads; scoped native call succeeds | Required | Required | Required | NOT IMPLEMENTED |
| A1-SEC-02 | Remote https page has no native access by default | Required | Required | Required | NOT IMPLEMENTED |
| A1-SEC-03 | Arbitrary `file://` outside app root denied; path normalization/symlinks checked | Required | Required | Required | NOT IMPLEMENTED |
| A1-SEC-04 | Cross-origin and nested iframe (including `about:blank` inheritance) denied by default | Required | Required | Required | NOT IMPLEMENTED |
| A1-SEC-05 | Trusted page navigates/redirects to untrusted origin: prior grants and stale object handles cannot invoke | Required | Required | Required | NOT IMPLEMENTED |
| A1-SEC-06 | Forged JS message/frame/origin metadata and direct raw transport are rejected at native boundary | Required | Required | Required | NOT IMPLEMENTED |
| A1-SEC-07 | Missing method capability denied; explicit exact grant allows only that method | Required | Required | Required | NOT IMPLEMENTED |
| A1-HOST-01 | Existing Win32 host: create/resize/focus/reload/destroy with `Engine::Auto` | Required | Backend toggle | — | NOT IMPLEMENTED |
| A1-HOST-02 | Existing GTK3 window: same lifecycle, native callbacks | — | — | Required | NOT IMPLEMENTED |
| A1-HOST-03 | Both actual host samples invoke native function and real plugin, dispose safely | Required | Required | Required | NOT IMPLEMENTED |
| A1-DIST-01 | Install/relocate SDK then build C++11 consumer without source checkout | Required | Required | Required | Core-only PASS; engine distribution NOT IMPLEMENTED |
| A1-DIST-02 | Packaged CEF ICU/locales/DLL or SO and executable run from relocated bundle | Optional CEF | Required | Required | NOT IMPLEMENTED |
| A1-DIST-03 | WebView2 Evergreen available/missing paths reported accurately; fallback selection specified | Required | — | — | NOT IMPLEMENTED |
| A1-DIST-04 | C ABI plugin binary load / incompatibility / missing dependency gives actionable errors | Required | Required | Required | PARTIAL: prior regression; package case pending |
| A1-LIFE-01 | Rapid open/close, simultaneous objects, in-flight destroy/unload: no UAF, no permanent busy | Required | Required | Required | PARTIAL: Core regressions |
| A1-DOC-01 | Follow quickstart in clean CI environment; package licensed and reproducible | Required | Required | Required | NOT IMPLEMENTED |

## Additional performance observation matrix (not a new Scope A1 release gate)

| ID | Measurement | Platforms | Status |
| --- | --- | --- | --- |
| PERF-BRIDGE-01 | Real JS → Native → JS tiny RPC RTT; cold/warm p50/p95/p99 | Windows WebView2/CEF; Linux CEF; macOS WKWebView ARM/Intel | **PLANNED — no latency numbers measured** |
| PERF-BRIDGE-02 | Real Native → JS → Native `execute()` RTT / rejection | Same platform matrix | PLANNED |
| PERF-BRIDGE-03 | `postMessage` + explicit ack roundtrip (not one-way latency) | Same platform matrix | PLANNED |
| PERF-BRIDGE-04 | Binary 0B/1KiB/64KiB/1MiB and concurrency 1/8/32; ops/s | Same platform matrix | PLANNED |
| PERF-BRIDGE-05 | Managed Python/C# worker call overhead after bytes IPC | Appropriate available browser+worker combinations | PLANNED — blocked by managed binary IPC |

[Measurement methodology](XYTRON_BRIDGE_LATENCY_BENCHMARK_KO.md).
GitHub-hosted runner results are diagnostic initially; set alert/release
thresholds only after repeated baselines, without weakening security gates.

## Additional product-core gates — platform, plugins, CLI and wrappers

| ID | Tested user workflow / expected result | Required platform | Status |
| --- | --- | --- | --- |
| A1-CB-01 | Actual plugin invokes named JS callback; JS returns value asynchronously to plugin without blocking browser UI thread | Linux CEF, Win CEF, Win WebView2 | NOT IMPLEMENTED |
| A1-CB-02 | Plugin emits event; JS subscribes/unsubscribes; no delivery after unsubscribe or navigation | all 3 browsers | NOT IMPLEMENTED |
| A1-CB-03 | JS can pass callback reference/token to plugin as explicitly supported; callback can be invoked or rejected safely | all 3 browsers | NOT IMPLEMENTED |
| A1-CB-04 | Callback error/rejection propagates across C ABI as structured code/message, handles out-of-order completion | all 3 browsers | NOT IMPLEMENTED |
| A1-CB-05 | Plugin unload/reload and WebView destroy revoke live callback tokens without UAF/deadlock | all 3 browsers + ASan when available | NOT IMPLEMENTED |
| A1-CLI-01 | From empty workdir, `xytron new desktop hello --backend cpp` creates a valid new native desktop app | Windows and Linux | NOT IMPLEMENTED |
| A1-CLI-02 | `xytron restore` resolves/pins/downloads required SDK and browser engine, reuses cache, handles offline/error cases | Windows and Linux | NOT IMPLEMENTED |
| A1-CLI-03 | `xytron run` builds/launches real browser+plugin app with JS↔C++ callback test and auto engine | Windows and Linux | NOT IMPLEMENTED |
| A1-CLI-04 | `xytron build`, `xytron test`, `xytron publish` build/test/package runnable app without manual CMake SDK editing | Windows and Linux | NOT IMPLEMENTED |
| A1-CLI-05 | `xytron plugin new/build` creates valid versioned C ABI DLL/SO; `xytron doctor` diagnoses missing compiler/engine | Windows and Linux | NOT IMPLEMENTED |
| A1-WRAP-01 | Python wrapper attaches to Core and invokes typed native/JS action with structured async success/error | Linux and Windows target matrix | NOT IMPLEMENTED |
| A1-WRAP-02 | C#/.NET wrapper attaches to same Core and exposes Task-style async/error, correct handle disposal | Windows, with Linux parity where supported | NOT IMPLEMENTED |
| A1-WRAP-03 | Python and C# callbacks/handles remain safe across WebView destroy and runtime/plugin shutdown | Wrapper-specific CI + actual browser path | NOT IMPLEMENTED |
| A1-PRODUCT-01 | Generated new app and separately embedded host app use the same NativeWeb core/engine/bridge/plugin API | Windows and Linux | NOT IMPLEMENTED |

**Existing JS→plugin factory E2E PASS does not satisfy A1-CB cases.**
C++→JS Core calls already exist but must be exercised from a separately loaded
plugin and through the intended callback C ABI.

## Standalone OS shell, real language backends and managed controls

| ID | Scenario and expected observable behavior | CI platforms | Current state |
| --- | --- | --- | --- |
| A1-SHELL-01 | Standalone OS window create/title/show/resize/close; JS event routing | Linux CEF, Windows WebView2/CEF | NOT IMPLEMENTED |
| A1-SHELL-02 | Real native app menu create/update/disabled/checked/accelerator and command event | Linux + Windows | NOT IMPLEMENTED |
| A1-SHELL-03 | Native context menu right-click/popup/selection invokes JS/backend callback; closes cleanly | Linux + Windows | NOT IMPLEMENTED |
| A1-SHELL-04 | Untrusted page/frame cannot invoke restricted window/menu/context commands | Three engines | NOT IMPLEMENTED |
| A1-SHELL-05 | Multi-window menu routing and dispose/close callback invalidation | Linux + Windows | NOT IMPLEMENTED |
| A1-BACK-01 | CLI-generated `cpp` desktop app invokes real DLL/SO; plugin calls JS back | Windows + Linux | NOT IMPLEMENTED |
| A1-BACK-02 | CLI-generated `python` desktop app invokes real Python backend and receives async/stream callbacks | Windows + Linux | NOT IMPLEMENTED |
| A1-BACK-03 | CLI-generated `csharp` app invokes real .NET backend and receives Task/event callbacks | Windows + Linux | NOT IMPLEMENTED |
| A1-BACK-04 | Python/.NET missing dependency, crash, shutdown, callback cancel report meaningful errors | Windows + Linux | NOT IMPLEMENTED |
| A1-HOST-04 | MFC child control embeds WebView; real resize/focus/reload/disposal with MFC app menu unchanged | Windows WebView2/CEF | NOT IMPLEMENTED |
| A1-HOST-05 | WinForms control embeds WebView and calls C# backend safely on correct UI dispatcher | Windows WebView2 | NOT IMPLEMENTED |
| A1-HOST-06 | WPF component embeds WebView, respects WPF layout/dispatcher and native callback disposal | Windows WebView2 | NOT IMPLEMENTED |
| A1-DIST-05 | xytron publish includes Python runtime/dependencies as configured; new app runs on clean host | Windows + Linux | NOT IMPLEMENTED |
| A1-DIST-06 | xytron publish handles .NET runtime/config assets; new C# app runs on clean host | Windows + Linux | NOT IMPLEMENTED |

Python LLM/AI CI uses deterministic local fake-model output instead of
external subscription credentials. OS menu E2E cannot be satisfied with an
HTML-only menu. Embedded-control E2E cannot be satisfied by a standalone
browser host disguised as a component.

## Unified Main/Module/Plugin regression cases

| ID | Test case | Scope | Current state |
| --- | --- | --- | --- |
| A1-MOD-01 | Register exactly one Main; Main may be cpp/python/csharp | Core C++11 + CI | CORE Windows/Linux PASS: 38334300674; runtime E2E pending |
| A1-MOD-02 | Additional Modules accept all languages independent of Main | Core C++11 + CI | CORE Windows/Linux PASS: 38334300674; runtime E2E pending |
| A1-MOD-03 | Duplicate module IDs, invalid reserved IDs and second Main return stable codes | Core C++11 + CI | CORE Windows/Linux PASS: 38334300674; runtime E2E pending |
| A1-MOD-04 | RAII token removal, module destruction/expired registry and replacement safety | Core Release/ASan | CORE Windows/Linux PASS: 38334300674; new-test ASan pending |
| A1-MOD-05 | Registry is app-scoped; same IDs in different apps do not collide | Core C++11 + CI | CORE Windows/Linux PASS: 38334300674; runtime E2E pending |
| A1-MOD-06 | Plugin `delivery=plugin` is Module, but embedded feature Module is not independent package; manifest/build rejection of mismatch | CLI + Runtime E2E | NOT IMPLEMENTED |
| A1-MOD-07 | Main Python + C++ camera Plugin + C# ERP Module coexist, JS calls identical, actual backend-to-backend request/reply, callbacks/events | Win/Linux actual-browser E2E | NOT IMPLEMENTED |
| A1-MOD-08 | Module-to-module permissions not implicit; forged calls denied by Core even when Main initiates | three browser engines | NOT IMPLEMENTED |
| A1-MOD-09 | Module unload with in-flight JS/worker callbacks, crash and restart do not leak calls or crash other Modules | Windows/Linux real engines + ASan | NOT IMPLEMENTED |
| A1-CLI-06 | `xytron new` default creates exactly one Main; `module add` adds features without changing Main; `plugin new` creates independently delivered module | Win/Linux CLI end-to-end | NOT IMPLEMENTED |

These are additive to verified plugin object E2E, and do not replace
application-shell, Python/C# actual backend, security, packaging or embedded
control release gates.

## Verified implementation slices, not yet product E2E

- Module Registry Core test passes on Ubuntu and Windows: [Core Regression 38334300674](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334300674). This verifies A1-MOD-01..05 at metadata/RAII level; A1-MOD-06..09 require real runtime and remain blocked.
- Native xytron CLI scaffold and CLI-generated C++ plugin DLL/SO compilation against installed SDK pass on both systems: [Xytron CLI 38334470209](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334470209). It does NOT pass full A1-CLI-01/05 or end-user publish.
- Exact-method/frame default-deny C++ policy primitive passes on Linux/Windows: [Core Regression 38334609344](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334609344). Actual engine security boundary is NOT IMPLEMENTED/VERIFIED; A1-SEC-01..07 remain blocked.

## Failure workflow

For each failure record: test ID; exact command and SHA; OS, compiler,
SDK/runtime versions; expected vs actual; error output; severity; scope;
workaround; owner or next action; retest link. Do not silently retry flakiness.

## Interpretation

- "Existing baseline PASS" only certifies the previously executed narrow
  regression, not a host sample or packaged consumer.
- Windows CEF may be an optional *end-user choice*, but its advertised backend
  retains its CI parity gate.
- Security tests must exercise the final native dispatch/IPC boundary; simply
  hiding `xytron` from JS is insufficient.
- Sandbox/origin policy details may need engine-specific implementation.
  Any proposed relaxation of default-deny is a **requires approval** decision.
- Core events remain. Plugin-owned event ABI is tracked separately unless
  adopted into the first vertical slice.

## 2026-10-13 coverage update (CI source 7e45e72b)

| Coverage | Verified evidence | Does NOT satisfy |
| --- | --- | --- |
| Native method entry guard | `core.trusted_document` and 3-engine **positive** browser lifecycle PASS in 38336744516 | A1-SEC-01–07 real **negative** trust/origin/iframe/permission E2E |
| Module namespace dispatch | `core.module_bridge` + Module Registry C++11, Ubuntu/Windows Core run 38336744611 PASS | A1-MOD-07 actual Python/C# hosts; secure cross-module transport; unloaded DLL lease |
| CLI `module add` scaffolds | Python Main + cpp/python/csharp feature sources/manifests, CLI 38336744554 PASS both OS | A1-CLI-06 full runtime use and schema migration; A1-CLI run/restore/publish |
| Independent C++ Plugin DLL/SO generation | CLI tests install Core SDK and compile generated module both OS, 38336744554 PASS | Plugin `xytron plugin build` command and runnable published desktop app |
| Existing Plugin Browser E2E | All 3 engines 38336744516 PASS | Plugin→JS reverse callback/event ABI and full desktop app shell |

The unit tests reject invalid methods, wrong main frame/source and stale
module generations, but these tests alone are **not evidence** that
a malicious real page/frame could not trigger a native side effect.

## 2026-10-13 additional security negative E2E + actual CLI plugin build

| Test ID | Behavior | Automated evidence | Remaining |
| --- | --- | --- | --- |
| A1-SEC-08 | Cross-file child iframe tries native side-effectful method; trusted parent only passes if the unauthorized native checkpoint does NOT appear | Actual CEF Linux/Windows and WebView2 Windows: [38342611291](https://github.com/nssang00/chatgpt-write-test/actions/runs/38342611291) PASS; stronger iframe-ready handshake in [38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515) | One case only; remote top-level navigation, same-URL iframe, canonical app roots and method rights pending |
| A1-SEC-09 | Renderer responses/errors are subject to the engine-sourced sender document gate; WebView2 child denial must not be posted to main's Promise | Source-level check in `cef_backend.cpp` / `webview2_backend.cpp` + normal browser E2E | Forged native-response browser negative test not yet implemented |
| A1-CLI-07 | `xytron plugin build camera --sdk PREFIX` compiles the generated C++ DLL/SO with installed Core SDK on Windows/Linux | [CLI 38343029145](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343029145) PASS Windows + Linux (CLI invocation + CMake build) | Automatic SDK restore, app build/publish and signing not implemented |
| A1-CLI-08 | Unsafe SDK-prefix shell injection input rejected; invalid plugin ID denied | [CLI 38343029145](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343029145) PASS | Future arbitrary plugin CMake project sandbox not implemented |

**Interpretation:** Passing A1-SEC-08 does NOT close release security
A1-SEC-01..07. In WebView2 a child without injected bridge API is safe,
but its lack of a structured permission response is not equivalent to
engine-verified origin+frame capability enforcement.

### Latest executable source evidence for security negative test

The **stronger iframe-ready negative test** (not just the first version)
passes in [38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515)
on Linux CEF, Windows CEF and Windows WebView2. On the same source revision,
[38343176992](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176992)
passes both operating systems for `xytron plugin build`,
[38343176859](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176859)
passes Linux/Windows Core, and [38343176874](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176874)
passes the installed Core SDK consumer.

All tests in A1-SEC-01..07 are **not complete**. A1-SEC-08 specifically
verifies one independent child-file native-side-effect denial case.

| A1-SEC-10 | Same exact trusted entry URL loaded in a nested iframe must never invoke a native-side `test.checkpoint`; JS proves child loaded; main still works | Real Linux/Windows CEF + Windows WebView2 E2E, checkpoint `security-same-entry-iframe-blocked` | PENDING CI; does not close method grants or navigation issues |

## Latest verified additional test coverage (code through 0c879ae0)

| ID | Verification | Evidence | Scope limitation |
| --- | --- | --- | --- |
| A1-SEC-10 | **SAME trusted entry HTML in a child iframe** gets a ready handshake; child native checkpoint attempt causes no native effect while top-level still can invoke Plugin | [Real browser 38354623946](https://github.com/nssang00/chatgpt-write-test/actions/runs/38354623946) Linux CEF / Windows CEF / Windows WebView2 PASS | Same source does not imply method-level permission, navigation/revocation or arbitrary frame-proof security |
| A1-CLI-09 | `xytron doctor --sdk PREFIX` verifies Core header + CMake config + CMake executable on Windows/Linux and fails for missing SDK | [CLI 38355060435](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060435) PASS both OS | Does not check engine/runtime assets, Python/.NET environment, standalone apps or publish |
| REG-CORE | Existing C++11 Core behavior remains green | [Core 38355060575](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060575) PASS both OS | Not product E2E |
| REG-SDK | Core package consumed after installation | [Installed Consumer 38355060701](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060701) PASS both OS | Core-only |

**Do not close NW-014:** A1-SEC-10 tests one specific nested-frame
scenario but does not implement or verify complete method-level capability
permissions, origin canonicalization, navigation epochs or callback
authorization.

## Cross-language registration / plugin binding contract (owner 2026-10-14)

| ID | Acceptance case | Required evidence | Status |
| --- | --- | --- | --- |
| A1-BIND-01 | `XYTRON_MODULE(m)` C++11 free `m.function("add", &add)`; binary ID comes from `plugin.json`, without ID in source | Win DLL + Linux SO load and call through actual C ABI | **B0 PASS:** Core 38481436246 / CLI 38481853765 Win+Linux |
| A1-BIND-02 | Legacy `NATIVEWEB_PLUGIN(Type, "id")` works unchanged in same release | Old C++ ABI + real 3-engine plugin E2E | PRIOR BASELINE; rerun on new source |
| A1-BIND-03 | C++ fluent exported class constructors, methods (const and nonconst), properties, dispose and error mapping | Real plugin object factory ABI + Core/3-engine E2E | NOT IMPLEMENTED |
| A1-BIND-04 | `plugin.json` is sole authoritative name, file path/platform/version/runtime validated; duplicate names rejected | Parser/path traversal, mismatch, missing runtime and duplicate tests | NOT IMPLEMENTED |
| A1-BIND-05 | Startup discovery without executing module; first import single-load and concurrent coalescing | Core/real loader tests, crash/busy/cycle/failure/unload | NOT IMPLEMENTED |
| A1-BIND-06 | Python `@xytron.export` discovers only allowed function/class members, no private exposure | Python unit tests + eventually live managed host | **PARTIAL:** local discovery Windows/Linux PASS 38481659399; live host pending |
| A1-BIND-07 | C# `[Export]` discovers approved functions/classes/properties and maps errors | .NET unit tests + eventual live managed host | **PARTIAL:** local reflection Windows/Linux PASS 38481659399; live host/error mapping pending |
| A1-BIND-08 | JS `import { add, Player } from "game"` named imports linked from static export manifest/proxy; Promise returns correct | CEF Linux/Windows + WebView2 Windows real E2E | NOT IMPLEMENTED |
| A1-BIND-09 | One Python Main with C++ game Plugin + C# feature module; same JS call/result/error/event and module-to-module permissions | Multi-language real Windows/Linux E2E | NOT IMPLEMENTED |
| A1-BIND-10 | Untrusted origin/module cannot import/invoke ungranted function, class factory, method, property or event | Native dispatch negative 3-engine E2E + NW-014 closure | NOT IMPLEMENTED |
| A1-BIND-11 | Async callbacks/in-flight object method preserve DLL/host process leases; unload/reload cannot UAF | Stress + ASan + Windows/Linux engine E2E | NOT IMPLEMENTED |

Source examples are an **API proposal** until tests prove implementation.
B0 may pass independently of the later class, ESM, language-host and
security release gates. C++17 `std::any` sample is not the existing
NativeWeb C++11 ABI.

### Cross-language registration baseline evidence — 2026-10-14

- [Core Regression 38481436246](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481436246):
  Windows/Linux PASS. Includes real C++11 `XYTRON_MODULE` DLL/SO
  ABI fixture: typed values, structured error, manifest-derived ID/
  version, unregister. Legacy `NATIVEWEB_PLUGIN` regressions remain.
- [CLI 38481853765](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853765):
  Ubuntu/Windows PASS. Includes newly generated `plugin.json` CMake
  build AND direct re-load of generated camera DLL/SO with method invocation.
- [Language discovery 38481659399](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481659399):
  Python 3.11 decorator metadata and .NET 8 attribute/reflection
  tests PASS Windows/Linux. Both are LOCAL discovery only, not actual
  Python/.NET backend JS IPC/streaming/callbacks.
- Browser engine regression for this source still tracked separately;
  B0 free-function SDK tests do not prove native JS named-ESM import,
  new class properties, method grants or real mixed-language services.

A1-BIND-03..05 and A1-BIND-08..11 remain NOT IMPLEMENTED.

## 2026-10-14 definitive cross-platform CI evidence for binding slice (source c2b4dd5a)

- **[Browser plugin E2E 38481853653](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853653):**
  Linux CEF, Windows CEF, Windows WebView2 **ALL PASS** (legacy
  JS->Plugin object lifecycle/negative frame regression preserved;
  not a new ESM named-import or managed-language host E2E).
- **[Core Regression 38481853664](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853664):**
  C++11 Linux+Windows PASS, including new actual binary
  `plugin.xytron_function_binding` and old NATIVEWEB_PLUGIN cases.
- **[CLI 38481853765](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853765):**
  Windows+Linux PASS, including installed SDK and loader/invocation of
  the CLI-generated plugin DLL/SO with plugin.json descriptor.
- **[Python/C# exports 38481853706](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853706):**
  Python local decorator and .NET 8 Reflection PASS Windows+Linux.
- **[Installed Core Consumer 38481853757](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853757):**
  Windows+Linux PASS; Core SDK only.

A1-BIND-01 B0 is verified. A1-BIND-06/07 are *local metadata only*
and need actual host IPC/JS callbacks. NW-014 P0 native permissions,
class/properties, PluginManager, native ES module imports, language
hosts, app shell and full publish remain unfinished.

## A1-URL series — source of truth: docs/APP_RESOURCE_PROTOCOLS.md

| Gate | Expected result | State |
| --- | --- | --- |
| A1-URL-00 | Strict app://host/path bytes+MIME bundle; invalid URL/duplicate/cross-scheme denial | **PASS:** Core #38525221058 Win/Linux |
| A1-URL-01 | app://desktop packaged HTML, relative JS/CSS/SVG, MIME and Fetch in 3 engines (test entry uses plugin_bridge_test.html) | **PASS:** Browser #38525221064 all 3 engines |
| A1-URL-02 | unknown asset 404 and app://desktop cannot read app://camera private bytes | **PASS for tested 404/cross-origin case:** Browser #38525221064; deeper origin testing pending |
| A1-URL-03 | traversal/percent-encoded path rejected by canonical in-memory registry; filesystem-backed adapter/symlink policy separate | **CORE PASS** #38525221058; filesystem/symlink and multi-engine malformed-request E2E PENDING |
| A1-URL-04 | untrusted http(s)/file iframe cannot obtain app API grants | NW-014 / NOT IMPLEMENTED |
| A1-URL-05 | legacy exact-entry file:/// page and plugin lifecycle compatible on 3 engines | **PASS:** Browser #38525221064 |
| A1-URL-06 | custom schemes validated before engine startup; late/duplicate declaration fails | NOT IMPLEMENTED |
| A1-URL-07 | explicit native method grants at app:// trusted origin | NW-014 / NOT IMPLEMENTED |
| A1-URL-08 | callback/plugin revocation on navigation/unload | NOT IMPLEMENTED |

| A1-URL-09 | Resource bundle snapshot and 16 MiB item/64 MiB package bounds | **CORE PASS:** #38525221058 |
| A1-URL-10 | CLI new desktop cpp/python/csharp declares app:// UI entry; no runtime claim | **CLI PASS:** #38525221109; run/build/publish pending |

Pass A1-URL-00 alone does NOT mean app:// URLs are renderable by
CEF or WebView2. Do not mark A1-URL-01 as done without real engine CI.

### Expanded real browser app:// resource assertions

- JS external script execution: `app_resource_script.js`, relative URL
- CSS application: computed style reads `--xytron-app-style`
- SVG image fetch: status, MIME and real bytes from bundled resource
- Missing resource returns HTTP-style 404, without filesystem fallback
- app://desktop is denied access to explicit app://camera private bytes
- Existing native bridge method/object lifetime and iframe denial
  regressions still pass in both modes
These are executable source assertions; label PASS only after all
3 engines succeed on the same commit.

## 2026-10-14 definitive U0/U1/U2 app:// verification (source d540df4a)

The **real packaged \`app://\` resource protocol** is now implemented on
**Linux CEF, Windows CEF and Windows WebView2**, including:
- Explicit C++11 in-memory \`AppResourceBundle\` with strict canonical
  URLs, MIME types, duplicate rejection, 16 MiB per asset / 64 MiB per
  bundle; snapshot at \`WebView::create\` prevents caller mutation races.
- CEF secure/standard custom scheme, per-WebView isolated request
  context and scheme factory; WebView2 secure authority-bearing custom
  environment scheme and byte-stream \`WebResourceRequested\` responses.
- **Real browser E2E** serves HTML + relative JS/CSS/SVG assets, checks
  execution and MIME, Fetch success / undeclared asset 404, and denies
  reading private bytes from \`app://camera\` in a
  \`app://desktop\` page. Legacy exact-entry \`file://\` UI and
  JS/native plugin object lifecycle regressions remain tested.
- \`xytron new desktop\` CLI manifests for C++/Python/C# Main backends
  declare \`ui.entry=app://desktop/index.html\` and \`ui.resources=ui\`.
  This is **scaffolding only**; \`xytron run/build/publish\` and a
  packaged app shell remain unimplemented.

**All-green source and CI**
- [Actual 3-engine browser E2E #38525221064](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221064):
  Linux CEF, Windows CEF, Windows WebView2 PASS (both legacy file://
  plugin E2E and packaged app:// resource lifecycle E2E).
- [C++11 Core Regression #38525221058](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221058):
  Windows and Ubuntu PASS.
- [Native CLI #38525221109](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221109):
  Windows and Ubuntu PASS, including new generated manifest declaration.
- [Installed Core SDK #38525221089](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221089):
  Windows and Ubuntu PASS (Core-only, not engine-inclusive distribution).

**Not release-complete:** NW-014 **P0** native per-method, original frame,
navigation and native-object grants; file:// user-approved file picker
and filesystem confinement; late/duplicate declared custom scheme
registration; app resource bundle packaging/restore; actual desktop
shell, \`xytron run/build/publish\` and full embedded host controls.
\`app://\` page origin alone is NOT automatic native access.
