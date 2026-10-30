# Xytron S2 — .NET 객체 호스트 (실험적)

## 설계

Python 인프로세스 `ObjectHost`와 같은 명령 이름을 사용하는 .NET 8의 실험적 호스트입니다. 기존 C++ ABI 및 `dotnet_reflector` 생성 코드는 변경하지 않습니다.

| 작업 | 브릿지 명령 | 제한 |
|---|---|---|
| 생성 | `plugin.Class.create` | `[Export]` 생성자 또는 public 기본 생성자 |
| 메서드 | `plugin.Class.method.Method` | `[Export]` public 인스턴스 메서드 |
| 프로퍼티 | `plugin.Class.get.Property`, `plugin.Class.set.Property` | `[Export]` 프로퍼티의 public 접근자 |
| 해제 | `plugin.Class.dispose` | 핸들 무효화 후 `IDisposable.Dispose()` |
| 연결 종료 | `host.Cleanup(trustedOwner)` | 해당 소유자 객체 전부 정리 |

핸들은 암호학적 난수로 생성하며 한 소유자/클래스에 묶입니다. `authorize(owner, operation)` 콜백이 필수이고 호출마다 확인합니다. 타입·인자 개수·JSON primitive 유효성 및 자바스크립트 안전 정수 범위를 검사합니다. 기본 최대 객체 수는 1024개입니다.

## 테스트

```bash
dotnet run --configuration Release --project bindings/dotnet/HostTests/ObjectHostSmoke.csproj
```

Windows와 Ubuntu의 CLI GitHub Actions에 별도 단계로 등록했습니다. 생성/호출/읽기·쓰기/소유자 격리/허용 거부/비노출 멤버 거부/인자 검증/객체 제한/IDisposable/연결 종료 정리를 테스트합니다.

## 보안 및 미완료 범위

**이 코드는 별도 호스트 프로세스도, 브라우저 IPC도, .NET 샌드박스도 아닙니다.** 생성자와 Reflection 메서드는 호스트 프로세스의 권한으로 실행되므로 신뢰한 플러그인 코드만 로딩해야 합니다.

- `owner`는 JavaScript의 일반 호출 인수로 받거나 신뢰하지 않습니다. 실제 네이티브 브릿지가 프레임·세션을 인증해 발급해야 합니다.
- 현재 콜백은 실제 NW-014 출처/프레임 권한 시스템과 통합되어 있지 않습니다. 따라서 NW-014 완료로 간주하지 않습니다.
- 실제 프로세스 간 IPC 인증 및 메시지 제한, 별도 .NET/Python 워커 관리, UI 스레드 비차단, 취소/타임아웃, PluginManager 언로드, WebView2/CEF 관리형 객체 E2E는 다음 단계입니다.
- ObjectHost의 잠금은 객체 수명 보호를 위해 호출 전체를 직렬화합니다. 장시간 작업은 향후 워커 프로세스에서 분리해 처리해야 합니다.
