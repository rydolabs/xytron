# NativeWeb Roadmap — standalone shell + embedded controls + language backends

> **Owner clarification 2026-10-13:** NativeWeb supplies both an Electron-like
> OS app shell (windows, real app menus/context menus) and WebView2-like
> controls for WPF/WinForms/MFC plus Win32/GTK3. The C/C++ Core is shared;
> developers author **cpp/python/csharp backend** logic as equal profiles.
> Canonical CLI user command is `xytron`, including new/restore/run/build/
> test/publish and one-command C++ plugin new/build.
>
> [Product contract](PRODUCT_MODES_AND_BACKENDS.md) ·
> [Scope A](RELEASE_SCOPE_A.md) ·
> [Test matrix](RELEASE_TEST_MATRIX.md)
>
> Main = one logical app-scoped Module; Plugins = independently packaged
> Modules; cpp/python/csharp coexist, not three isolated app types. See
> [Module architecture](MODULE_ARCHITECTURE.md).
>
> Phase 0–10 inventory below is historical; CLI, shell and first language
> backend templates are **first usable release** requirements, not deferred
> until the numeric phase containing them.

이 문서는 우선순위 문서다. 날짜 약속이 아니라 dependency와 개발 순서를 나타낸다.

### 2026-10-16 revised bridge + resource extension plan

Product-facing JS uses `xytron.math.add()`. `xytron.invoke()` is the
compatibility primitive. Core owns transport, permissions and browser lifetime;
C++/Python/C# modules share the same public contract. Read
[bridge transport plan](XYTRON_BRIDGE_TRANSPORT_PLAN_KO.md) before extending
binary framing, custom schemes or plugins. Existing NW-014 P0 document-session
security work is NOT complete merely because event routing exists.


## Phase 0 — Repository foundation

상태: **complete**

- [x] GitHub repository write/read validation
- [x] Linux/Windows C++11 CMake/CTest smoke CI
- [x] Architecture/handoff documentation
- [x] Root project/CMake skeleton
- [x] Canonical user `Any.h` import
- [x] Core regression workflow

완료 기준: 새 세션이 저장소만 보고 개발을 이어갈 수 있고, core tests를 Windows/Linux에서 반복 실행할 수 있다.

## Phase 1 — Core value/async/runtime

- [x] Any/VariantList/VariantDict integration
- [x] structured error model
- [x] binary type
- [x] async/future abstraction
- [x] request IDs
- [x] pending request registry
- [x] object registry
- [x] event primitive
- [x] engine capability skeleton
- [ ] security permission/capability policy

완료 기준: browser 없이 core contracts를 unit test할 수 있다.

## Phase 2 — Public WebView API skeleton

- [x] `nativeweb::WebView` public pImpl shape
- [x] create/destroy declarations
- [x] load/reload declarations
- [x] dynamic bind contract
- [x] C++11 typed bind adapter
- [x] typed execute adapter
- [x] emit declaration
- [x] capabilities
- [x] public WebView Impl -> BrowserBackend orchestration
- [x] engine auto/CEF/WebView2 selection contract
- [x] API compile regression on Windows/Linux
- [x] engine-independent BrowserBackend contract

완료 기준: public API shape가 C++11에서 안정적으로 compile된다.

## Phase 2.5 — Public execution and JavaScript DX contract

- [x] RequestId / pending future baseline
- [x] structured request / response / error / event envelope
- [x] `xytron.foo()` direct JS facade
- [x] `xytron.invoke()` primitive compatibility test
- [x] runtime WorkerPool / TaskQueue
- [x] JS -> C++ default worker execution
- [x] concurrent/out-of-order JS call regression
- [x] remove per-call `std::async` from typed C++ -> JS future conversion
- [x] root/singleton native object binding API baseline
- [x] Binary vs Transfer vs Shared public semantics contract
- [ ] TransferBuffer implementation
- [ ] SharedBuffer implementation

상세 계약: [API_CONTRACT.md](API_CONTRACT.md)

## Phase 2.6 — Callable metadata foundation

Plugin SDK와 TypeScript generation 전에 공통 callable metadata layer를 만든다.

- [x] `Callable` type-erasure
- [x] `SignatureMetadata` (argument count/types + result type) baseline
- [x] existing free function/lambda bind parity
- [x] member + const-member parity
- [x] void / exception parity
- [ ] explicit overload-selection escape hatch
- [x] current `bind()` public API unchanged
- [x] root object `.method()` public API unchanged
- [x] generation-safe `RegistrationToken` / registration ownership baseline

