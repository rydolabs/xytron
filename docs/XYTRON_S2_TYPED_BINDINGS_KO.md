# Xytron S2-1: 네이티브 함수 스키마 및 JS/TS 바인딩

기준: S1 (`fbe2934`) 후속 작업. C++11 Core 및 NativeWeb C ABI v1 호환성을 유지한다.

## 1. 이번 구현 범위

- 기존 `nw_host_api_v1`의 **끝에** `register_typed_function` 콜백을 추가한다. ABI 버전을 올리지 않고 `struct_size`를 검사하는 선택적 확장이다.
- C++ `m.function()` / `PluginApi::bind()`의 `CallableSignature`를 C ABI 경계에서 복사해 Core의 `BindingRegistry`까지 전달한다.
- 새 메타데이터가 있는 함수 호출은 **플러그인 코드 실행 전에** 인자 개수와 타입을 검증한다. 잘못된 인자는 `invalid_arguments` 오류를 발생시킨다.
- 기존 등록 콜백만 제공하는 ABI v1 플러그인은 동작을 유지한다. 이 함수는 서명이 `Unknown`이며 타입 자동 생성 대상에서 제외한다.
- C++ 네이티브 플러그인은 로컬에서 빌드한 신뢰 가능한 DLL/SO에서 서명을 발견해 다음을 생성한다.
  - `bindings/exports.json`: 공통 스키마 **초안** (`schemaVersion: 1`, `runtime: native`, `pluginId`, 함수별 인자·반환 타입)
  - `bindings/index.d.ts`: TypeScript `Promise<T>` 함수 선언
  - `bindings/index.mjs`: `globalThis.xytron.invoke('plugin.method', ...)` JS 래퍼
- `xytron pack`은 3개 생성 파일이 모두 존재할 때만 `.xplugin`에 포함한다. 일부 파일만 있으면 실패한다.

## 2. 현재 지원 타입

| C ABI 타입 | JSON/TS | 런타임 검증 |
|---|---|---|
| BOOL | boolean | bool |
| INT32 | number | int |
| DOUBLE | number | double |
| STRING | string | std::string |
| BINARY | Uint8Array | nativeweb::Binary |
| 결과 NULL | void | 인자에는 불가 |

`List`, `Dictionary`, `NativeObject`, 미상 타입과 사용자 정의 구조체는 **현재 자동 스키마 대상이 아니다**. 함수 등록 자체는 레거시 방식으로 되돌아가며 잘못된 타입 선언을 출력하지 않는다. 인자 이름은 아직 C ABI로 전달하지 않으므로 `arg0`, `arg1`로 생성한다. C++의 `long`, `float` 등 플랫폼별 타입 폭과 실제 Any 변환의 일관성은 별도 개선 대상이다.

## 3. 사용 예 (C++ 로컬 개발)

```bash
xytron new plugin camera --lang cpp
cd plugins/camera
xytron build --sdk /path/to/installed/NativeWeb
xytron bindings generate --library build/libcamera.so --out bindings --trusted
xytron pack
```

Windows에서는 라이브러리 예제가 `build/Release/camera.dll`이다. 네이티브 함수 변경 후에는 반드시 `build`와 `bindings generate`를 다시 실행해야 한다. 현재 `bindings generate`는 기존 파일 덮어쓰기를 거부한다. 재생성 전에는 생성 디렉터리를 명시적으로 삭제해야 한다.

**보안:** `--trusted`는 로컬 개발자가 해당 플러그인 바이너리를 신뢰하며 **로드·초기화 코드 실행을 허가**했다는 뜻이다. 다운로드한 임의의 `.xplugin`을 스키마 추출 목적으로 실행해서는 안 된다. 이 명령은 샌드박스가 아니며, 권한 정책 검증(NW-014)도 우회할 수 있으므로 개발용으로 한정한다. 최종 사용자 앱에서는 플러그인 신뢰·권한·출처 정책이 별도로 필요하다.

## 4. 검증 및 아직 미완성인 부분

로컬 CMake Core/Plugin/CLI 회귀 테스트 30개 통과. 추가된 테스트:

- `plugin.typed_signature`: C++ 타입 전파, 정확한 실제 호출, 인자 개수·타입 차단, 기존 ABI 레거시 호출 성공
- `plugin.typed_bindings`: JSON/TS/JS 생성, 기존 파일 덮어쓰기 차단, `--trusted` 필수, 레거시 미타입 함수에 대한 안전한 실패
- 생성한 실제 C++ 플러그인에서 바인딩 생성 후 `.xplugin` 패키징, 독립 ZIP 검사

남은 과제: Python/.NET 실제 언어 호스트 및 자동 스키마, 클래스·객체/프로퍼티·비동기 함수 모델, 이름 있는 파라미터, PluginManager 자동 로드, NW-014 호출 권한 검증, 디지털 서명과 재현 가능한 패키지 검증, 실제 3개 브라우저 엔진에서 자동 생성 래퍼 E2E. 이번 S2-1만으로는 위 기능이 완성됐다고 보지 않는다.
