# Xytron JS ↔ Native 브릿지 지연시간: 첫 실브라우저 스모크

측정일: 2026-10-16. 대상 커밋: `3c45b72c80c49c696a324f0110718af8f211018d`.
**표본은 각 독립 GitHub-hosted Runner의 1회 실행**이다.
Release 빌드, warm-up 20, 작은 요청 100회, 64KiB 40회(워밍업 10회).
값 단위: **ms**, 각 칸은 **p50 / p95**.
정식 반복 성능 기준선, 속도 보증 또는 동일 하드웨어 엔진 대조 결과가 아니다.

| 환경 | Tiny `xytron.math.add()` RTT | `postMessage`+ack RTT | Binary 1KiB RTT | Binary 64KiB RTT |
| --- | ---: | ---: | ---: | ---: |
| Windows WebView2 | 0.200 / 0.305 | 0.200 / 0.205 | 0.800 / 0.900 | 36.500 / 37.230 |
| Windows CEF | 0.300 / 0.400 | 0.200 / 0.300 | 0.300 / 0.400 | 0.600 / 0.700 |
| Linux CEF | 0.250 / 0.400 | 0.200 / 0.300 | 0.300 / 0.400 | 0.700 / 0.800 |
| macOS WKWebView ARM64 | 0.000† / 1.000 | 0.000† / 1.000 | 1.000 / 3.000 | 27.500 / 35.100 |
| macOS WKWebView Intel | 1.000 / 5.100 | 1.000 / 1.000 | 2.000 / 3.000 | 58.000 / 79.300 |

† macOS WKWebView 두 Runner에서 `performance.now()`의 최소 관찰
tick은 **1ms**였다. 0.000ms 표본은 0 지연이 아니라 **시계 해상도
내에서 구분하지 못한 측정값**이다. Windows CEF/WebView2의 최소 관찰
tick은 0.1ms였다. 원본 JSON의 `clock_resolution_ms`와
`quantized_zero_fraction`을 반드시 함께 확인해야 한다.