상세 설계: [CALLABLE_PLUGIN_DESIGN.md](CALLABLE_PLUGIN_DESIGN.md)

## Phase 3 — Linux CEF backend

Selected CEF:

```text
144.0.36+g78619fd+chromium-144.0.7559.264
```

- [x] CEF package acquisition/install strategy
- [x] multi-process bootstrap
- [x] BrowserBackend implementation
- [x] renderer bridge injection
- [x] request/response transport
- [x] Any <-> CEF value conversion
- [x] events
- [x] reload reconnect
- [x] binary
- [x] multi-WebView
- [x] real-browser destroy while C++->JS Promise is pending
- [x] native object handle / JS Proxy vertical slice
- [x] C++ -> JS synchronous call
- [x] C++ -> JS Promise result
- [x] JS Promise rejection -> C++ Error
- [x] real GitHub Actions integration test

완료 기준: real CEF로 bridge baseline 전부 통과.

## Phase 4 — Windows browser backends

### WebView2

- [x] WebView2 backend
- [x] runtime-aware auto detection
- [x] WebView2 integration contract tests
- [x] WebView2 multi-WebView isolation

### CEF Windows

- [x] CEF Windows backend
- [x] same contract suite
- [ ] engine switch test

완료 기준:

```text
Windows / WebView2
Windows / CEF
Linux   / CEF
```

가 가능한 한 동일 public API contract를 통과한다.

## Phase 4.5 — macOS WKWebView and Linux WebKitGTK (additive, post-Scope-A1)

- [x] Platform engine IDs `Engine::WkWebView` / `Engine::WebKitGtk` reserved.
- [x] Platform-aware `Engine::Auto` preference: WKWebView first on macOS,
  CEF first on Linux, WebView2 first on Windows; only linked adapters qualify.
