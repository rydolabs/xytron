# Xytron NW-014: 문서별 세션 토큰 — 구현 및 보안 범위

## 목표
같은 URL로 페이지를 재탐색하더라도 이전 렌더러 문서에서 늦게 배달된
JavaScript ↔ C++ 메시지가 새 문서의 권한으로 처리되지 않도록 한다.

## 구현
- JS → Native fire-and-forget event도 엔진 제공 top-frame/navigation
  프레임 컨텍스트를 Core에 전달한다. Core는 활성 trusted frame과
  일치하지 않는 event를 무응답 폐기한다. 실제 과거 렌더러 in-flight
  race 및 리사이클 조건은 여전히 별도 E2E/구현 과제다.
- 메인 프레임의 네이티브 내비게이션 이벤트에서 이전 토큰을 폐기하고 새 토큰을 발급한다.
- 토큰은 24바이트(192비트) OS 난수의 48자리 소문자 16진수이다.
  - Windows: `BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)`
  - Linux/Unix: `/dev/urandom`, 오류 시 fail-closed
- 정상 문서의 로드 완료 시 새 토큰을 해당 최상위 프레임에 제공한다.
- CEF: 브라우저 → 렌더러 `nativeweb.document.session` 메시지; 렌더러
  `CefV8Context`별 비공개 토큰과 요청 대기열. 요청 IPC slot 3,
  네이티브 JS 호출 응답 IPC slot 4에 토큰 포함.
- WebView2: 브라우저 → 페이지 `nativeweb_session` 메시지; 주입된
  브릿지의 클로저에 토큰 보관. 요청 전송을 토큰 수신까지 지연하고
  요청/응답/오류 메시지에 `session` 필드를 추가한다.
- 백엔드는 기존 **엔진 제공 URL, 메인 프레임, 내비게이션, 메서드 허용 목록**
  검사에 더해, **현재 문서의 토큰이 반드시 존재하고 일치**해야
  브릿지 메시지를 Core에 전달한다.
- 토큰 없는 요청 또는 다른 토큰을 가진 요청은 처리하지 않는다.
  이 경우 새 문서의 우연히 동일한 요청 ID와 충돌할 수 있으므로
  응답도 새 메인 문서에 임의로 전달하지 않는다.
- 관리형 ProcessBroker와 일반 C++ 바인딩은 동일한 네이티브 입구를 통과한다.

## 검증
- `core.document_session`: 토큰 형식, 난수 재발급, 이전 토큰 거부,
  길이 변경 및 변조 거부를 Windows/Ubuntu에서 확인한다.
- 기존 Browser plugin E2E: CEF Linux/Windows, WebView2 Windows에서
  C++/Python/.NET 정상 호출이 계속 동작하는지 확인한다.
- WebView2 관리형 테스트는 정상 페이지에서 직접
  `chrome.webview.postMessage`로 **누락된 세션/틀린 세션** 메시지를 만들어
  `test.fail` 바인딩이 실행되지 않는지 확인한다.
- 2026-10-16 소스 커밋 `2bbc37c`, [실브라우저 E2E #38781105368](https://github.com/nssang00/chatgpt-write-test/actions/runs/38781105368):
  **Linux CEF / Windows CEF / Windows WebView2 모두 PASS**.
  동일 URL 재로드 후 이전 세션 토큰을 사용해 네이티브 호출·취소를 시도한다.
  이전 세션 요청의 부작용이 0이고, 살아 있는 새 문서의 요청은 유지된다.
- CEF에서는 통합 테스트 빌드에만 포함되는 `NATIVEWEB_CEF_ENABLE_E2E_REPLAY_HOOKS`
  후크로 이전 토큰을 렌더러 내부에만 보관하고, 새 문서에서 실제 CEF
  `nativeweb.request` / `nativeweb.cancel` 프로세스 메시지를 전송한다.
  일반 배포용 렌더러 코드에는 이 후크가 활성화되지 않는다.
  WebView2는 기존 JS→호스트 `postMessage` 테스트를 유지한다.
- **검증 한계:** 현재 실브라우저 테스트는 *이전 토큰 재사용 차단*을 검증한다.
  실제 파괴된 구 렌더러에서 이미 전송한 IPC가 내비게이션과 경합하여 늦게
  도착하는 순서·시점, 렌더러 프로세스 전환 및 모든 프레임 식별 재사용 경우를
  그대로 재현하지는 않는다. 추가 경합 E2E와 NW-014 권한 위임 정책은 남아 있다.

## 보안 한계
- 이 토큰은 문서 세대별 재생 방어 용도이지 JS XSS에 대한 샌드박스가 아니다.
  WebView2의 호스트 메시지는 페이지 JS에서 관찰 가능하다.
- JS가 이미 실행 가능한 신뢰 문서를 공격자가 장악한 상황까지 방어하지 않는다.
- CEF/V8 컨텍스트, WebView2 엔진 제공 출처 및 최상위 프레임 제한은 유지되어야 한다.
- 네이티브 요청 대기열은 최대 256개로 제한된다. 토큰이 전혀 오지 않는
  악성 페이지의 기다리는 JS Promise를 별도 타임아웃하는 기능은 아직 없다.
- PluginManager 서명, OS 샌드박스 및 전체 NW-014 권한 위임은 미구현이다.
