# NativeWeb issue log (2026-10-13)

Issues should be appended, not silently discarded. A blocked task does not block independent work.

## NW-001 Release-only plugin regression failure — FIXED / IN GITHUB
- Priority: P1. Scope: `plugin.object_factory`, `plugin.webview_facade`.
- Reproduction: configure and build the browser-E2E ZIP in CMake Release with tests ON, then run `ctest --output-on-failure`.
- Failure: 19/21 pass; both failing tests throw `Plugin has live native objects or object calls`.
- Cause: `assert(...)` contained side-effecting `runtime.release` calls. `NDEBUG` removed the release entirely. In addition, `assert(waitUntil(...))` in `plugin.async_lifetime` skipped race synchronization.
- Local fix: shared `test_require.hpp` and unconditional `NW_REQUIRE(...)` checks in all three plugin regression sources. Retest: Linux GCC 14.2, Release 21/21 pass, Debug 21/21 pass; 3 targeted plugin tests repeated 30 times each after source import (90 repetitions).
- GitHub source import 377a5444; Linux and Windows Core Regression run 38314597138 PASS. No release gate blocked by this defect.

## NW-002 Browser E2E completion — PARTIAL, CEF PENDING
- Priority: P1 release gate. The Linux workspace has no extracted CEF SDK, and it has no Windows WebView2 runtime.
- Windows WebView2 actual-browser plugin E2E PASS on workflow 38314597431 and 38314779373. Linux CEF/Windows CEF still require successful actual engine tests.
- Next: inspect Linux/Windows CEF results from rerun 38314779373; preserve job logs and repair failures.

## NW-003 GitHub branch behind ZIP — RESOLVED
- Priority: P1. `main` at `43639459c97044feed1d2f8020979f589c99ef96`; latest browser-E2E ZIP contains uncommitted plugin changes.
- `.github/workflows/browser-plugin-e2e.yml` absent from `main`.
- Imported 33 changed/new files in 377a5444 plus 5 originally omitted baseline files in 5b8e75b2. All 125 ZIP paths exist on PR branch; 122 blob hashes match exactly. Three deliberate exceptions are PR-owned execution/issue/acceptance docs, kept at their newer revision. `main` remains unchanged; do not merge without approval.

## NW-004 Container GitHub clone DNS failure — WORKAROUND
- Priority: P2 infrastructure. `git clone https://github.com/nssang00/chatgpt-write-test.git` failed: `Could not resolve host: github.com`.
- The connected GitHub integration can read/write branches and files. Local ZIP builds are not affected.
- Next: use a permitted source-sync transport or an environment with Git network access; continue independent tests.

## NW-005 Installed Core SDK packaging — PASS (CORE ONLY)
- Priority: P2 independent workstream; does not unblock NW-002 or NW-003.
- Source: CMake install/export, native C++11 consumer, Linux and Windows GitHub Actions in PR #7.
- Local evidence (latest ZIP): Linux Release 21/21, install success, installed consumer 1/1, relocated-prefix consumer 1/1.
- On full plugin-integrated branch commit 377a5444: GitHub installed SDK consumer workflow 38314597217 PASSED Linux/Windows; Core Regression 38314597138 PASSED Linux/Windows. Separate engine distribution packaging remains out of scope and unverified.
- Next: separately define/install CEF/WebView2 distribution/package and security policy before release.

## NW-006 First browser CI lacked pinned CEF bootstrap scripts — FIXED, RERUN IN PROGRESS
- Priority: P1 (test infrastructure). Run 38314597431 Linux CEF and Windows CEF failed at bootstrap, before compile/browser execution, because `tools/bootstrap_linux.sh` and `tools/bootstrap_windows.ps1` were omitted from initial GitHub source import.
- Fix: restored both scripts, pin.env, probe_linux.sh, SNAPSHOT_SHA.txt in commit 5b8e75b2. Source-list and blob parity verified.
- Windows WebView2 passed independent of CEF bootstrap. Follow-up run 38314779373 started; both CEF jobs in progress at this checkpoint.
- Do not label CEF E2E passed until successful job/checkpoint evidence is available.

## NW-007 Linux CEF ICU startup crash — FIX IMPLEMENTED, BROWSER RETEST PENDING
- Priority: P1 runtime E2E. Runner 38314779373 compiled Linux CEF and plugin fixture successfully but the actual browser test exited 133 at startup: `Invalid file descriptor to ICU data received` (`base/i18n/icu_util.cc:232`); none of the four JS plugin checkpoints were reached.
- Comparison: previously passing CEF bridge CI starts the executable with its runtime bin directory as cwd and passes `--no-sandbox`. The new Python E2E runner omitted both.
- Hypothesis: different Chromium subprocess launch context caused ICU resource/FD handling failure; not confirmed until real-browser rerun.
- Fix in 0af8c271: pass `--no-sandbox`, launch from the CEF runtime bin directory. Python syntax passed; local fake executable smoke confirmed cwd, flag, and checkpoint parsing. Fake executable is NOT evidence of real browser functionality.
- Verification: actual SDK-backed browser rerun 38315301893 is in progress. If it still fails, collect the CEF job logs and investigate startup/ICU resource descriptors further. Do not mark PASS without job evidence.

