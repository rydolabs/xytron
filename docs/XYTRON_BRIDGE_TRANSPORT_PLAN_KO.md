# Xytron 브릿지·리소스 통합 구현 계획 (2026-10-16)

상태: 합의된 제품 방향 + 단계별 구현 계획. 완료 체크는 구현/실제 브라우저 검증이 확인된 것만 표시한다.
개발 브랜치: nativeweb/autonomous-integration, PR #7 (draft, main 미병합).

## 공개 API (CEF / WebView2 동일, C++ / Python / C# 백엔드 동일)

- 대표 JS 함수 호출: `await xytron.math.add(1, 2)`.
  `xytron.invoke("math.add", 1, 2)`는 호환/동적 호출용 원시 API.
- 단방향: JS `xytron.postMessage(channel, data)`,
  `xytron.onMessage(channel, callback)` / `offMessage(id)`.
  C++ `WebView::postMessage/onMessage/offMessage`.
- Native `Binary` (`ByteArray` 동일 타입) -> JS `ArrayBuffer`.
  `std::vector<std::string>`은 byte buffer 아님. Python bytes와 C# byte[] 매핑은 별도 개발.
- 큰 파일: Plugin Resource Provider가 유효기간·권한 검증된 URL을 제공하고,
  JS는 표준 `fetch(url)` / Response.body / Blob으로 소비.
- 반복 갱신되는 큰 데이터: 명시적인 shared-buffer API, 소유권/해제 계약.
- 플러그인/모듈이 주 확장 단위. 임의 custom scheme은 고급 선택 기능.

## 작업 순서

1. **P0 보안 유지 + 단방향 브릿지 baseline**
   - [x] C++ Core EventDispatcher 재사용; JS ↔ Native postMessage/onMessage 연결.
   - [x] CEF/ WebView2 진짜 브라우저 E2E에 메시지/ArrayBuffer 검증 추가.
   - [ ] 브라우저 CI 통과 확인 및 문서/구현 실제 상태 동기화.
   - [x] 단방향 event에 stale session token 재전송 방어 브라우저 테스트 추가.
   - [x] native 메시지 콜백 예외 격리 및 Core regression 추가.
   - [x] 단방향 메시지도 verified frame/navigation을 Core까지 전달하여 검사.
   - [ ] NW-014: old renderer in-flight IPC/navigation/frame 재사용 경쟁,
     method/event grant 강화. 이벤트에 권한 없는 채널 접근을 허용하지 않을 정책 필요.
2. **P1 기본 바이너리 계약 완성**
   - [x] `Binary`와 `ByteArray` 이름 동일화 (소유형 std::vector<unsigned char>).
   - [x] CEF typed-array 입력 (subarray/DataView offset·중첩 구조 포함) 변환.
     - [ ] 신규 E2E의 3개 엔진 조합 CI 통과 확인.
   - [x] WebView2 1차 Binary JSON wire 최적화: byte-number-array 대신
     base64 marker; 구형 numeric array 수신 호환성 유지, Core 회귀 테스트.
- [ ] WebView2 control envelope + raw SharedBuffer/zero-copy transport는
     별도 후속. Base64는 복사 기반이며 실제 WebView2 E2E에서
     64KiB p50 36.5→9.4ms 예비 개선 확인([실측 기록](BRIDGE_LATENCY_RESULTS_2026-10-16.md)).
   - [ ] Python/C# worker IPC의 스칼라/64KiB 제약 해소, framing/limit/backpressure,
     bytes/byte[] 왕복·worker crash·cancel 검증.
2.5. **P1 브릿지 호출 지연시간 벤치마크**
   - [ ] 공통 실브라우저 fixture로 JS→Native RPC와 Native→JS execute RTT.
   - [ ] postMessage+ack, Binary/ArrayBuffer, cold/warm, 1/8/32 동시성 측정.
   - [ ] Windows WebView2·CEF / Linux CEF / macOS WKWebView(ARM/Intel)
     p50·p95·p99·처리량 기준선과 CI 원본 아티팩트 확보.
   - [ ] Python/C# worker 경로는 실제 IPC가 구현된 후 별도 측정.
   - [ ] 현 단계는 **계획**이며 실측값·속도 SLA는 아직 없음.
     상세: [브릿지 지연시간 측정 계획](XYTRON_BRIDGE_LATENCY_BENCHMARK_KO.md).
