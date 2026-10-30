# Xytron S2: 문서 내비게이션 시 C++ → JS future 취소

- 기존 JS → C++ 비동기 응답은 `ResponseSinkState::generation`으로 이전 문서 응답을 제한한다.
- 새로운 `BridgeRuntime::rejectPending(error)`는 진행 중인 **C++ → JavaScript** `std::future<T>`를 오류로 완료하지만 일반 바인딩/이벤트 등록은 유지한다.
- `WebView::load()`, `reload()`, 엔진의 `onLoadStarted()`, 종료 경로에서 `invalidateDocument()`를 통해 `navigation_changed` 오류로 기존 future를 해제한다.
- 늦게 도착한 이전 응답 ID는 `PendingCallRegistry`에서 제거됐으므로 무시되며, 새 요청 ID는 계속 증가한다.
- 테스트: `core.webview_orchestration` — 요청 후 reload, 이전 ID 응답, 새 요청 완료, 브라우저 주도 내비게이션.
- 기존 샘플/호스트는 UI 스레드에서 `future.get()`으로 블로킹하지 말아야 한다.
- **제한:** 호출 타임아웃, 원격 JS Promise 취소, 프레임별 NW-014 권한 세분화, PluginManager/서명 검증은 포함하지 않는다. 요청 결과 취소가 원격 JS 작업 자체를 취소하는 것은 아니다.

## 빌드 안정성

`cmake/NativeWebCoreSources.cmake`를 Core/CEF/WebView2 세 프로젝트에서 공통으로 사용한다. 신규 Core 소스를 한 곳에 등록하면 브라우저 단독 빌드 링크 누락을 방지한다.