## NW-008 Windows CEF SDK extraction stalls in tar.exe — FIX SUBMITTED / CI PENDING
- Priority: P1; Windows CEF actual-browser E2E blocked, while Linux CEF and Windows WebView2 succeeded.
- Runs 38315301893, 38315497991, 38315691707: pinned 144 MB archive downloaded and SHA1 ce9d951914e79f179e224f3ba9d55d9363696efe verified; tar.exe -xjf then stalled until 45-minute job timeout before build.
- Change: split bzip2 and tar extraction using installed 7-Zip; keep SHA1 checks, add offline fixture and a per-step 20-minute timeout.
- Pending evidence: fixture, archive extraction, Windows CEF compile, and real JS lifecycle E2E must all pass. Fixture success alone is not a browser pass.

## NW-009 Windows CEF CI concurrent runtime-file copy — FIX SUBMITTED / E2E PENDING
- Priority: P1; occurred after NW-008 extraction succeeded.
- Run 38321964829 Windows CEF compiled/linked two integration executables in parallel. Both CMake COPY_FILES post-build rules copy the same Resources/locales folder to build/cef/bin; cmake -E copy_directory on nativeweb_cef_multi_webview_integration returned exit code 1 (MSB3073).
- Root cause inference: concurrent post-build copies to the shared bin directory; source distribution layout was verified and bridge executable linked successfully.
- Fix: browser plugin E2E job explicitly builds only nativeweb_cef_bridge_integration and nativeweb_plugin_browser_fixture, sequentially. Multi-WebView executable is unrelated to this E2E; its separate existing CI remains unchanged.
- Next: rerun real Windows CEF browser and verify all JS checkpoints; full parallel-target build race remains worth a separate CMake fix if all-target builds are required.

## NW-009 Underlying CMake runtime staging race — FIX SUBMITTED / FULL BUILD CI PENDING
- In tests/integration/cef/CMakeLists.txt, primary bridge is sole owner of COPY_FILES and multi-WebView depends on it for resource ordering.
- Windows E2E now runs a full parallel CEF integration build (both executables + plugin fixture) to exercise the corrected dependency graph.
- Only a passing full Windows CEF compile and real browser JS E2E can close NW-009.

## 2026-10-13 closure checkpoint — VERIFIED
- **NW-002** Real-browser plugin E2E: RESOLVED for Linux CEF, Windows CEF, Windows WebView2 in GitHub Actions run 38322633033 (source b2f1ced3). This covers the defined plugin-object scenario, not the entire future browser test matrix.
- **NW-007** Linux CEF ICU startup context: RESOLVED; actual browser E2E PASS in run 38322633033.
- **NW-008** Windows CEF tar extraction timeout: RESOLVED; 7-Zip archive fixture, pinned archive extraction, configure, build, and E2E PASS in run 38322633033.
- **NW-009** Shared Windows CEF runtime copy race: RESOLVED; full parallel CEF all-target build and browser E2E PASS in run 38322633033 after making multi-WebView depend on the sole runtime-copy owning bridge target.
- C++11 Core Regression run 38322632990, Installed SDK Consumer run 38322633018, Platform Smoke run 38322632977 all SUCCESS on the same commit.
- Remaining independent release blockers: default-deny security/capability policy, full engine runtime packaging and other product roadmap milestones. Do not mark the whole product done.

## NW-010 Historical docs target first release inconsistently — FIXED (planning only)
- Severity: P2 documentation/coordination; no executable behavior change.
- Found: README/HANDOFF/ROADMAP sections still prioritized previously completed plugin-object tasks and Core-only deliverables, whereas owner approved **Scope A** on 2026-10-13.
- Resolution: create release scope and testcase matrix; replace execution/acceptance gates; update roadmap header, README, decisions and handoff. Existing test evidence preserved, not reclassified as host/security PASS.
- Next: implement security A1-SEC tests first. Verify doc-only commit in Draft PR #7; code CI need not be interpreted as new functional validation.

## NW-011 Product positioning lost first-class platform requirements — FIXED (docs only)
- Severity: P1 planning gap, no runtime incident.
- Owner clarified product identity on 2026-10-13: Electron/Tauri-category
  desktop app platform with C/C++ native backend, first-class Python/C#
  wrappers, bidirectional C++ plugin JS bridge, and .NET-like xweb CLI for
  dependency download/new/build/run/test/publish.
- Former Scope A excluded CLI and deferred callback/plugin events, wrappers
  and greenfield apps; this contradicted the actual product target.
- Resolution: docs/PRODUCT_VISION.md and D-033–D-036, Scope A, release
  matrix, execution plan, roadmap, README/HANDOFF/architecture updated.
- Implementation state unchanged: CLI, wrappers and full plugin callback
  gates are **NOT IMPLEMENTED**; no CI PASS is claimed for the doc change.
- Next: develop security + CLI skeleton independently; add failing plugin
  callback tests before implementing bridge/ABI changes.

## NW-012 Product requirements corrected: OS shell/menu, WPF/WinForms/MFC and backend language profiles — SPECS UPDATED
- Priority: P1 product-definition gap, **no executable defect**.
- Owner confirmed 2026-10-13: NativeWeb must provide both Electron-like
  standalone native desktop app with actual menu/context menu control and
  WebView2-like WPF/WinForms/MFC embedded controls. Python and C# are
  full user-authored app backend profiles (AI/LLM and .NET legacy), not
  merely wrappers; C++ plugin creation and JS reverse callbacks must be easy.
  User-facing CLI target is `xytron` rather than prior working name `xweb`.
- Earlier Scope A deferred these UI controls/menus and modeled Python/C#
  mainly as wrappers; previous PASS evidence covers Core/plugin browser
  basics but not these requirements.