3. **P1 Plugin Resource Provider + fetch**
   - [ ] core ResourceRouter / 요청/응답/stream/cancel 타입, 등록 해제.
   - [ ] 고정 `app://` 권한 있는 opaque resource URL 구현.
   - [ ] 응답 Content-Type/Range/cache/CORS/CSP와 엔진별 스트리밍 확인.
   - [ ] C++/Python/C# provider, 플러그인 언로드/문서 내비게이션 철회 테스트.
4. **P2 고급 Custom Scheme**
   - [ ] 선언형 manifest + 사전 엔진 등록 (CEF 초기화/WebView2 Environment 생성 전).
   - [ ] `xytron new scheme <name> --module <id>` 템플릿,
     C++/Python/C# `onRequest(request)` 및 에러/취소 테스트 생성.
   - [ ] 첫 실증 예제: `mbtiles://` -> SQLite XYZ/TMS 조회 / PBF MIME.
   - [ ] `serial://` 예제는 재시도·장치 I/O 부작용에 주의, RPC/event 우선.
5. **P2+ 고성능 선택적 공유 버퍼**
   - [ ] CEF 공유 메모리 IPC vs WebView2 SharedBuffer 어댑터.
   - [ ] JS ArrayBuffer 접근 권한, release, navigation/unload 크래시 안전성.
   - [ ] 대용량 throughput·copies·backpressure 벤치마크 후 opt-in.

## 핵심 제약

- 일반 사용자는 엔진 종류를 모른다. 내부 전송 구현은 달라도 API/오류/타입 일치.
- 공유 버퍼나 transfer의 ownership 변화는 데이터 크기만으로 자동 발생시키지 않음.
- Custom Scheme 이름은 엔진 생성 전 선언해야 하며 플러그인 실행 중 새 스킴
  등록은 재시작/기존 resource URL fallback이 필요할 수 있음.
- User-defined scheme/Resource Provider는 파일/포트/DB 전역 권한이 아님.
  문서 세션·프레임·모듈별 grant, URI normalization, SSRF/경로 순회와
  요청 헤더·MIME·CORS 정책, 취소 및 백프레셔를 각각 검증.
- 단방향 postMessage는 송신 성공 확인과 전달 보장을 약속하지 않는다.
- P0 완료 및 릴리스/merge 여부는 별도 검증·사용자 승인 필요.

## 2026-10-16 SharedBuffer 단독 실험 (Native → JS)

- 내부 WebView2 backend `postReadOnlySharedBufferProbe` API는
  **실험용 코드**이며, 공개 JS/RPC 바이너리 규약이 바뀌지 않는다.
- 실제 WebView2 앱 리소스 문서에서 엔진 검증된 메인 프레임 이벤트로
  요청하고, 읽기 전용 `ICoreWebView2SharedBuffer`를 Native에서 생성,
  복사한 뒤 `PostSharedBufferToScript`로 전달한다. 같은 앱 문서와 같은
  요청·검증 로직으로 기존 Base64 Native→JS 이벤트 전달도 계측한다.
- JS는 `sharedbufferreceived`에서 크기/바이트를 검증하고
  `releaseBuffer()`를 반드시 호출하며, Native는 Post 이후 `Close()`.
