# Xytron S2-6: C++11 ProcessBroker의 검증 범위

## 목표와 구현

`src/core/process_broker.hpp/.cpp`에 엔진 독립적인 실험용 네이티브 브로커를 추가한다.
**최신 C++11 Core 및 C ABI v1 변경 없음.** 브로커는 기존 Python/.NET S2-5 JSONL 작업자를
**신뢰한 로컬 네이티브 부모**로부터 전용 stdin/stdout 파이프로 호출한다.

- C++ `Any`/`VariantList` → UTF-8 JSONL 인자 직렬화. `null`/`bool`/`int`/유한 `double`/`std::string`만 허용. 배열/객체/바이너리 타입은 **이 실험적 IPC 규약에서 거부**한다.
- `id`는 프로세스당 단조 증가 정수. 응답의 `id`, `ok`, 허용된 키 집합, 결과 타입 검사. 중복 JSON 필드 거부. 올바르지 않은 응답/응답 ID 불일치/타임아웃/EOF는 세션 종료.
- 최대 64KiB 요청/응답, 최대 32개 인자, 10~60000ms의 호출 제한, 메서드별 일반 오류 응답은 `worker_rejected`로 변환해 자식 내부 오류 문자열을 UI로 전달하지 않음.
- POSIX: fork/execvp + private pipes, poll 마감시간, 비차단 입력 쓰기, SIGPIPE 차단, 종료 시 자식 회수. Windows: CreateProcessW + private pipes, PeekNamedPipe 기반 응답 제한, 종료 시 프로세스 회수.
- `stop()` / 소멸 시 세션 프로세스를 닫음. 같은 브로커의 호출은 mutex로 직렬화된다. Core WorkerPool과 함께 사용할 때 **WebView UI 스레드를 기다리게 해서는 안 된다.**
- 브로커는 공개 WebView 바인딩 또는 브라우저 요청 라우팅에 아직 연결하지 않았다. 직접 `ProcessBroker::invoke` 하는 C++ 호출자는 **신뢰한 네이티브 코드에 한정**한다.

## 테스트

`core.process_broker`는 실제 별도 Python 프로세스를 생성하여 생성→메서드→프로퍼티→dispose,
유한 숫자/문자열/불리언/null 직렬화, 비허용 멤버 거절,
프로토콜 오류(중복 키·ID·잘못된 타입), 타임아웃/사망, 과대 인자 거부를 검증한다.
Windows/Ubuntu CLI CI는 같은 C++ 실행 파일을 **실제 .NET 8 작업자**와도 연결한다.

로컬 실행 예시:

```bash
cmake -S . -B build -DNATIVEWEB_BUILD_TESTS=ON
cmake --build build --target nativeweb_process_broker_regression
ctest --test-dir build -R '^core.process_broker$' --output-on-failure
```

## 완료라고 주장하지 않는 범위

1. **NW-014 엔진 권한/출처 연결은 미완료**: CEF/WebView2가 검증한 문서·프레임·내비게이션 신원을 native broker에 제공하는 경로가 없다. JSON body 안의 owner/origin은 신뢰하지 않는다.
2. **실제 관리형 브라우저 E2E 미완료**: 이 테스트는 C++ native parent → Python/.NET subprocess까지만 확인한다.
3. **악성 플러그인 sandbox 아님**: 임의 native/file/network 접근을 OS 정책으로 격리하지 않는다. 승인된 로컬 코드 외에는 사용 금지.
4. **Windows 입력 파이프의 하드 쓰기 마감시간 미보장**: 응답 대기는 제한되지만 blocking `WriteFile`은 불응답 자식의 파이프가 가득 차면 별도 OS overlapped-pipe 구현 전까지 차단될 수 있다.
5. **재시작/자동 취소/백프레셔/다중 요청 병렬화 미구현**: 한 프로세스에서 순차적으로 호출하며, 오류 발생 시 프로세스를 닫는다.

다음: 권한 정보가 **엔진에서 검증된 실제 객체**로 Core로 전달되도록 이벤트 계약을 변경하고,
서브프레임/내비게이션 변경을 허용목록과 세션 종료에 연결한 후,
CEF·WebView2 양쪽에서 실제 Python/.NET 객체 호출을 브라우저 E2E로 검증한다.