- Resolution: authoritative contract `docs/PRODUCT_MODES_AND_BACKENDS.md`;
  revised scope, plan, test matrix, acceptance, roadmap, decisions and handoff.
- Implementation status: NOT IMPLEMENTED/UNVERIFIED for the new shell/menu,
  language backend, CLI and embedded-control gates. Do not infer test PASS
  from this documentation commit. Continue independent work.

## NW-013 Main/Module/Plugin naming and scope ambiguity — SPEC RESOLVED
- P2 architecture clarity. Owner accepted: exactly one logical Main Backend
  plus feature Modules; Plugin is an independently deployable Module type.
  Any Module can use cpp/python/csharp concurrently and all JS calls share
  a common contract. The Core is not a privileged Main module.
- Recorded in D-041/D-042 and docs/MODULE_ARCHITECTURE.md with regression
  cases A1-MOD-01..09/A1-CLI-06.
- Full multi-language dispatch/callback E2E remains NOT IMPLEMENTED.
  No claim of success until actual language hosts run in CI.
- Next parallel tracks: internal registry tests, CLI scaffold tests,
  native security design/negative tests. Document any blocker without
  stopping unrelated tracks.

## NW-014 Native security policy foundation lacks browser dispatch integration — OPEN / P0 RELEASE BLOCKER
- Scope: WebView JS → C++ call authorization for Linux CEF, Windows CEF and Windows WebView2.
- Observation: `NativePermissionPolicy` in 27bf2489 implements exact-method, verified-frame, navigation-scoped **Core decisions only**. On the same SHA Core CI 38334609344 passes Windows/Linux.
- Impact: **do not claim origin security**; actual CEF/WebView2 bridge has not been connected to host-verified identity. A malicious/untrusted document potentially reaches the native bridge despite a correct isolated policy.
- Prerequisite: inspect the real IPC receiver and navigation/frame events; never allow JS to forge `engineVerified`, origin or frame identity.
- Repro/tests required: A1-SEC-01..07 on actual browsers (trusted local, untrusted remote/file, nested frames, redirects/reloads, raw transport forgery, exact method grants); include plugin object methods and backend-to-backend permissions.
- Status: OPEN. Independent CLI/Module Registry work has continued. No security waiver or release requested.

## 2026-10-13 partial implementation checkpoint — VERIFIED SLICES
- Module Registry C++11 Core metadata + lifecycle: `bf67b567`; Core Regression 38334300674 Linux and Windows PASS.
- Native xytron CLI app/plugin scaffolds: `091a57f4`; CLI 38334300772 PASS both OS. Real generated C++ DLL/SO compilation against installed SDK: CLI 38334470209 PASS both OS.
- Exact-method and trusted-frame policy unit contract: `27bf2489`; Core Regression 38334609344 PASS both OS. Still NOT connected to real browser native-dispatch pipeline; NW-014 remains OPEN.
- Full Module Dispatcher, cross-language backend runtime, app menus, controls, real xytron run/publish remain NOT IMPLEMENTED and keep their existing release gates.

## NW-014 partial browser bridge gate — IMPLEMENTED / THREE-ENGINE CI PENDING
- The first native request boundary now compares an engine-supplied sender
  document URL to the exact local file entry explicitly supplied at create().
  CEF also requires the originating frame to be the main frame. WebView2
  compares event-arg Source to the top-level WebView Source.
- Denies remote URLs, arbitrary other local files, non-main CEF frames,
  changes to a different URL and requests for untrusted entry schemes.
  Returns a structured permission_denied JS response for denied requests.
- This is not yet the final release authorization layer: WebView2 same-URL
  iframe identity requires explicit frame-level verification, local file URL
  canonicalization/symlinks need an app-root policy, and module/method grants
  plus native object calls must route through NativePermissionPolicy.
- Trust gate is a strict compatibility change for applications that used to
  invoke native code on remote sources or navigated to other pages. This is
  intentional default-deny as approved, but explicit opt-in capability must
  be implemented before product release. CI success must be verified.

## NW-015 xytron module add only edits bootstrap canonical manifest — OPEN / P2
- This implementation supports inserting feature Modules of cpp/python/csharp
  into CLI-generated `xytron.json` with one Main. It rejects unknown manifest
  layouts instead of silently changing user-owned files.
- Limitation: no full JSON schema parser, transactional filesystem update,
  production backend start/stop or module-to-module transport.
- CLI regression exercises 3-language manifest, duplicate names, unsafe IDs
  and absence of a project; keep release gates A1-MOD-07 and A1-CLI-06 OPEN
  until actual mixed-language JS E2E.

## NW-016 Xytron module-add CMake regression script parse typo — FIX SUBMITTED, CI PENDING
- CLI workflow run 38336055684, Ubuntu job fails `Scaffold regression`
  before invoking the CLI: `tools/cli/test_xytron.cmake:70` lacked a
  closing double quote in `file(READ "${TEST_ROOT}/ai_app/xytron.json" expanded)`.
- Fix: restore closing quote and rerun independent Ubuntu/Windows CLI CI.
- Scope: CLI smoke test script only; Core and actual browser E2E are not
  blocked. No success claim until rerun actually passes.

## NW-017 Module Registry → BridgeRuntime qualified-call dispatch — IMPLEMENTED / CI PENDING
- Internal `BridgeRuntime::registerModule` and `bindModuleMethod` bind
  app-scoped cpp/python/csharp Module descriptors to the same JS request
  envelope method namespace (e.g. camera.capture). This is a first
  language-neutral dispatch **hook**, not live Python/.NET execution.
