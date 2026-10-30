# NativeWeb autonomous execution plan — C/C++-first platform / Scope A
Updated 2026-10-13 after product-identity clarification

## Goal and developer stories

NativeWeb is in the Electron/Tauri product category, with a C/C++ native
backend, C++ DLL/SO plugins, bidirectional JS/native callbacks, OS-native app
menus/context menus, real cpp/python/csharp application backend profiles,
minimum WPF/WinForms/MFC controls, and a `dotnet`-like `xytron` CLI.

**Story 1 — New app:** `xytron new` → `xytron restore` → `xytron run` →
C++ plugin and JS callbacks → `xytron build`/`xytron publish` → standalone app.

**Story 2 — Existing app:** install SDK → embed Win32/GTK3 WebView →
use the *same* plugin/bridge from local UI → build/distribute.

**Story 3 — AI/legacy backends:** Python LLM and C# legacy developers write
actual application logic in their chosen language with async JS↔backend
callbacks; native wrappers or supervised processes are internal host choices.

See `PRODUCT_VISION.md`, `RELEASE_SCOPE_A.md`,
`RELEASE_TEST_MATRIX.md`, `ACCEPTANCE_CRITERIA.md`.

**Story 4 — OS app shell:** native window, app menu and context menu
create/update/click callbacks work in standalone mode, while host menus
are retained by embedded WPF/WinForms/MFC controls.

## Confirmed cross-language binding workstream (2026-10-14)

Reference: [CROSS_LANGUAGE_BINDING.md](CROSS_LANGUAGE_BINDING.md),
derived from owner-supplied Xytron cross-language plugin binding design.

In parallel with P0 NW-014 security and P2 CLI desktop app work:
- B0 (implement first): C++11 fluent module function-registration shim,
  `plugin.json` as unique identity, compiled DLL/SO registration and
  *legacy NATIVEWEB_PLUGIN compatibility* regressions.
- B1/B2 (independent core/parser/ABI): full plugin catalog/manifest
  validation, lazy native load, C++ fluent class constructor/method/
  property and unloaded-object/lease tests.
- B3 (.NET/Python): developer attribute/decorator discovery and
  managed-language host adapters; complete real language E2E, not just
  reflection/introspection tests.
- B4 (browser): static ESM proxy generation and language-neutral async
  JS calls tested on CEF Linux/Windows and WebView2 Windows.
- B5 (app): CLI restore/run/publish and packaged desktop shell.
Finish each slice with CI evidence; log blockers and continue separate
workstreams without granting unsafe browser APIs.

## A0 existing verified foundation

- Linux/Windows C++11 Core Regression: CI 38322632990 PASS.
- Linux CEF, Windows CEF, Windows WebView2 JS→plugin object E2E:
  CI 38322633033 PASS.
- Installed relocatable **Core-only** C++11 and C ABI consumer:
  CI 38322633018 PASS.
- Existing C++→JS `execute()`/Core events tests are **not** equivalent to
  full plugin-owned callback/event ABI coverage.

## Workstreams (independent tracks may run concurrently)

