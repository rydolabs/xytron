# Xytron S2-2: Python·.NET 공통 함수 메타데이터 및 JS/TS 생성

기준: S2 C++ ABI v1 typed binding contract (`schemaVersion: 1`) 유지.

## 목표 및 검증 범위

- `xytron new plugin NAME --lang python|csharp` 템플릿을 개발자가 직접 수정한다.
- `xytron build`로 Python 구문 검사 또는 .NET 8 컴파일을 수행한다.
- `xytron bindings generate --out bindings`로 Python 소스를 **import/실행 없이** AST 파싱하고, `@export` 최상위 동기 함수 정보를 추출한다.
- C#은 `xytron bindings generate --out bindings --trusted`로 **명시적으로 신뢰한** 로컬 빌드 어셈블리를 Reflection으로 분석한다. .NET 코드를 로드하므로 외부 플러그인/다운로드 파일에 사용하지 않는다.
- 결과 `exports.json`, `index.d.ts`, `index.mjs`를 `xytron pack`에서 `.xplugin`에 포함한다.

## 명령 예시

```bash
xytron new plugin myplugin --lang python
cd plugins/myplugin
xytron build
xytron bindings generate --out bindings
xytron pack
```

C#은 `--lang csharp`, `bindings generate --out bindings --trusted`를 사용한다. 현재 명령은 플러그인 디렉터리에서 실행해야 한다.

## JSON 형식

```json
{
  "schemaVersion": 1,
  "runtime": "python",
  "pluginId": "myplugin",
  "functions": [
    {
      "name": "hello",
      "arguments": ["string"],
      "parameterNames": ["name"],
      "returns": "string"
    }
  ]
}
```

C++ S2-1 스키마와 `functions[].arguments`/`returns` 포맷을 공유하며, Python/.NET에서는 `parameterNames`가 추가된다. 기존 네이티브 생성기는 `arg0` 같은 기본 명칭을 사용한다.

지원 값: `number`, `boolean`, `string`, `Uint8Array`, 반환의 `void`. Python은 `str/int/float/bool/bytes/None`, C#은 지원 가능한 기본 primitive 타입을 사용한다.

## 명시적 제한

- 클래스·메서드·인스턴스 핸들, 제네릭, 오버로드, 기본 매개변수, 비동기 함수는 아직 지원하지 않으며 오류 처리한다.
- Python `int`와 C# `long`을 JS `number`로 매핑한다. **2^53-1보다 큰 정수의 정밀도 및 실제 Bridge 전달 검증은 아직 보장되지 않는다.** 추후 BigInt 및 범위 검증 계약이 필요하다.
- `@export`는 정적 형태만 해석한다. 동적으로 생성되는 export/alias는 자동 탐색하지 않는다.
- **실제 WebView2/CEF에서 Python/.NET 실행되는 브릿지, PluginManager, NW-014 권한 모델은 미구현**. 현재 JS 래퍼는 기존 `globalThis.xytron.invoke(...)` 계약을 이용하는 생성 코드다.
- `bindings/` 재생성은 파일 덮어쓰기를 거부하므로 사용자/CLI가 과거 생성물 삭제 후 다시 실행해야 한다. 패키징 과정에서 소스 수정 후 생성물 노후 여부를 자동 검출하는 기능은 아직 없다.
- CLI 보조 생성 도구(`generate_managed_bindings.py`, C# reflector)는 CLI 설치 시 `share/xytron/cli/`에 함께 배포한다. **C# 메타데이터 생성에는 .NET 8 SDK만 필요**하고 Python 메타데이터 생성에는 Python 3이 필요하다. Xytron CLI 자체는 C++11로 독립 빌드된다.

## 검증

- `tools/cli/test_managed_bindings.py`: 무실행 Python AST, 누락 타입·중복 Export·클래스·미지원 타입 거부, 재생성 덮어쓰기 거부, ZIP 포함 여부 확인.
- .NET 8 설치된 CI 환경에서 C# Reflection, 실제 어셈블리 빌드, 패키징 및 오버로드 거부를 검증한다.
- 플랫폼별 `xytron-cli.yml` Ubuntu/Windows 잡에 추가한다.