- `ModuleRegistry::generation(id)` guarantees a stale function handler
  cannot be invoked when its Module was removed and a replacement
  with the same ID re-registers. `core.module_bridge` exercises calls,
  duplicate handling, removal, replacement, shutdown.
- Important: this does not yet lease actual dynamically loaded code during
  an in-flight callback. Plugins need explicit shared-library leases and
  cancellation/worker lifetime handling before connecting unloadable ABI
  method handlers. Real JS/browser, Python/.NET hosts and native
  authorization integration remain pending.

## NW-018 Separate CEF/WebView2 static Core integration misses new ModuleRegistry TU — FIX SUBMITTED
- First browser E2E regression after Module→BridgeRuntime routing addition:
  Windows WebView2 CI run 38336528363 fails LNK2019 unresolved
  `ModuleRegistry::ModuleRegistry`, `registerModule`, `generation`,
  `clear` when linking bridge and multi-webview executables.
- Core Regression run 38336528298 and CLI run 38336528323 passed Linux/Windows;
  cause is source-list divergence, NOT native ABI break.
- Fix: add `src/core/module_registry.cpp` to both independent static core
  targets in `tests/integration/webview2/CMakeLists.txt` and
  `tests/integration/cef/CMakeLists.txt`.
- Verify all three actual browsers again before declaring resolved.
  Follow-up: centralize reusable Core source lists to avoid drift.

## 2026-10-13 VERIFIED implementation milestone — code 7e45e72b