| Track | Planned work | Mandatory test gate | State |
| --- | --- | --- | --- |
| P0 Secure bridge | Trusted packaged UI; host-supplied frame/origin identity; per-method grants; navigation invalidation | A1-SEC-01–07 on 3 real engines | PARTIAL: exact entry URL native gate; 3-engine positive E2E, negative security pending NW-014 |
| P1 Bidirectional plugin callbacks | Plugin→JS named invoke/event, async results, event subscription/unsubscribe, callback lifetimes across reload/destroy/unload | A1-CB-01–05 | NOT STARTED |
| P2 CLI and new-app workflow | xytron new/restore/run/build/test/publish/doctor + plugin new/build, versioned manifest, SDK download/cache | A1-CLI-01–05 | PARTIAL: new desktop cpp/python/csharp, module add, plugin new+compiled DLL/SO; runtime commands pending |
| P3 Standalone shell and embedded controls | Native app window/menu/context and minimum Win32, GTK3, WPF, WinForms, MFC controls | A1-SHELL-01–05 and A1-HOST-01–06 | NOT STARTED |
| P4 Actual backend profiles and wrappers | Python & C# authored backend templates, managed runtime lifecycle, async/stream/error/callback plus wrappers | A1-BACK-01–04 and A1-WRAP-01–03 | NOT STARTED |
| P5 Engine packaging | Portable SDK artifacts, CEF resources/ICU, WebView2 Evergreen diagnostics, third-party notices | A1-DIST-01–04 | CORE-ONLY BASELINE |
| P6 Release validation | Both stories from fresh machines, negative security, unload stress, reproducibility | All required release matrix cases | NOT STARTED |
| P7 Approval | Versioned deliverables / changelog / draft PR review | Explicit authorization to merge/publish | REQUIRES APPROVAL |

## Dependency-aware order

1. Start **secure native dispatch policy** and minimal **CLI manifest/
   command skeleton** in separate changes. Define failing security tests.
2. Implement **OS-native app/context menu shell** and **plugin→JS callback/event vertical slice** and C ABI lifetime
   rules without weakening unload/worker guarantees.
3. In parallel, implement minimal WPF/WinForms/MFC/Win32/GTK3 controls
   and actual Python/C# app backend lifecycle templates and wrapper proofs.
4. Connect CLI restore/build/run to installable engine SDK packaging and
   plugin fixture; test both new-project and existing-host paths.
5. Gate release using actual Windows/Linux host/browser/plugin/CLI/wrapper
   CI, clean consumer and negative security tests, not only Core CTest.
6. Deliver a release candidate for approval; do not merge/publish unapproved.

CLI usability is a **core product requirement**, not a late optional add-on.
Extended CLI ecosystems/templates and full wrapper API parity remain future
milestones, but their *minimum working vertical slices* are release gates.

## Operating policy

- On failure, log reproduction, observed/expected, environment, impact,
  severity, and fix/CI evidence in `ISSUE_LOG.md`; keep independent tasks
  progressing.
- Follow: implement → focused regression → real browser/host/CLI smoke →
  full affected CI matrix → issue close with run URLs.
- Never claim mock/fixture-only success as real end-user functionality.
- Keep Draft PR #7, do not modify `main` or publish without approval.
- Request approval only for material breaking ABI/API, unsafe permission
  changes, material scope or expense, and final merge/release.

## CI checkpoint at commit 7e45e72b

Three real browser E2E jobs PASS: 38336744516. Core Ubuntu/Windows
PASS: 38336744611. CLI bootstrap plus generated DLL/SO and mixed
Module scaffolds PASS: 38336744554. Installed Core consumer PASS:
38336744625. Distinguish **unit/positive smoke** from unmet full
negative-security, real language-host, desktop shell and distribution
gates. NW-014 and NW-015 stay open in ISSUE_LOG.md.

## 2026-10-13 workstream follow-up

