# Xytron macOS WKWebView / Linux WebKitGTK 포팅 계획

기준: 2026-10-16. 기존 문서 [ROADMAP](ROADMAP.md), [브릿지·리소스 계획](XYTRON_BRIDGE_TRANSPORT_PLAN_KO.md),
[NW-014 문서 세션](XYTRON_NW014_DOCUMENT_SESSION_KO.md)과 함께 적용.

## 제품과 범위 결정

- 기존 공식 실브라우저 타깃: Linux CEF, Windows CEF, Windows WebView2.
- Windows: WebView2 우선, CEF 선택.
- macOS: **WKWebView 우선**, 추후 요구가 확인되면 CEF 선택.
- Linux: **CEF 우선**, WebKitGTK 4.1(현 GTK3 호스트) 선택. GTK4/6.0은 이후 별도 호스트.
- 사용자 C++/Python/C# 백엔드 언어는 브라우저 엔진 선택과 독립적이다.
- 공개 JS API는 `xytron.math.add()`, `xytron.postMessage/onMessage`,
  `fetch()`, `ArrayBuffer`를 그대로 사용한다.
- 현재 **Scope A1은 Windows/Linux 필수**이며 macOS/WebKitGTK는 별도 추가
  확장이다. macOS Core CI 성공은 WKWebView E2E 성공을 의미하지 않는다.

## 0. 첫 단계 — macOS Core CI 및 포터빌리티