증거(각 job의 `xytron-bridge-latency-*` artifact):
- [Windows CEF + WebView2, Linux CEF](https://github.com/nssang00/chatgpt-write-test/actions/runs/38846688020)
- [macOS WKWebView ARM64 + Intel](https://github.com/nssang00/chatgpt-write-test/actions/runs/38846688063)

**확인된 사실:** 각 브라우저의 공통 공개 JS API에서 측정값과 결과
아티팩트를 생성했다. 64KiB 바이너리의 Windows WebView2 결과가
Windows CEF보다 현저히 크게 나타났다. WebView2에는 현재
`__nativeweb_binary` JSON 숫자 배열 인코딩이 남아 있으므로
바이너리 framing/IPC 최적화가 우선적 실험 후보이다.

**확정되지 않은 해석:** 별도 runner이므로 두 Windows 엔진의 동일
하드웨어 대조 값은 아니다. 인코딩/IPC/디코딩 시간의 정확한 기여도,
풀 baseline(1,000 warm-up / 5,000회 × 5세션), 단방향 편도시간,
Native→JS 반복 `execute` LAT-03, Python/C# worker 비용, 1MiB 이상
바이너리, 장기 p99 SLA는 **미측정**이다. CEF의 0B
`ArrayBuffer` ↔ `Binary` 변환은 첫 CI에서 `RefAnyCast` 실패가
확인돼 별도 기능 결함으로 추적하며 숫자에 포함하지 않았다.

**다음 우선순위:** 동일 Windows Runner에서 CEF↔WebView2 쌍비교,
WebView2 JSON 인코딩/IPC 분리 측정, batch 기반 짧은 RTT 보정,
Native→JS LAT-03 추가, 빈 버퍼 타입 수정 후 회귀, 반복 기준선 구축.

측정 코드:
[`tests/integration/cef/bridge_benchmark.html`](../tests/integration/cef/bridge_benchmark.html) ·
[`tools/run_bridge_benchmark.py`](../tools/run_bridge_benchmark.py).

## WebView2 base64 전송 최적화 — 예비 전/후 비교

적용: `src/browser/webview2/webview2_bridge_script.hpp`와
`src/core/json_codec.cpp` (`anyToJsonCompactBinary`).
JS→Native 및 Native→JS 메시지의 Binary를 base64 문자열 marker
`__nativeweb_binary_base64`로 전달한다. 기존 숫자 배열
`__nativeweb_binary`는 수신 호환성을 유지한다. Core·CEF·managed
worker의 기존 `anyToJson` 직렬화는 변경하지 않는다.

Windows WebView2, GitHub-hosted Runner, Release 빌드, 64KiB 40표본,
1KiB 100표본 기준 각 **독립 PR CI 실행**의 p50 / p95 값:

| 시나리오 | 변경 전 (`3c45b72`) | Base64 (`f661d2c`) | p50 감소 |
| --- | ---: | ---: | ---: |
| 64KiB Binary RTT | 36.500 / 37.230 ms | 9.400 / 9.710 ms | **74.2%** |
| 1KiB Binary RTT | 0.800 / 0.900 ms | 0.300 / 0.405 ms | **62.5%** |
| 작은 `math.add` RTT | 0.200 / 0.305 ms | 0.200 / 0.300 ms | 변화 불분명 |

증거:
- [변경 전 실제 WebView2 smoke 아티팩트](https://github.com/nssang00/chatgpt-write-test/actions/runs/38846688020)
- [Base64 변경 후 실제 WebView2 smoke 아티팩트](https://github.com/nssang00/chatgpt-write-test/actions/runs/38851250903)

**해석:** 새 전송 포맷이 64KiB JSON 배열 대비 더 낮은
왕복시간을 보였고, 기존 WebView2 실제 브라우저 E2E 4개도 통과했다.
다만 **동일 장비에서 짝지어 반복한 측정이 아니다**. CI Runner 부하와
실행 간 변동이 포함된 만큼 절대 성능 개선률이나 SLA를 확정하지 않는다.

**후속:** 동일 Windows Runner 쌍비교, encode/decode/IPC 구간별 프로파일,
64KiB 이상의 1MiB 검증, `ICoreWebView2SharedBuffer` 기반 제로카피
전송 여부 별도 실험. **Base64는 여전히 복사·문자열 인코딩 비용이 있다.**

## 같은 Windows Runner의 엔진 비교와 SharedBuffer 실험 (2026-10-16)

### 기존 JS→Native→JS RPC 성능: CEF와 WebView2, 동일 Runner

`windows-2022` Runner 한 대에서 두 엔진을 빌드한 뒤,
실행 순서를 바꿔 3회씩 반복했다(작은 호출 300표본,
64KiB는 40표본/실행). 값은 각 실행 p50의 **중앙값**.

| 3회 묶음 | CEF tiny RPC | WebView2 tiny RPC | CEF 64KiB RPC | WebView2 64KiB RPC |
| --- | ---: | ---: | ---: | ---: |
| [실행 38863601576](https://github.com/nssang00/chatgpt-write-test/actions/runs/38863601576) | 0.30 ms | 0.20 ms | 0.70 ms | 8.60 ms |
| [실행 38864908293](https://github.com/nssang00/chatgpt-write-test/actions/runs/38864908293) | 0.20 ms | 0.20 ms | 0.70 ms | 9.40 ms |

→ 같은 Windows VM, 동일 앱 코드/데이터 조건에서도 WebView2의
64KiB 복사·Base64 왕복 비용이 CEF보다 크다. 다만 이것만으로
각 엔진의 모든 앱/전송 방식을 일반화하지 않는다.

### **다른 지표**: 같은 WebView2 문서의 Native→JS 전송

`src/browser/webview2/webview2_backend.cpp` 실험용
`postReadOnlySharedBufferProbe()`에서 Native가 버퍼를 준비해
읽기 전용 SharedBuffer로 보내는 경로와, `postBridgeMessage()`의
Base64 바이너리 이벤트 경로를 비교했다. JS→Native 제어 이벤트,
바이트 패턴, 전체 내용 검증은 공통이며, 타이머 종료는
**JS 수신 직후**다. 검증 루프 시간은 측정에서 제외했다.

| 크기 | 시나리오별 표본 | Base64 p50 / p95 | SharedBuffer p50 / p95 |
| --- | ---: | ---: | ---: |
| 1KiB | 25+25 | 15.50 / 16.06 ms | 15.60 / 17.42 ms |
| 64KiB | 20+20 | 21.30 / 21.81 ms | 12.15 / 15.41 ms |
| 1MiB | 8+8 | 112.30 / 120.45 ms | 9.95 / 16.49 ms |

[실측/원본 JSON/매칭 실행 아티팩트](https://github.com/nssang00/chatgpt-write-test/actions/runs/38864908293).
**모든 바이트 정확성 검증 및 SharedBuffer JS 해제 53회 성공**.
단발 실행이며 작은 버퍼는 전달 고정비가 지배적이다.

두 표는 서로 다른 계측이다. 첫 번째는 `bench.echo`의 전체
JS→Native→JS 함수 왕복. 두 번째는 JS 제어 이벤트→Native 버퍼
할당/채우기→JS 수신까지의 **단일 전송 실험**이다.
SharedBuffer에도 Native→공유 메모리로의 복사가 1회 존재하므로
`zero-copy end-to-end`라고 표기하지 않는다. 운영용 API가 아닌
별도 시험용 내부 백엔드 메서드이며, 실제 배포 자동 전환은 미완료다.

**다음 승인 조건:** 문서 전환·same-origin reload·iframe 권한,
리소스 해제 누락/스크립트 타임아웃/크래시, backpressure,
메모리 압력, 사용자 콘텐츠 격리, 큰 메시지 크기 한계 및
보호 메모리 접근 동작을 실제 Runner에서 확인해야 한다.
향후 숫자는 엔진/Runner 버전, 독립 반복 세션과 함께 검증한다.