1. Security (P0): `022c519e` added real negative untrusted **child
   local-file iframe** JS→native side-effect E2E, plus CEF/WebView2
   untrusted renderer response-envelope filtering. `7bccf114` prevents
   returning a child WebView2 denial to the top-level document;
   `4ff17af2` requires iframe-ready. One all-engine negative run
   [38342611291](https://github.com/nssang00/chatgpt-write-test/actions/runs/38342611291)
   passed, but NW-014 is NOT resolved. Next: full method/capability
   policy, canonical trusted asset roots, same-URL frame and navigation,
   negative iframe/remote/method/object tests across three engines.
2. CLI (parallel): `xytron plugin build ID --sdk PREFIX` now invokes
   CMake and **actually builds** DLL/SO, run 38343029145 PASSED both
   OS; invalid path test fixed as NW-021. Next: SDK restore/download,
   generated standalone host/run, package/publish (required user gate).
3. Module/Backend (parallel): Core Module Registry→BridgeRuntime exists;
   next wire real C++ Plugin callbacks and Python/.NET out-of-process
   language hosts, return typed errors and authenticated permissions
   for all module-to-module traffic.
4. All changes remain on `nativeweb/autonomous-integration`,
   PR #7 Draft; no `main` merge or release without explicit approval.

## 2026-10-13 next verified incremental slices

- **Security:** Same exact trusted entry reloaded as nested iframe,
  real native checkpoint call attempted and blocked across all three
  browsers [38354623946](https://github.com/nssang00/chatgpt-write-test/actions/runs/38354623946).
  Next priority remains **app-scoped grant-based native dispatch**, verified
  origin/frame/navigation identity, object method permissions,
  controlled reload/redirect behavior and real negative E2E.
- **Developer CLI:** `xytron doctor --sdk PREFIX` implemented and
  Windows/Ubuntu verified [38355060435](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060435).
  Use it as an independent prerequisite diagnostic for SDK+plugin build,
  not as proof of final engine-inclusive runtime package. Next:
  reproducible `restore`, actual runnable standalone host shell,
  published distribution, Python/C# hosting.
- **Isolation:** Browser E2E, Core C++11 and CLI Windows/Linux CI
  independently continue. NW-026/NW-027 were CI launch-script
  defects, not reasons to suspend browser regression work.

## 2026-10-14 binding API implementation checkpoint

**B0 C++11 free-function authoring PASS:** new public header
`include/xytron/plugin.hpp`, compile-time ID/version read from
`plugin.json`, old C ABI v1 preserved. Core and CLI Windows/Linux
run 38481436246 / 38481853765 passed real plugin load/invoke/error.
The generated plugin CMake needs 3.19+ JSON parser; NativeWeb Core
remains CMake 3.16+/C++11.

**B3 local registration discovery PASS (not managed language host):**
Python `@xytron.export` and C# `[Export]` reflection metadata,
Windows/Linux run 38481659399. No actual Python/.NET subprocess
hosting, JS transport, callback or UI engine E2E yet.

**Independent next slices:** B1 catalog/manifest strict validation and
lazy plugin loading; B2 C++ class_/constructor/method/property on stable
object ABI; B3 Python/.NET supervised host RPC; B4 static ESM exports;
NW-014 full method-level native permission enforcement. Test each
slice separately; fail-safe launcher behavior and binary leases first.

## 2026-10-14 URL protocol workstream

- **U0** implemented: `AppResourceBundle` C++11 resource catalog,
  canonical `app://host/path` parser, explicit payload/MIME and
  negative unit tests. No implicit file:// disk fallback.
- **U1** independently build CEF custom scheme registration in all
  processes plus factory; test actual file-less app:// loaded DOM.
- **U2** independently build WebView2 Environment custom app scheme +
  request handler, test same app:// DOM and fetch behavior.
- **U3** require matching origin/frame identity, object/method grants
  (NW-014), actual 3-engine negative tests; only then default CLI
  standalone desktop UI to app:// and design user-defined schemes.
- Preserve parallel B0/B1 C++ Plugin/language-host workstreams;
  record engine SDK failures without changing main.

### U0/U1/U2 source verification checkpoint (2026-10-14)

- U0 Core, U1 CEF and U2 WebView2 **implemented and cross-platform E2E verified** at executable commit `d540df4a`; browser #38525221064 3/3 PASS. Old file:/// plugin tests included.
- Browser semantic coverage: packaged HTML, relative JS/CSS/SVG, proper MIME and 404, same-origin fetch and app://desktop→app://camera cross-origin read denial.
- CLI `new desktop` now emits `app://desktop/index.html` as a *manifest field*, but cannot run/publish it.
- Next parallel: U3 NW-014 P0 per-method+frame native grants and navigation invalidation; Core+engine origin/mount permission; CLI restore/run/publish; explicit file:// picker and custom scheme declarations pre-startup. No release claims yet.

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