- [x] macOS 15 ARM64 / Intel native C++11 Core CI workflow added.
- [x] macOS Core + plugin regression + installed SDK consumer + Objective-C++
  WKWebView API compile probe green on both GitHub runners:
  [38834287692](https://github.com/nssang00/chatgpt-write-test/actions/runs/38834287692).
  This is NOT WKWebView real-browser E2E.
- [ ] WKWebView (.mm) adapter with Cocoa main-thread dispatch and lifetime.
- [ ] macOS packaged `app://` URL scheme/Resource Provider and MIME/404 tests.
- [ ] WKWebView verified main-frame+navigation+document-session bridge:
  dotted RPC, `postMessage/onMessage`, `Binary` / `ArrayBuffer`.
- [x] macOS first real WKWebView E2E on ARM64+Intel Actions:
  [38841405596](https://github.com/nssang00/chatgpt-write-test/actions/runs/38841405596),
  advanced reverse RPC/session negative:
  [38841804226](https://github.com/nssang00/chatgpt-write-test/actions/runs/38841804226).
  More frame/navigation, crash/cancellation and packaging parity still pending.
- [ ] macOS standalone/embedded host, .app bundle, Apple signing/notarization
  with explicit credential configuration. No secret required for CI Core tests.
- [ ] WebKitGTK 4.1 + GTK3 POC: determine engine-authenticated sender frame
  provenance BEFORE granting JS -> native IPC. Do not treat session token
  alone as engine-authenticated origin/frame identity.
- [ ] Linux WebKitGTK real browser E2E, CEF fallback, dependency diagnostics.
- [ ] GTK4 / WebKitGTK 6.0 follow-up after GTK3 baseline, not dual GTK linkage.
- [ ] CPU/memory/startup/performance baselines for existing/added engines.

Supported **now**: Linux CEF / Windows CEF / Windows WebView2. The new engine
enum values and CI do NOT imply that WKWebView/WebKitGTK adapters are shipped.
Scope A1 release gate stays Windows/Linux only; this adds a staged platform
expansion without reclassifying unfinished browser tests as PASS.
See [macOS/WebKit platform plan](MACOS_WEBKIT_PORT_PLAN_KO.md).

## Phase 4.6 — JS ↔ Native bridge latency baseline (P1)

- [x] 5개 실제 브라우저 조합의 tiny-RPC latency CI smoke 완료:
  [브라우저 CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38846688020) ·
  [macOS CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38846688063).
  [첫 결과](BRIDGE_LATENCY_RESULTS_2026-10-16.md).
- [ ] JS→Native→JS, Native→JS→Native, one-way+ack, Binary RPC:
  cold/warm, p50/p95/p99, concurrency 1/8/32, ops/s 수집.
- [x] PR CI에서 측정 JSON/CSV/Markdown과 commit·OS·CPU·Runner
  메타데이터를 Actions artifact로 저장. 엔진 세부 버전 및 nightly는 후속.
- [x] 동일 Windows Runner에서 CEF↔WebView2 각각 3회 교차 실행 비교:
  [첫 CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38863601576),
  [후속 CI+SharedBuffer](https://github.com/nssang00/chatgpt-write-test/actions/runs/38864908293).
- [x] WebView2 native→JS SharedBuffer read-only 1KiB·64KiB·1MiB
  테스트와 JS release 동작/기존 Base64 이벤트 쌍비교 smoke:
  [실험 결과](BRIDGE_LATENCY_RESULTS_2026-10-16.md).
- [x] WebView2 실험용 SharedBuffer 2MiB/4건 in-flight quota,
  협조적 release-ack과 reload 무효화, 유효 토큰을 가진 same-origin iframe
  위조 이벤트 차단 확인:
  [전송량/내비게이션 CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38877466139) ·
  [iframe 음성 CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38877953065).
- [x] JS에서 1MiB SharedBuffer 해제와 release-ack을 의도적으로
  누락한 상태에서 추가 전송 제한 및 reload 무효화 E2E 통과:
  [CI 38878657939](https://github.com/nssang00/chatgpt-write-test/actions/runs/38878657939).
- [x] 동일 requestId 재사용 시 이전 문서 토큰/이전 전송 grant
  ACK 위조 및 중복 ACK 거부 E2E:
  [CI 38910026996](https://github.com/nssang00/chatgpt-write-test/actions/runs/38910026996).
- [x] 복원 불가 문서 누적 32MiB cap 및 1MiB×31회 후 차단
  E2E: [CI 38911699700](https://github.com/nssang00/chatgpt-write-test/actions/runs/38911699700).
- [x] 실제 WebView2 renderer `Page.crash` 후 `ProcessFailed`
  세션/grant 무효화와 `Reload()` 복구 E2E:
  [CI 39011968543](https://github.com/nssang00/chatgpt-write-test/actions/runs/39011968543).
- [x] 두 WebView가 동일 requestId를 사용해도 grant/quota가 격리되고
  한쪽 reload가 다른 쪽 pending 전송에 영향 없음을 확인:
  [CI 39012118661](https://github.com/nssang00/chatgpt-write-test/actions/runs/39012118661).
- [x] 문서당 32MiB × 4회 reload = 128MiB 누적 전송·release
  스모크와 host/browser main process 작업 집합 기록:
  [CI 39012660459](https://github.com/nssang00/chatgpt-write-test/actions/runs/39012660459).
  수동 `soak_cycles` 4~500회 확대 가능. PR CI에서는 100회 반복도
  검증했으나 수십 분 이상 지속하는 장기 실행은 별도.
- [x] WebView2 브라우저 **메인 프로세스 실제 강제 종료** 후
  `ProcessFailed` 통보와 기존 grant 무효화, 전용 새 사용자 데이터 폴더의
  새 컨트롤 생성 및 RPC/SharedBuffer 재활성화:
  [CI 39036334075](https://github.com/nssang00/chatgpt-write-test/actions/runs/39036334075).
- [x] 실제 100문서 × 32MiB = **3,200MiB 논리적 SharedBuffer 전송**,
  매 문서 release-ACK/reload 및 WebView2 전체 프로세스 그룹
  Private Bytes 101회 측정:
  [CI 39036736024](https://github.com/nssang00/chatgpt-write-test/actions/runs/39036736024).
  시작 81.2MB → 종료 94.7MB, 최고 121.9MB. 원본 CSV/JSON 보존.
- [ ] 기존 사용자 데이터 폴더를 재사용하는 browser-process 복구,
  수십 분 이상의 시간 기반 soak/여러 독립 Runner 분산,
  동시 다중 사용자 프로세스 장기 압력, hang/unresponsive 실측,
  제품 모드 정책 결정 전 공개 API 자동 전환 금지.
- [ ] full baseline 5세션×5,000 표본, encode/decode 단계별 trace,
  Native→JS LAT-03, WebView2 고급 SharedBuffer 수명·보안 테스트.
- [x] WebView2 Base64 Binary 프레이밍 예비 실측: 64KiB p50
  36.5→9.4ms (각 독립 CI 실행; 동일 장비 반복 측정 전).
  [전/후 측정값](BRIDGE_LATENCY_RESULTS_2026-10-16.md).
- [ ] Python/C# 실제 worker 비용은 별도 계측, IPC 개선 전후 비교.
- [ ] 최초 실측 수치와 반복 분산 확인 후 회귀 경고 임계치 결정.
  초기에는 GitHub-hosted Runner의 잡음 때문에 성능 수치만으로
  PR을 실패 처리하지 않음.

방법·완료 기준: [브릿지 지연시간 벤치마크 계획](XYTRON_BRIDGE_LATENCY_BENCHMARK_KO.md).
이는 **P1 관측/최적화 기준선 작업**이며 NW-014 보안 작업과 Scope A1
출시 기준을 변경하지 않는다. 첫 실측은 예비 smoke이며 장기 기준선은 미완료.

## Phase 5 — Thin host adapters (MFC/WPF/WinForms basic controls in first release)

- [ ] Win32
- [ ] MFC
- [ ] WinForms
- [ ] WPF
- [ ] Qt
- [ ] GTK

각 sample은 framework 기본 wizard/sample project에 control/widget 몇 파일을 추가한 수준을 유지한다.

Host layer가 application framework로 커지면 설계를 다시 검토한다.

## Phase 6 — Desktop APIs

우선순위 A:

- [ ] app
- [ ] window
- [ ] webview
- [ ] dialog
- [ ] clipboard
- [ ] shell

우선순위 B:

- [ ] fs/path
- [ ] process
- [ ] notification
- [ ] menu/context menu — mandatory first-release native app/context APIs
- [ ] tray
- [ ] global shortcut
- [ ] screen/DPI

우선순위 C:

- [ ] session/cookies/cache/proxy
- [ ] custom protocol
- [ ] single instance
- [ ] updater
- [ ] secure storage
- [ ] power
- [ ] capture

## Phase 7 — Plugin SDK

핵심 제품 차별화 단계. Phase 2.6 Callable metadata foundation을 재사용한다.

- [x] small cross-platform SharedLibrary abstraction + real DLL/SO load test
- [x] PluginModule + function registration ownership baseline
- [x] stable versioned C ABI v1 function-call baseline
- [ ] embedded plugin descriptor (id/version/ABI/runtime/capabilities)
- [x] queued/running task/module lifetime coupling
- [x] native object/module lifetime coupling (Linux SO and Windows DLL plugin E2E validated)
  - [x] ObjectRegistry/NativeObjectRuntime owner-lease foundation
  - [x] real plugin-created native object ABI/lifetime regression (Linux)
- [x] C++ wrapper
- [x] `NATIVEWEB_PLUGIN` macro/export convenience
- [x] plugin namespace registration
- [x] callable/module lifetime coupling
- [x] PluginModule conservative unload / plugin_busy baseline
- [x] WebView plugin manager integration and browser E2E tests — real Linux CEF / Windows CEF / WebView2 CI 38322633033
- [x] real DLL Windows regression
- [x] real SO Linux regression
- [x] binary function argument/result baseline
- [x] C ABI v1 object factory and instance-method support (internal runtime)
- [ ] plugin event support (WebView factory exposure and object call E2E already validated)
- [x] ABI compatibility validation baseline
- [ ] TypeScript declaration generation
- [ ] missing-dependency diagnostics
- [ ] `xytron plugin new/build/inspect`

장기:

- [ ] isolated plugin host process

## Phase 8 — Sidecar (optional process model; distinct from official Python/C# wrappers)

- [ ] process lifecycle abstraction
- [ ] transport protocol
- [ ] restart/error handling
- [ ] typed request/response
- [ ] event stream
- [ ] binary/streaming
- [ ] example Python service
- [ ] example Node service
- [ ] example .NET/Rust service

## Phase 9 — CLI / developer experience (MVP moved into first release)

Working CLI name: `xytron` (final brand not fixed).

- [ ] `xytron new`
- [ ] `xytron run`
- [ ] `xytron build`
- [ ] `xytron test`
- [ ] `xytron publish`
- [ ] `xytron doctor`
- [ ] `xytron engine`
- [ ] `xytron permission`
- [ ] `xytron plugin`

`engine = auto`가 기본이다. **최소 xytron CLI의 new/restore/run/build/test/publish/doctor/plugin new/build는 Scope A 필수**, 이 Phase에는 고급 CLI 확장을 남긴다.

## Phase 10 — Code - OSS benchmark

NativeWeb의 Electron-scale compatibility/stress test.

- [ ] Code - OSS Web Workbench load
- [ ] keyboard/editor/WebWorker/WASM/WebGL
- [ ] filesystem/dialog/clipboard
- [ ] native window/menu services
- [ ] Node extension host as sidecar
- [ ] PTY/terminal
- [ ] extension compatibility investigation
- [ ] Electron shell dependency reduction

이 단계는 NativeWeb 1.0의 선행조건이 아니다.