- [x] `macos-15` ARM64 / `macos-15-intel` x86_64 CI 매트릭스 추가.
- [x] 실제 두 runner의 C++11 Core/Plugin/CLI 빌드 및 CTest 통과.
  증거: [macOS Core Port CI 38834287692](https://github.com/nssang00/chatgpt-write-test/actions/runs/38834287692).
- [x] Darwin에는 `sigtimedwait`가 없어 `process_broker.cpp` SIGPIPE
  cleanup을 `sigwait`로 대체 (신호 pending 검증과 block 유지).
- [x] ARM64/Intel 빌드·CTest로 수정 검증 완료 (38834287692).
- [x] macOS 네이티브 Core SDK 설치·재배치 후 별도 소비자 CMake/CTest 단계 추가.
- [x] 해당 SDK 소비자 단계 ARM64 / Intel CI 통과 (38834287692).
- [ ] ARM64/Intel runner를 각각 재현 가능한 artifact로 기록.
- Intel runner는 GitHub 공지 기준 2027-08까지 제공 예정. 지속
  지원을 제품 약속으로 만들기 전에 대체 빌드 경로를 수립한다.

## 1. macOS WKWebView 엔진

- [x] AppKit/WebKit 프레임워크와 WKURLSchemeHandler /
  WKScriptMessageHandlerWithReply Objective-C++ 컴파일 프로브 추가.
  컴파일/링크 CI 두 아키텍처 통과 (38834287692).
  이는 완성된 adapter나 실브라우저 E2E가 아님.
- [x] WKWebView 최소 Objective-C++ BrowserBackend adapter 구현 및
  Core RPC/event/packed app:// 리소스의 첫 실브라우저 E2E 추가.
- [x] ARM64/Intel WKWebView 실브라우저 기본 RPC·바이너리·이벤트 E2E
  통과: [CI 38841405596](https://github.com/nssang00/chatgpt-write-test/actions/runs/38841405596).
- [x] Native→JS execute(Promise 응답/오류), Uint8Array offset 및
  위조 세션 거부 실제 브라우저 테스트 통과:
  [CI 38841804226](https://github.com/nssang00/chatgpt-write-test/actions/runs/38841804226).
- [x] worker→Cocoa 전송을 메인 스레드로 마샬링하고 pending 작업을
  navigation generation 및 destroy lifetime으로 무효화하도록 구현.
- [ ] lifecycle race / iframe / same-URL reload 강화된 음성 E2E.
- [ ] macOS 전용 Objective-C++(`.mm`) WebKit adapter 및 Cocoa window/NSView
  생성, 메인 스레드 스케줄링, destroy/navigation 안전성.
- [ ] `Engine::WkWebView` 실제 factory 등록과 `Engine::Auto` 판정.
  Enum/factory 선행 선언만으로 사용 가능 엔진이 되는 것은 아니다.
- [ ] `WKUserScript` + `WKContentWorld` 격리, `WKScriptMessageHandlerWithReply`,
  `WKFrameInfo` 기반 검증 및 기존 문서 토큰 회전/폐기.
- [ ] JS→C++ RPC 오류/취소, C++→JS `execute`, 단방향 이벤트.
- [ ] 일반 `Binary`→`ArrayBuffer` 복사 계약, typed-array offset, 중첩 데이터.
- [ ] `WKURLSchemeHandler`로 `app://` 리소스 지원 (정확한 Origin/MIME,
  CSP/CORS, 실패/취소/내비게이션).
- [ ] 실제 macOS Runner에서 브라우저 생성·호출·응답·리로드·폐기 E2E.
  초기에는 WKWebView 내부 자체 체크포인트를 사용하고, GUI 의존
  테스트는 XCTest/호스트 재현성을 검증해 추가한다.
- [ ] .app/SDK 패키징 및 배포 테스트. 서명·공증은 추후 명시적 인증서/팀 구성.

### macOS 브릿지 지연시간 기준선

- [ ] ARM64/Intel WKWebView의 `xytron.math.add()` JS→Native→JS RTT
  실측(p50/p95/p99).
- [ ] Native→JS `execute()` RTT, `postMessage`+ack,
  ArrayBuffer/typed-array 크기별 응답 및 큐잉(1/8/32) 실측.
- [ ] macOS 시스템 WebKit 버전·Runner image·아키텍처를 기록해
  결과를 Windows/Linux의 절대 엔진 성능 순위로 오용하지 않음.
- [ ] WebKitGTK가 실제 구현되면 동일 벤치마크 매트릭스에 추가.

참조: [공통 브릿지 지연시간 벤치마크 계획](XYTRON_BRIDGE_LATENCY_BENCHMARK_KO.md).

## 2. Linux WebKitGTK 선택 엔진

- [ ] WebKitGTK 4.1 + GTK3 pkg-config/headers 지원 탐색 및 최소 페이지 로드.
- [ ] UI process와 web process 구분, 검증 가능한 메인 프레임 출처 신호/API
  실증. 확인 전에는 JS→Native 브릿지 허용 금지 (fail closed).
- [ ] `webkit_web_context_register_uri_scheme` 기반 `app://` 로딩과
  응답 MIME/404/취소 테스트.
- [ ] RPC/event/ArrayBuffer 및 기존 보안 음성 테스트 포팅.
- [ ] X11/Wayland, GStreamer 및 배포판 런타임/라이브러리 버전 검사.
- [ ] GTK4 + WebKitGTK 6.0은 별도 호스트 어댑터가 준비된 후.

## 공통 엔진 계약과 승인 조건

- `BrowserBackend`와 document-session/security 경계를 유지하며
  기존 CEF/WebView2 구현을 다른 엔진의 권한 우회 근거로 삼지 않는다.
- 브라우저 실기반 E2E: trusted main만 허용, cross-origin/iframe,
  stale session replay, pre/post-navigation race, plugin unload, cancel,
  binary transport, no UAF 및 browser shutdown.
- `postMessage`는 단방향/전달 보장 없음. 민감 동작은 검증된 RPC 사용.
- Custom Scheme/Plugin Resource Provider, SharedBuffer는
  [브릿지·리소스 계획](XYTRON_BRIDGE_TRANSPORT_PLAN_KO.md)의 뒤 단계;
  WKWebView 및 WebKitGTK 포팅의 필수 선결조건이 아니다.
- macOS 브라우저 CI와 적절한 배포 smoke 없이 macOS 지원 완료로 표기 금지.
