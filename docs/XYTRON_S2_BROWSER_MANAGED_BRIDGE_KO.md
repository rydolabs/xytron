# S2-7 패키징 앱 관리형 브릿지 — 실험적

## 구현

- `WebView::bindManagedWorker(pluginId, allowedOperations, command, timeoutMs)`는 신뢰한 `app://` 엔트리로 생성된 WebView에서만 사용한다. 관리형 객체의 create/method/get/set/dispose를 기존 C++11 `ProcessBroker`를 통해 Python/.NET IPC v0으로 전달한다.
- 호스트가 승인한 **정확한 메서드 이름만** BindingRegistry에 등록한다. 다른 플러그인, 중복, 위험 식별자, 비공개 메서드는 등록되지 않는다. 브라우저 백엔드는 기존 엔진 유래 문서 URL과 최상위 프레임을 확인한 후에만 Core로 전달한다.
- 임의의 JavaScript가 worker 실행 경로, 세션 owner, 프레임/출처를 지정하지 않는다. worker command에는 신뢰한 절대 실행 경로가 필요하다.
- 상위 문서 내비게이션/load/reload/destroy/close에서 등록과 worker 세션을 철회한다. 이전 문서 요청 응답은 요청 당시 세대 번호가 다르면 새 문서에 전송하지 않는다. 종료 작업은 UI 스레드에서 별도 종료 스레드로 넘긴다.

## 검증

- `core.managed_bridge`: 모의 백엔드에서 **실제 C++ BridgeRuntime → Python 자식 프로세스** 객체 수명·권한·내비게이션 회귀 테스트.
- `browser.managed_python`: Linux/Windows CEF 및 Windows WebView2에서 실제 `managed_bridge_test.html`이 JS로 객체 생성·호출·접근·해제한다. 승인하지 않은 메서드와 동일 URL 자식 프레임을 거부한다.
- 기존 C++11 ABI v1, 네이티브 플러그인, std::future/Promise는 변경하지 않는다.

## 여전히 미완료

- 정식 NW-014 권한 시스템 전체(사용자 동의, 권한 선언, 동적 회수, 플러그인 서명/격리)는 아직 미구현. **현재는 명시적 함수별 허용 목록과 패키징된 신뢰 엔트리만 허용한다.**
- Python/.NET 프로세스는 OS sandbox가 아니며 외부 임의 코드를 실행하면 안 된다.
- Windows 파이프 쓰기의 엄격한 타임아웃, 비동기 worker 자동 재시작/취소, 응답 전달 직전 엔진 내비게이션 경합의 완전한 종결 처리도 추후 보강 대상.
- 생성된 JS/TS `index.mjs`를 자동으로 웹 자원에 배포하거나 .NET 객체를 실제 엔진에서 별도로 검증한 단계는 아니다. 브라우저 E2E는 일반 `xytron.*` 호출을 사용한다.
