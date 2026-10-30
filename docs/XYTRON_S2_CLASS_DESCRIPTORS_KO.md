# Xytron S2-3 — 관리형 클래스 메타데이터와 객체 호출 계약

기준 브랜치: `nativeweb/autonomous-integration`; 함수 메타데이터 S2-2 스키마와 기존 ABI v1 유지.

## 이번 단계에서 구현한 범위

- Python `@export` 클래스: AST만 분석하며 **플러그인 코드를 import하거나 실행하지 않음**.
- C# `[Export]` 클래스: **신뢰한 로컬 빌드 어셈블리**만 .NET Reflection으로 분석 (`xytron bindings generate --trusted`).
- 클래스 생성자, 명시적으로 export한 인스턴스 메서드, 읽기/쓰기 프로퍼티 타입을 `exports.json`의 선택적 `classes` 배열에 출력. 기존 `functions` 배열 유지.
- `index.d.ts`에 생성자 팩토리, 메서드, 프로퍼티 get/set, dispose 선언 자동 생성.
- `index.mjs`에 비동기 객체 호출 래퍼 생성. 핸들은 클라이언트에서 비공개 필드로 보관하고 dispose 후 호출 거부.
- Python SDK 및 C# `ExportDiscovery`도 **공개 멤버 자동 노출을 금지**하고 명시적 export만 표시하도록 일치시킴.

## 개발자 예시

```python
from xytron import export

@export
class Player:
    def __init__(self, name: str) -> None:
        self.name = name

    @export
    def attack(self, amount: int) -> int:
        return amount

    @property
    @export
    def health(self) -> int:
        return 100

    @health.setter
    def health(self, value: int) -> None:
        pass
```

C#은 클래스와 공개 생성자, 공개 메서드/프로퍼티에 `[Export]`를 붙인다. 사용자 템플릿에서 어트리뷰트 대상에 `Constructor`와 `Property`를 추가했다.

생성되는 JS 호출 계약 예시 (`pluginId = game`):

```
game.Player.create(name)                 -> opaque string handle
game.Player.method.attack(handle, n)      -> Promise<number>
game.Player.get.health(handle)            -> Promise<number>
game.Player.set.health(handle, n)         -> Promise<void>
game.Player.dispose(handle)              -> Promise<void>
```

위 호출 명칭은 `globalThis.xytron.invoke(name, ...args)`에서 사용하는 **제안 프로토콜**이다. 현재 Python/.NET 객체 호스트는 이 메서드들을 처리하지 않으며, 생성된 래퍼만으로 실제 객체가 실행되는 것은 아니다.

## 보안/제약

- 명시적 `@export`/`[Export]`가 없는 인스턴스 메서드·프로퍼티는 스키마에서 제외된다.
- 클래스 팩토리는 반환한 핸들이 비어 있지 않은 문자열인지 JS에서 확인한다. 이것은 **보안 경계가 아니다**. 호스트는 별도로 출처/프레임/권한 및 객체 소유자와 타입을 검증해야 한다(NW-014).
- `dispose()`는 래퍼에서 재호출을 방지하지만, 실제 관리형 객체 정리, 충돌 시 회수, `IDisposable`/Python context cleanup은 미구현이다.
- 지원 타입: 기본 scalar, `Uint8Array`, `void`; 동기 메서드만. 오버로드/제네릭/기본값/정적 클래스 메서드/복합 객체/비동기는 아직 미지원이다.
- C# Reflection은 로컬 어셈블리를 로드하므로 **외부에서 받은 바이너리를 신뢰 옵션으로 분석하지 않는다**.
- 생성된 JS는 실제 Browser E2E 대신 Node mock bridge로 계약을 테스트한다. 기존 Browser E2E 결과를 관리형 객체 실행의 성공으로 간주하지 않는다.
- 기존 생성물 덮어쓰기는 금지하고, 변경 후 깨끗한 디렉터리로 재생성해야 한다.

## 검증 기준

- `ctest` 전체 Core/Plugin 30개 회귀
- `test_managed_bindings.py` 기존 함수 메타데이터 회귀
- `test_class_bindings.py` Python AST 무실행, 기본 생성자/메서드/프로퍼티, 잘못된 타입 차단, ZIP 포함, Node mock 핸들/해제, C# .NET 8 Reflection 테스트
- `bindings/python/tests` `@export` 명시성 검사
- `bindings/dotnet/Tests` `ExportDiscovery` 명시성 검사
- Windows/Linux GitHub Actions에서 실제 .NET 8 컴파일 및 검증

## 다음 단계

1. Python/.NET 언어별 **호스트 프로세스**에서 opaque handle 생성·보관·해제 구현
2. NW-014 출처/프레임 권한 및 인스턴스 소유자/프로세스 격리
3. WebView2/CEF에서 실제 JS→객체 생성/메서드/프로퍼티/해제 E2E
4. PluginManager 자동 로딩·언로드와 busy object 수명 관리

## S2-4 실험적 Python 객체 호스트 (2026-10-15)

- `bindings/python/xytron/object_host.py`에 **독립 프로세스 연결 전 단계의 인프로세스 호스트**를 추가했다.
- 명시적 `@export` 클래스/멤버만 접근 가능하며, 생성·메서드·get·set·dispose를 S2-3 호출 규약과 동일한 이름으로 처리한다.
- `secrets.token_urlsafe` 기반 예측 불가능한 객체 핸들, **브릿지가 발급한 소유자 ID**별 검증, 객체 수 제한, 중복 해제 거부 및 소유자 연결 종료 시 일괄 정리를 구현했다.
- `authorize(owner, operation)` 콜백을 필수로 요구하며, 허용 결정이 명시적 `True`가 아니면 거부한다. 이 콜백은 **NW-014 권한 시스템을 구현한 것은 아니다.** 프레임/출처 신원 확인과 권한 판정은 실제 네이티브 브릿지가 수행해야 한다.
- `close()` 구현 시 dispose/cleanup에서 호출하되, 해당 메서드는 JS로 직접 export하지 않는다.
- `python bindings/python/tests/test_object_host.py`의 5개 테스트를 Windows/Linux CLI CI에 연결했다.
- **주의:** 현재 프로세스 간 IPC, 실제 브라우저 호출, Python 호스트 프로세스 배포, .NET 객체 호스트는 여전히 미구현이다. 객체 핸들을 브라우저에서 받아 그대로 소유자 ID로 사용하면 안 된다.
