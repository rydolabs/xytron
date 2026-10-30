# Xytron S2-5: 관리형 객체 호스트 별도 프로세스 IPC 실험

## 목적 및 실제 완료 범위

이 단계는 S2-3의 JS 객체 래퍼 호출 규약과 Python/.NET `ObjectHost`를 **별도 자식 프로세스**에서 연결하는 작은 수직 통합이다. 기존 C++11 Core, C ABI v1, `new/build/pack` CLI를 변경하지 않는다. 모든 작업은 `nativeweb/autonomous-integration` Draft PR에서 검증한다.

**포함:** Python 3 표준 라이브러리 JSON Lines 작업자, .NET 8 JSON Lines 작업자, 기동/응답/EOF 소유 객체 정리, 한 세션당 하나의 worker, 잘못된 프레임 거부, `index.mjs`를 사용한 JS → 실제 Python worker 테스트, Windows/Linux CI.

**제외:** Xytron C++ Bridge와의 통합, 브라우저 프로세스/프레임 식별, NW-014 출처·권한 검증, 신뢰하지 않는 플러그인 격리, WebView2/CEF E2E, PluginManager, 자동 로딩/설치. 이 단계를 통과해도 관리형 브릿지가 완성된 것은 아니다.

## 통신 규약 (실험적 v0, 정식 안정 ABI 아님)

- 전송: 신뢰한 부모가 생성한 자식 프로세스의 **전용 stdin/stdout 파이프**만 사용한다. 포트/소켓을 열지 않는다.
- 프레이밍: 줄당 UTF-8 JSON 객체 1개, 최대 64KiB. 멀티라인 요청, 중복 필드, 너무 큰 요청, 추가 필드, JSON 객체/배열 인자는 거부한다.
- 요청: `{"id":1,"operation":"example_py.Player.create","args":["Ada"]}`. `id`는 양의 32비트 정수로 순증가. 요청은 `owner`, `origin`, `frameId`, 권한 플래그를 지정할 수 없다.
- 성공: `{"id":1,"ok":true,"result":"xy_..."}`.
- 거부: `{"id":1,"ok":false,"error":"Rejected"}`. 잘못된 프레임의 `id`는 `null`. 예외 타입/메시지/traceback은 stdout 응답에 직렬화하지 않는다.
- 객체 작업: `{plugin}.{class}.create`, `.method.{name}`, `.get.{name}`, `.set.{name}`, `.dispose`. 이미 존재하는 클래스 바인딩 생성기의 호출명과 동일하다.
- 소유권: **프로세스 1개 = 신뢰한 세션 1개**의 첫 구현. 부모가 프로세스를 시작할 때 부여한 owner 값만 사용하며 프런트엔드에서 받은 owner를 전달하지 않는다. 워커는 프로세스 내 `ObjectHost`의 소유권·타입·`[Export]`/`@export` 검증을 재사용한다.
- 종료: stdin EOF면 모든 객체 핸들을 무효화하고 Python `close()` / .NET `IDisposable.Dispose()`를 호출한다. 정상 종료와 비정상 종료에 대한 부모의 프로세스 강제 종료·재시작 정책은 아직 없다.
- 표준 출력: IPC 전용. 일반 Python `print()` 및 .NET `Console.WriteLine()`은 stderr로 전환해 단일 JSONL 채널을 보전한다. 신뢰하지 않는 모듈은 OS 레벨 stdout 쓰기·임의 파일/네트워크 접근이 가능하므로 **sandbox가 아니다**.

## 실행 및 테스트

Python 워커 개발 예시 (신뢰한 로컬 코드 한정):

```bash
PYTHONPATH=bindings/python:tests/ipc python -m xytron.host_process \
  --plugin-id example_py --module python_fixture --class Player --owner session_1
```

.NET 워커 테스트 프로젝트:

```bash
dotnet build bindings/dotnet/HostProcessDemo/HostProcessDemo.csproj -c Release
dotnet bindings/dotnet/HostProcessDemo/bin/Release/net8.0/HostProcessDemo.dll
```

회귀 테스트:

```bash
python tests/ipc/test_host_process.py
python tests/ipc/test_host_process.py --dotnet-dll bindings/dotnet/HostProcessDemo/bin/Release/net8.0/HostProcessDemo.dll
```

테스트 항목: 실제 subprocess 생성/메서드/get/set/dispose, EOF 정리, 소유권 격리, export되지 않은 멤버·잘못된 타입 거부, 임의 owner 필드/중복 요청 ID/중복 JSON 키 거부, 과대 프레임 차단, JS/TS 자동 생성 래퍼 → Node 임시 브로커 → Python 프로세스의 end-to-end 호출.

## 다음 작업: 네이티브 브릿지 통합 전제 조건

1. **NW-014:** C++ 네이티브 브릿지가 엔진 WebView2/CEF에서 프레임/문서 출처를 신뢰 가능한 방식으로 식별하고 명시적으로 권한 허용한 호출만 브로커에 전달한다. 페이지 제공 `owner`나 JSON 메타데이터는 신뢰하지 않는다.
2. **C++ ProcessBroker:** 플랫폼별 자식 기동·파이프·대기 시간·크래시 감지·응답 ID 검증·세션별 수명 종료·요청 수 제한. 권한 정책과 프로세스 관리는 Core 측에서 담당한다.
3. **실제 브라우저 E2E:** 허용 프레임의 생성→메서드→프로퍼티→dispose 성공, 금지 프레임/내비게이션 후 호출·재사용 핸들 차단, 서브프레임·프로세스 재시작 검증.
4. **격리/신뢰:** 플러그인 코드가 임의로 네이티브 API에 접근하므로 프로세스 분리는 보안 sandbox가 아니다. 외부 플러그인은 별도 서명/권한/OS 격리 정책을 마련할 때까지 실행하지 않는다.