- [x] Windows WebView2 브라우저에서 1KiB/64KiB/1MiB 크기별
  Base64·SharedBuffer 번갈아 전송(총 106회), 전체 바이트 확인,
  SharedBuffer 53회 release 및 latency 수집:
  [CI 38864908293](https://github.com/nssang00/chatgpt-write-test/actions/runs/38864908293).
  [상세 수치](BRIDGE_LATENCY_RESULTS_2026-10-16.md).
- [ ] 내비게이션 중단, JS 미해제, 프레임·권한, 연속 전송 backpressure
  검증 전에는 `WebView::postMessage`에 자동 사용 금지.
- 이 실험은 JS→Native 바이너리 RPC 전송 최적화가 아니고,
  Native→JS 공유 버퍼의 별도 가능성 확인이다.

**SharedBuffer 실험의 주의:** Native→JS 전송 실험에는 JS→Native
제어 요청과 Native 바이트 채우기, UI 스케줄링이 포함된다. 본 측정은
독립된 tiny RPC/64KiB `bench.echo`와 같은 지표가 아니다. API 지원 여부,
버퍼 검증·해제·문서 세션 안전성을 확인한 뒤 실제 앱 전송 계약으로
승격할 수 있다.

## 2026-10-16: SharedBuffer quota + document/frame negative CI

- 실험 API에 문서당 미확인 SharedBuffer **2MiB/4건** 상한,
  requestId 중복 방지 및 검증된 메인 프레임의 명시적 release-ack
  회계 처리를 추가했다. 문서 내비게이션 시작/브라우저 파괴 시
  미확인 상태를 지운다.
- [x] 실제 WebView2에서 1MiB 버퍼 5건 연속 전송 요청 시
  2건만 허용, 3건 차단 후 추가 전송 거부.
  [기본 안전성 CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38877466139).
- [x] 동일 origin의 iframe에게 **메인 문서의 실제 세션 토큰**을
  전달해 `chrome.webview.postMessage`로 이벤트를 위조해도
  메인 프레임의 권한으로 디스패치되지 않는지 검사.
  [iframe 음성 CI](https://github.com/nssang00/chatgpt-write-test/actions/runs/38877953065).
- [x] WebView2 reload의 NavigationStarting에서 미확인 전송 0B로
  reset되고, 재활성화 전 전송을 거부하는지 실브라우저 검사.
- [x] **JS releaseBuffer()와 release-ack을 둘 다 누락**하고
  1MiB 버퍼 참조를 유지한 채 한도 재전송 거부 및 새로고침 경계
  무효화 실브라우저 확인:
  [CI 38878657939](https://github.com/nssang00/chatgpt-write-test/actions/runs/38878657939).
  이는 1개 누락 버퍼/1회 새로고침 검증이며 장기 메모리·crash 안전성
  입증으로 해석하지 않음.
- [x] 새로고침 이후 **동일 requestId=200**을 재사용하되,
  구 문서 세션 + 새 grant, 현 문서 + 구 grant, 중복 ACK 모두 거부;
  현재 grant만 정상 ACK. [CI 38910026996](https://github.com/nssang00/chatgpt-write-test/actions/runs/38910026996).
- [x] SharedBuffer마다 192비트 무작위 grant를 생성하여 ACK의
  requestId뿐 아니라 해당 실제 할당의 grant도 검사.
- [x] 협조적 ACK로 돌려받을 수 없는 **문서당 누적 전송 32MiB**
  제한 구현. 새 문서의 선행 1KiB + 1MiB 반복 31회 허용 후
  다음 1MiB 차단을 실제 브라우저에서 검증:
  [CI 38911699700](https://github.com/nssang00/chatgpt-write-test/actions/runs/38911699700).
- [x] WebView2 `ProcessFailed`에서 main renderer/browser 종료 감지 후
  메인 문서 토큰, 미확인 SharedBuffer 및 누적 quota 즉시 무효화;
  renderer 종료는 **명시적 host `Reload()`**로 복구.
  실제 DevTools `Page.crash`로 renderer 종료, 1MiB grant 폐기,
  새 문서에서 새 grant/1KiB 전송 확인:
  [CI 39011968543](https://github.com/nssang00/chatgpt-write-test/actions/runs/39011968543).
  브라우저 메인 프로세스 강제 종료 및 **새 격리 프로필 생성** 검증은
  별도 [CI 39036334075](https://github.com/nssang00/chatgpt-write-test/actions/runs/39036334075)에서 완료.
- [x] 동일 Win32 호스트의 실제 WebView2 **두 인스턴스**:
  A의 quota 2MiB 소진 후 B에서 1MiB 성공, 상호 grant ACK 거부,
  A reload 후 B의 pending quota 보존과 독립 해제 확인:
  [CI 39012118661](https://github.com/nssang00/chatgpt-write-test/actions/runs/39012118661).
- [x] 동일 WebView2에서 1MiB × 32회/문서를 4개 문서에 걸쳐
  반복 전송·release+ACK·reload, 총 **128MiB 논리적 전송량** E2E
  스모크 통과: [CI 39012660459](https://github.com/nssang00/chatgpt-write-test/actions/runs/39012660459).
  호스트 작업 집합 약 12.8MB→최대 14.8MB, WebView2 브라우저
  메인 프로세스 약 86.8MB→최대 89.9MB(렌더러/유틸리티 제외).
- [x] 수동 `Windows Bridge Paired Benchmark` workflow의
  `soak_cycles` 입력값(4~500)으로 확장 테스트를 재현할 수 있도록 준비.
- [x] 실제 100회 reload/총 3,200MiB 논리적 SharedBuffer 전송,
  매 사이클 렌더러/GPU/utility 등 WebView2 Runtime 프로세스 그룹
  Private Bytes 기록 (crashpad 제외):
  [CI 39036736024](https://github.com/nssang00/chatgpt-write-test/actions/runs/39036736024).
  101개 메모리 표본 및 CSV/JSON/요약 Markdown을 Actions artifact 보관.
  프로세스 그룹 Private Bytes 81,162,240B → 94,679,040B,
  중간 최대 121,913,344B. 5개 프로세스·렌더러 1개 관측.
  해당 CI의 100회 전송은 약 40초 내외의 단기 스트레스이며
  **수십 분 이상의 장기 누수·안정성 검증은 아직 아님**.
- [x] 전용 temp 사용자 데이터 폴더의 실제 WebView2 browser
  main process를 강제 종료하고 ProcessFailed(BROWSER_PROCESS_EXITED),
  SharedBuffer grant 폐기 및 새 격리 프로필/컨트롤 생성 테스트:
  [CI 39036334075](https://github.com/nssang00/chatgpt-write-test/actions/runs/39036334075).
  기본 profile이나 다른 WebView2 앱 프로세스는 종료하지 않음.
- [ ] 동일 사용자 데이터 폴더의 브라우저 재사용/재생성,
  수십 분 이상 장기 압력/다중 호스트, hang/unresponsive,
  악성 ACK와 비정상 종료의 결합 경계 실측.

주의: release-ack은 **협조적 회계 처리**이며, JS 측 `releaseBuffer()`
실행의 증명이 아니다. 현 문서가 유효 grant를 알고 ACK를 위조해도
**미확인 전송량** 한도는 우회 가능하다. 하지만 같은 문서 내에서
누적 32MiB 상한은 복원되지 않는다. 여러 문서/브라우저에서
반복적으로 리로드하는 경우까지 메모리를 완전히 제한하는 것은
아니다. 기본 `xytron` 바이너리 전송으로 자동 승격하지 않는다.

### ProcessFailed 복구 범위와 측정 주의 (2026-10-17)

- WebView2 `COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED`:
  back end는 세션/SharedBuffer grant를 즉시 취소한다. 외부 호스트가
  이후 명시적으로 `Reload()`를 호출한다. 정상 재활성화 시 새 토큰/
  grant가 필요하다. 기존 자동 복구 또는 무조건 재로드는 하지 않는다.
- `COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED`:
  동일하게 상태를 취소하지만 **Reload만으로는 복구 불가능**하다.
  WebView 컨트롤러 재생성이 필요. **전용 첫 프로필의 브라우저를
  강제 종료한 뒤 새로운 격리 프로필로 재생성**하는 E2E까지 완료:
  [CI 39036334075](https://github.com/nssang00/chatgpt-write-test/actions/runs/39036334075).
  동일 사용자 데이터 폴더 재사용과 Environment5 BrowserProcessExited
  자원 종료 타이밍 검증은 별도 후속.
- 이 테스트용 `Page.crash`, quota 및 memory 관측용 메서드는
  WebView2 내부/테스트 전용이다. 공개 `xytron` RPC로 노출하지 않는다.
- 기존 작업 집합(Working Set) 스모크는 호스트/브라우저 메인만
  보았지만, 새 `ICoreWebView2Environment8::GetProcessInfos` 실험은
  같은 사용자 데이터 폴더의 browser/renderer/GPU/utility
  Private Bytes를 집계한다(크래시패드 제외). 각 시점에 살아 있는
  프로세스만 측정하며 공유 매핑 및 OS 캐시 전체 비용을 의미하지
  않는다. **한 번의 100회 결과로 leak-free를 보증하지 않는다.**