- **Three actual browser engines:** Linux CEF, Windows CEF (full parallel
  build) and Windows WebView2 E2E all PASS at
  [browser run 38336744516](https://github.com/nssang00/chatgpt-write-test/actions/runs/38336744516).
  The new engine-sourced local-entry guard **did not regress** the previously
  verified JS→C++ plugin lifecycle path.
- **Core Regression:** 26/26 cases on Ubuntu and Windows including
  `core.module_bridge`, `core.trusted_document`,
  `core.native_permission_policy`, `core.module_registry`;
  [run 38336744611](https://github.com/nssang00/chatgpt-write-test/actions/runs/38336744611)
  PASS both OS.
- **xytron native CLI:** cpp/python/csharp app scaffolds, independent C++
  plugin DLL/SO build from installed SDK, Python Main + C++/Python/C#
  feature Module scaffolds, duplicate/unsafe-input tests;
  [run 38336744554](https://github.com/nssang00/chatgpt-write-test/actions/runs/38336744554)
  PASS on Windows and Ubuntu.
- **Installed Core consumer:** [run 38336744625](https://github.com/nssang00/chatgpt-write-test/actions/runs/38336744625)
  PASS both OS (Core-only, not browser-distribution release).
- **NW-016 RESOLVED:** missing CMake string quote in a CLI test fixed
  by 3c1bac5d; CLI run 38336144862 and 38336744554 PASS both OS.
- **NW-018 RESOLVED:** engine Core static targets now include
  module_registry.cpp (7e45e72b); all three real browser E2E jobs PASS
  in 38336744516.
- **NW-014 STILL OPEN / P0 RELEASE BLOCKER:** exact initial-file URL
  check has 3-engine *positive* regression evidence and pure Core
  denied-URL unit tests. This does **not** prove untrusted-frame/remote
  negative E2E, complete method/capability grants, local root/symlink
  canonicalization or safe native object method access. Explicit per-app
  grants, cross-engine negative E2E and iframe-origin proof required.
- **NW-015 STILL OPEN / P2:** CLI module-add manifest editor has
  limited canonical schema and nontransactional writes.
- **NW-017 PARTIALLY VERIFIED:** language-neutral in-process Module
  handlers route through BridgeRuntime and stale generation is blocked
  in Core tests, but actual Python/.NET processes, cross-language
  callbacks and unload-safe plugin code leases remain unimplemented.

This evidence is about the **implemented technical slices**, not
NativeWeb product-release completion. CLI run/restore/publish, shell/menu,
WPF/MFC/.NET host controls, Python/C# real backend and Plugin→JS
callbacks still have unmet release gates. Keep PR #7 Draft, main intact.

## NW-019 Untrusted renderer result forgery and child frame negative regression — CI PENDING
- Earlier local-document gate checked JS->native Requests but forwarded CEF
  `nativeweb.call.response` and WebView2 `response/error/event` envelopes
  from any renderer document. This permitted an untrusted sender to inject a
  reply toward a Core pending native->JS call (subject to guessing IDs).
- Fix: gate **all incoming renderer envelope types** using engine-owned
  sender metadata before forwarding to `BridgeRuntime`. For CEF, route
  `permission_denied` to the actual child CefFrame rather than its parent.
- Real browser E2E now loads an independent sibling local file in a child
  frame and attempts a side-effectful native `test.checkpoint`. It must not
  appear in stdout; a trusted parent must confirm the negative checkpoint.
- IMPORTANT: a timeout/no bridge in the WebView2 child is not proof of a
  structured permission_denied response; frame-specific backend event
  support is a separate remaining security test. This is **one negative
  test case**, not blanket closure of NW-014.

## NW-020 Native plugin build subcommand with installed SDK — IMPLEMENTED / CI PENDING
- `xytron plugin build ID --sdk PREFIX` invokes CMake on the CLI-generated
  plugin project to produce a real DLL/SO; validates names and SDK prefix
  characters before launching the command.
- Workflow `xytron-cli.yml` additionally exercises the CLI subcommand on
  Windows/Linux using the freshly installed Core SDK.
- SDK restore/download, signing, language host, app packaging and
  untrusted third-party plugin build sandboxing remain unimplemented.

## NW-021 Plugin build invalid-SDK test argument split by CMake — FIX SUBMITTED
- CLI run 38342700294 failed Ubuntu + Windows during `cli.scaffold`.
  Test supplied `invalid;echo PWN` and CMake expanded `;...` into two
  positional arguments, causing CLI usage exit 2 instead of the expected
  invalid-path rejection exit 1. The CLI source compiled on both systems.
- Test fixture changed to `invalid&echo` (still forbidden shell character
  but no CMake list splitting). CI rerun required before closing issue.

## NW-022 WebView2 subframe error response targeted top-level document — FIX SUBMITTED
- WebView2 engine's `PostWebMessageAsJson` sends its response to the main
  document, not to arbitrary nested frames. Returning permission_denied
  for an untrusted child request using that API could collide with a
  legitimate main-document pending Promise's numeric request ID.
- Restrict denial responses to untrusted *top-level sender* requests
  (`senderUrl == mainUrl`). Drop child requests without forwarding or
  replying to another document. Actual browser iframe side-effect test
  already treats missing-frame-response as safe (no native invocation).
- Still requires WebView2 frame-owned messaging/capability machinery for
  deterministic per-frame error responses and same-URL iframe isolation.

## NW-019 negative iframe E2E fixture strengthening — CI PENDING
- Child document sends a separate `security-probe-ready` handshake BEFORE
  attempting its native request. The host parent rejects missing ready, so a
  missing local file or malformed iframe cannot make a 7-second timeout count
  as a passing denied-native-call test.
- Runner still rejects any actual `security-UNTRUSTED-CALL-EXECUTED`
  native-side checkpoint. No-bridge/no-response only passes for an iframe
  whose separate document demonstrably executed.

## 2026-10-13 validation rollup — security and CLI

- **NW-019 PARTIAL VERIFIED**: real native request/response sender gating
  and side-effectful wrong-file iframe test, all 3 engines CI
  38342611291 PASS on source 022c519e. New ready-handshake source
  4ff17af2 requires an actually loaded child; CI 38343177515 tracks.
  Remaining: frame-owned replies, same-URL frames, page navigation and
  deliberate forged response tests. **NW-014 remains OPEN/P0.**
- **NW-020 VERIFIED for installed-SDK plugin build only**: native
  `xytron plugin build` executed with real generated DLL/SO on
  Windows/Linux CI 38343029145 PASS. SDK restore/run/publish not done.
- **NW-021 RESOLVED**: CLI security-test argument corrected; independent
  Windows/Linux CLI regression and real plugin build pass, run 38343029145.
- **NW-022 code implemented, negative E2E rerun pending**: WebView2
  child-frame denial not posted to the main page's pending Promise;
  38343177515 tracks stronger browser test.

## 2026-10-13 final verification: latest hardened fixture + WebView2 child denial

**Executable source `4ff17af2fb6d1d1e2752acec7e25ea7d52f35f5d`**
- [Real browser #38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515): Linux CEF, Windows CEF, Windows WebView2 **ALL SUCCESS**. This specifically includes the independent iframe ready handshake, attempted native side effect and forbidden-checkpoint absence, plus legacy C++ Plugin E2E.
- [Core #38343176859](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176859): Ubuntu/Windows **PASS**, Module Bridge/Registry/trusted document methods.
- [CLI #38343176992](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176992): Ubuntu/Windows **PASS**, real `xytron plugin build` compiles generated DLL/SO with installed SDK.
- [Installed SDK #38343176874](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343176874): Ubuntu/Windows **PASS** (Core consumer only).
- NW-019 first negative actual browser side-effect case: VERIFIED for *different-file child iframe*. NW-022 wrong-frame reply hardening: VERIFIED against same E2E and Win WebView2. NW-020 actual plugin build VERIFIED, NW-021 test argument fix CLOSED.
- **NW-014 P0 security BLOCKER STILL OPEN**: Only a targeted negative test, not all origins, same-source frames, lifecycle and per-method grants. No release waiver or security claim beyond tested scenario.


## NW-023 main branch advanced independently — MONITOR / P2 MERGE READINESS
- Observed 2026-10-13: `main` is now
  `7951ba62625c0deb305fc2f99fc04b0c7f23dd19`, +5 commits since
  previous development base `43639459c97044feed1d2f8020979f589c99ef96`.
  The changes shown in the comparison are M5 CI workflow/docs
  (`.github/M5-CI-README.md`, M5 workflow YAML), unrelated to the
  current NativeWeb plugin/bridge feature scope.
- GitHub compare latest main vs development head reports DIVERGED,
  development branch ahead 44 / behind 5. This assistant has made no
  `main` merge/rebase or publish. PR #7 remains Draft.
- Before final integration, re-evaluate base compatibility/CI and obtain
  explicit owner approval for main merge/release. This is not a blocker
  for independent security, CLI, language-host or shell work.

## NW-024 Same trusted-entry URL reused in nested frame — NEGATIVE E2E PENDING
- A nested iframe loading the **exact same file:/// entry URL** has
  native API methods in the renderer, but is not an authorized top-level
  document. A different-file frame test is insufficient to demonstrate
  correct engine main-frame identity handling.
- Expand real browser Plugin E2E with a second frame probe loading its own
  `plugin_bridge_test.html` URL. The script detects `window !== window.top`,
  reports a ready handshake, tries the real side-effectful native
  checkpoint and reports outcome; original trusted parent also remains
  capable of calling native bridge APIs.
- Each of Linux CEF / Windows CEF / Windows WebView2 must output
  `checkpoint: security-same-entry-iframe-blocked` and NEVER output
  `security-UNTRUSTED-CALL-EXECUTED`. This is one additional negative
  scenario, **not** closure of origin/permission policy NW-014.

## NW-025 Core SDK readiness diagnostics via xytron doctor — CI PENDING
- Implemented `xytron doctor --sdk PREFIX`, failing on missing
  plugin headers, CMake SDK config or missing CMake executable.
- Windows/Linux CLI workflow now checks freshly installed Core SDK
  with `doctor` prior to invoking `xytron plugin build`.
- Deliberately excludes complete browser/managed-runtime publishing
  dependencies; do not interpret as green desktop-platform distribution.

## NW-026 CLI doctor workflow YAML quoting — FIX SUBMITTED / CI PENDING
- On code 8d6f6151 the `xytron-cli.yml` workflow was rejected by GitHub
  before starting jobs: a `run:` scalar began with a double quote but
  contained unquoted trailing command arguments. This is a workflow
  syntax error, not test failure or `doctorSdk` compilation issue.
- Fix: use block-scalar `run: |` for the `xytron doctor --sdk`
  invocation. Rerun both Windows/Linux CLI tests before closing.

## NW-027 PowerShell quoted executable invocation in doctor CI — FIX SUBMITTED
- After NW-026 YAML fix, Ubuntu CLI job PASS, Windows failed
  (run 38354857320) with PowerShell `Unexpected token 'doctor'`
  because quoted exe path on a command line requires special invocation.
- Align with already working Windows plugin-build workflow by launching
  via `cmake -E chdir . PATH_TO_XYTRON doctor --sdk PREFIX`.
  C++ doctor program and installed SDK were compiled/installed normally.
- Rerun Windows/Linux CLI CI before marking doctor verified.

## 2026-10-13 verified same-entry iframe security and SDK doctor milestone

- **Code branch:** `nativeweb/autonomous-integration` on Draft PR #7.
  Same-entry negative browser test `dfaabf0e`, doctor feature
  `8d6f6151`, fixed YAML quoting `913a775e` and PowerShell invocation
  `0c879ae0`.
- **Real same-entry frame security test:** a child loads the **identical
  trusted HTML entry**, proves it executed, attempts `xytron.test.checkpoint`
  and the test rejects any forbidden native side effect. Original
  different-file iframe probe and plugin-object E2E still run.
  [Browser CI 38354623946](https://github.com/nssang00/chatgpt-write-test/actions/runs/38354623946)
  **PASS: Linux CEF / Windows CEF / Windows WebView2.** NW-024 VERIFIED
  for this scenario only; NW-014 P0 still OPEN.
- **Native `xytron doctor --sdk PREFIX`:** checks real installed
  `include/nativeweb/plugin.hpp`, NativeWeb CMake package and `cmake`
  availability. [CLI CI 38355060435](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060435)
  **PASS: Windows and Ubuntu**, including doctor, source scaffolds,
  `xytron plugin build` and actual installed-SDK DLL/SO generation.
  NW-025 VERIFIED for Core SDK only; NW-026 YAML and NW-027 PowerShell
  workflow issues corrected and CI green.
- [Core Regression 38355060575](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060575)
  **PASS Windows + Ubuntu**; [Installed Core consumer
  38355060701](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060701)
  **PASS both OS**.
- **Not complete:** `NativePermissionPolicy` not fully bound to
  browser-origin authentication and exact method authorization.
  Real negative method-denial, remote and navigation tests, object
  capability checks and race/revocation remain open. CLI Core doctor
  does **not** validate or download CEF, WebView2, Python or .NET
  engines/hosts. `xytron run`, `restore`, `publish`, native desktop
  shell/menus and language hosts are not implemented yet.
- PR #7 remains Draft. No main merge/release. Main independently
  advanced earlier (NW-023), and integration needs owner approval.


### NW-024 verification closure
- Same exact local entry URL loaded in nested frame now has 3-engine
  side-effect denial evidence in `38354623946`. Specific case CLOSED.
  NW-014 whole security requirement remains OPEN.
### NW-025 verification closure
- Local Core SDK doctor passed Windows/Ubuntu run `38355060435`.
  **Core-only** diagnostics scope, not full product release readiness.
### NW-026 and NW-027 verification closure
- GitHub workflow YAML scalar and Windows PowerShell executable invocation
  both corrected; same run `38355060435` passes both platforms.

## NW-023 main branch drift updated during independent work (2026-10-13)
- Main now `5e286f965f55548727f87e08bf8186fb2f8df65c`
  (10 extra commits since previously recorded `7951ba62`; comparison
  includes M5 CI workflow and m5-ci disclosure/readme docs).
- Latest main and `nativeweb/autonomous-integration` differ. **This work
  did not merge/rebase/push main.** Check changed workflows and base
  compatibility immediately before any owner-approved merge.
- Current feature E2E and CLI CI are for our development source
  `0c879ae0`; do not assume green CI on newly advanced `main`.

## NW-028 Cross-language registration spec integration — SPEC ACCEPTED / IMPLEMENTATION IN SLICES
- Owner-approved uploaded design: C++ XYTRON_MODULE + fluent functions/
  classes, Python @xytron.export, C# [Export], PluginManager catalog/
  lazy loader, common Module Registry/Invoker, JS language transparency.
- Resolved design divergence: existing C++11 / nativeweb::Any rather than
  literal C++17 std::any sample; Promise-first JS default; ESM static
  export metadata required for named imports; supervised Python/.NET
  hosts by default; PluginManager is packaged-Module manager, not Main.
- Build-supplied plugin ID from plugin.json without repeating it in C++
  macro; preserve NATIVEWEB_PLUGIN v1. Previous CLI module.json scaffolds
  require controlled migration and test rather than silent renaming.
- Implementation workstreams/tests detailed in docs/CROSS_LANGUAGE_BINDING.md
  and A1-BIND-01..11. NW-014 per-method native permission release block
  remains OPEN; registration is not permission.

## NW-029 Initial XYTRON_MODULE C++11 function adapter — SOURCE SUBMITTED, CI PENDING
- New public shim maps module `m.function` registrations to stable
  C ABI v1, loads test DLL/SO with real PluginModule, checks return
  values, errors, module ID and destructor cleanup.
- New CLI plugin template uses plugin.json sole ID, CMake 3.19+ JSON
  extraction to generate binary descriptor without repeating ID in C++.
  Legacy NATIVEWEB_PLUGIN source and ABI v1 intentionally preserved.
- A1-BIND-01 is not VERIFIED until Windows/Linux binary+CLI CI PASS.
  Full class/property fluent API remains A1-BIND-03 blocker.

## NW-029 B0 build-system follow-up — FIX SUBMITTED / CI PENDING
- Install `include/xytron` along with existing `include/nativeweb`;
  otherwise a CLI-generated C++ plugin cannot find its new public shim
  when building against an installed SDK.
- Preserve literal quoted string preprocessor definitions for the
  test fixture plugin ID/version rather than passing bare identifiers.
- Windows/Linux CTest and real installed-SDK CLI binary compilation are
  required to close this initial B0 gate.

## NW-030 Python/C# local registration metadata — IMPLEMENTED / CI PENDING
- Added native language authoring stubs `@xytron.export` and `[Export]`
  with introspection/reflection plus independent Windows/Linux tests.
- The registrations are only discoverable metadata; no real .NET/Python
  managed transport, method invocation from JS, streaming, callback or
  native permission enforcement. Keep A1-BIND-06/07 PARTIAL only.
- C# uses .NET 8; Python local test uses 3.11. Full runtime/manifest,
  argument coercion and object lifetime remain next slices.

## NW-031 CLI-generated plugin.json identity and actual DLL/SO load — CI PENDING
- Added nativeweb_generated_xytron_plugin_regression to load generated
  camera binary via C ABI; verify ID/version, call and cleanup.
- Windows/Linux CLI workflow runs it after building the real DLL/SO.

## 2026-10-14 B0/B3 implementation and validation checkpoint
- **NW-029 C++ free-function registration VERIFIED:** initial CLI
  38481352637 failed to compile after source changed to
  `#include <xytron/plugin.hpp>` because SDK install was missing
  `include/xytron`. Fix committed `ac4008ed`, installation and
  test macro quoting; [Core 38481436246](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481436246)
  and [CLI 38481853765](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481853765)
  PASS Ubuntu/Windows. Generated CLI camera plugin binary is loaded
  against actual PluginModule C ABI and hello method succeeds.
- **NW-030 Python/C# export metadata PARTIAL VERIFIED:**
  [language CI 38481659399](https://github.com/nssang00/chatgpt-write-test/actions/runs/38481659399)
  PASS Ubuntu/Windows for Python decorator and .NET Reflection;
  managed process, JS bridge and callbacks remain NOT IMPLEMENTED.
- **NW-031 generated-plugin metadata ABI VERIFIED:**
  source `c2b4dd5a`, CLI CI 38481853765 Win/Linux PASS.
- **NW-014 P0 still OPEN:** export registration never implies method/
  origin/object capability grant. Same original security release gate.
- **NW-032 NEW P2:** plugin.json entry binary paths (`bin/<rid>/...`)
  are target packaging metadata. `xytron plugin build` currently writes
  normal CMake build-directory binaries, not the declared RID entry;
  publish/install/manifest validation is required before PluginManager
  packaged lazy loading. Do not claim plugin.json entry path is deployed.

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

## NW-033 app:// default app resource scheme — U0 CORE IMPLEMENTED / ENGINE PENDING
- Owner chose packaged `app://` Core default, optional safe file://,
  ordinary http(s), manifest-declared custom-scheme Module handlers.
- `AppResourceBundle` implements byte/MIME catalog and conservative
  canonical URL parsing with no filesystem accesses. Core unit test
  `core.app_resources` rejects traversal and cross-scheme requests.
- **RELEASE BLOCKER:** neither CEF nor WebView2 yet serves this catalog
  via a real app:// scheme. CEF needs all-process scheme registration,
  WebView2 needs environment-level custom scheme registration; native
  trust gate still only accepts exact file:/// entry.
- No claim of default usable app:// until 3-engine positive+negative
  browser E2E passes. NW-014 full method/frame grants remains P0.

## NW-033 U1 CEF per-WebView app scheme — SOURCE SUBMITTED / CI PENDING
- AppResourceBundle passed using `WebViewOptions::appResources`;
  CEF renderer and browser register a standard secure app scheme.
  Per-request-context handler serves only explicit bytes/MIME;
  unknown keys get 404, no disk fallback.
- Exact trusted main `app://` entry now allowed by native bridge
  policy, but app module method permissions NW-014 remain incomplete.
- WebView2 custom app scheme still NOT IMPLEMENTED; do not
  claim cross-engine app:// support before actual 3-engine E2E.
- CEF scheme lifecycle/buffer multiwindow tests are required before
  U1 can be verified.

## NW-033 U1 actual CEF app:// bundle E2E — CI PENDING
- New CEF browser E2E `browser.app_resource_lifecycle` replays
  existing real-plugin method/event/object lifecycle and untrusted child
  frame tests with HTML served from `app://desktop/plugin_bridge_test.html`.
  Host feeds in-memory app resources explicitly, never file:// mapping.
- CEF and WebView2 independent Core source builds now include
  `src/core/app_resources.cpp` to prevent old NW-018 linker drift.
- WebView2 rejects app:// with structured `app_scheme_not_supported`
  instead of attempting an undefined navigation. U2 remains open.
- App bundle entry prevalidation moved before BrowserBackend creation;
  invalid entry fails without leaving an unusable backend pointer.

## NW-034 Core regression trusted_document test missing Core/include dependency — FIX SUBMITTED
- Commit ee1e071c extended trusted_document.hpp to accept canonical app://
  URLs by using the AppResourceBundle policy. The old standalone
  nativeweb_trusted_document_regression CMake target only included src,
  causing nativeweb/app_resources.hpp missing compilation on Win+Ubuntu
  in Core CI 38523543240.
- Fixed its CMake target to link NativeWeb::Core (exports installed header
  include path and AppResourceBundle symbol). Follow-up full test CI
  required before closing. Independent CEF/WebView2 builds continue.

## NW-035 CEF request-context source lacks complete handler header — FIX SUBMITTED
- CEF 144 source in initial U1 build 38523543239 fails in
  cef_scoped_refptr.h with incomplete CefRequestContextHandler when
  CreateContext(settings, nullptr) instantiates a typed CefRefPtr.
- Fix: include cef_request_context_handler.h explicitly. Browser
  UI app:// tests will prove actual custom scheme after recompile.
- WebView2 linker errors in same early commit were source-list drift;
  app_resources.cpp was added to both engine CMake projects in
  3e1bffba, pending verified source tests.

## NW-033 U2 WebView2 app scheme — SOURCE SUBMITTED / 3-ENGINE CI PENDING
- Creates a secure app:// scheme in WebView2 environment options4
  before Environment creation; resource filter serves only explicit
  AppResourceBundle bytes and deny-by-default 404 for unknown paths.
- Adds Windows real browser packaged app:// plugin lifecycle/iframe
  E2E in addition to Linux/Windows CEF.
- Native module per-method grants and plugin/other host isolation
  remain NW-014 open. Do not mark cross-engine URL gate PASS before
  all three actual browser jobs pass.

## NW-036 Additional packaged app:// browser resource semantics — CI PENDING
- Cross-platform browser E2E adds relative JS, CSS, SVG, 404, and
  app://desktop vs app://camera cross-origin private byte denial.
- Same source fixtures remain available as disk files for legacy
  file:/// URL E2E. New host isolation is a required separate gate
  from native Module invocation permissions NW-014.

## NW-037 CLI app:// default UI declared but desktop runner missing — OPEN / P1
- `xytron new desktop` writes ui.entry=app://desktop/index.html and
  ui.resources=ui, pointing to generated ui/index.html for all Main
  backend languages. CLI regression validates the manifest.
- No actual `xytron run/build/publish` or engine-backed desktop shell
  consumes this declaration yet; it remains future distribution work.
- This must NOT be presented as a runnable desktop app scaffold until
  CLI runner, app asset mounting and clean-machine binary tests exist.

## NW-038 WebView2 expanded packaged resource fixture name collision — FIX SUBMITTED
- Browser CI 38524905603 Windows WebView2 failed in test main.cpp:
  `AssetEntry assets[]` shadows existing
  `std::shared_ptr<AppResourceBundle> assets` in the same scope.
  Error C2040/C2088/C2039; no production backend compile error.
- Rename entry descriptor array to `entries[]` while retaining
  the AppResourceBundle pointer. Rerun 3-engine browser suite.

## 2026-10-14 app:// rollout verified — ISSUE RESOLUTION UPDATE

- **NW-033 U0/U1/U2 RESOLVED for in-memory resource serving**: C++11 resource catalog + isolated CEF scheme + true WebView2 custom environment scheme; real [3-engine browser #38525221064](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221064) PASS. Broader packaged app integration U3 pending separately.
- **NW-034 RESOLVED:** trusted_document CTest links Core/include; Core Windows/Linux #38525221058 PASS.
- **NW-035 RESOLVED:** CEF request context incomplete handler header fixed; CEF Linux/Windows browser CI PASS.
- **NW-036 VERIFIED**: browser relative JS, CSS, SVG, MIME, known Fetch, unknown asset 404, app://desktop cannot read private app://camera bytes in [#38525221064](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221064). Additional malicious origin tests still needed.
- **NW-037 PARTIAL / OPEN P1:** CLI `ui.entry` set to app://desktop/index.html and CLI Windows/Linux #38525221109 PASS; actual `xytron run/build/publish` and packaged mounting NOT IMPLEMENTED.
- **NW-038 RESOLVED:** Windows WebView2 test array name collision fixed; real all-3-engine browser suite PASS.
- **NW-014 REMAINS OPEN P0 RELEASE BLOCKER:** native per-method/frame/object grants and full origin proof are NOT completed merely by app:// resource loads.

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
