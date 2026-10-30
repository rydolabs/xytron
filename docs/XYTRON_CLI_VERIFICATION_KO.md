# Xytron CLI 1차 구현 테스트 기록

실행일: 2026-10-14
테스트 환경: Linux x86-64, CMake, GCC/G++ 14, Python 3, .NET SDK 없음
기준: handoff SHA `02716552ade37e2b1be1c460d7f6d8ab1d328013`

## 자동 회귀

1. `cmake -S . -B build -DNATIVEWEB_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build -j6`
3. `ctest --test-dir build --output-on-failure -j4`
   - **28/28 PASS** (Core/Plugin/CLI 스캐폴드)
4. `cmake --install build --prefix <local-sdk>`
5. `cmake -E chdir build/xytron-cli-smoke <xytron> plugin build camera --sdk <local-sdk>`
   - **PASS**, 실제 `libcamera.so` 생성
6. `<nativeweb_generated_xytron_plugin_regression> <camera.so>`
   - **PASS**, 바이너리에서 ABI v1 등록·실제 함수 호출
7. `python tools/cli/test_artifacts.py --xytron <xytron> --repo . --sdk <local-sdk>`
   - **9개 통합 항목 PASS**, 아래 범위

## 플러그인 템플릿/패키지 테스트

| ID | 검증 | 결과 |
|---|---|---|
| T01 | `new app` 기본 C++ Main | PASS |
| T02 | Python 템플릿 `@export` 실제 호출, 메타데이터 조회 | PASS |
| T03 | Python 소스 수정·추가 후 재패키징, ZIP CRC·내용 일치 | PASS |
| T04 | 로컬 `publish`, 동일 파일 비교, 덮어쓰기 차단 | PASS |
| T05 | JSON 문법 오류 / 지원하지 않는 `runtime` 거부 | PASS |
| T06 | Python 구문 오류를 `xytron build`에서 거부 | PASS |
| T07 | 소스 디렉터리 심볼릭 링크 외부 참조 거부 | PASS |
| T08 | 무빌드 C++ 패키지 거부, 실제 빌드 후 ZIP 생성 | PASS |
| T09 | C# 템플릿·매니페스트 생성 | PASS (빌드 SKIP) |

별도 `tools/cli/test_xytron.cmake`는 생성 산출물 유무, 세 언어의
`runtime`/`entry`, 앱 기본값, 명령 실패 코드, 중복 생성, 비정상 SDK 경로,
패키지 덮어쓰기 거부까지 검증한다.

## 아직 검증하지 못한 내용

- **C# 실제 빌드**: 로컬 머신에 .NET SDK가 없으므로 `dotnet build`, Reflection 소비자 실행은 미검증. GitHub Actions `xytron-cli.yml`에 `actions/setup-dotnet@v4`와 C# 빌드/소비자 검증 단계를 추가했으나 **원격 실행 결과는 아직 없음**.
- **Windows/MSVC 빌드**: 현재 변경분에 대한 Windows CI 확인 전.
- **브라우저 E2E**: 이번 테스트는 기존 Core와 ABI 및 생성된 플러그인에 한정. CEF/WebView2에서 새 Python/C# 호스트를 호출하지는 못함.
- **패키지 설치/검증**: `.xplugin`은 ZIP 형태로 생성되지만, 실제 PluginManager의 설치 및 런타임 로딩 경로에 아직 연결되지 않음.
- **의존성 재현성/서명**: 외부 NuGet/Python/C++ 네이티브 종속성을 모두 잠그거나 번들링하지 않으며 서명 검증은 미구현.

이 테스트 기록은 제공된 1차 구현에 대한 것이며 정식 릴리스 승인 문서는 아니다.

## 2026-10-15 S1 검증 강화 (별도 로컬 패치, GitHub 미반영)

- JSON 문법 확인 후 최상위 구조 필드를 파싱하도록 `tools/cli/json_fields.hpp` 추가.
  정규식 기반 필드 검색을 제거해 중첩된 `schemaVersion`, `entry` 및 중복 JSON 키로 인한
  오인식을 방지한다. `schemaVersion`은 최상위 JSON 숫자 `1`이어야 한다.
- `tools/cli/verify_artifact.py`를 독립 소비자 검사기로 추가했다. ZIP 파일명과 `runtime`
  진입점, 저장 방식, CRC, 경로 탈출, 이름 충돌, Symlink, 파일 개수/크기를 검사한다.
  아카이브 내부 코드는 **실행하지 않는다**.
- 생성 패키지를 사전 검사 후 새 디렉터리에 추출해 독립 Python 프로세스에서 수정된
  `@export` 함수를 import·호출하는 테스트를 추가했다.
- 직접 지정되는 `plugin.json`, `.dll/.so`, `.csproj` 빌드 결과 등 패키지 입력 파일도
  Symlink/Reparse Point를 거부한다.
- `xytron-cli.yml`에 Python 3.11 환경 준비 단계를 추가했다. 기존 .NET 8 SDK 단계와
  Windows/Linux 매트릭스는 유지하지만 **수정 소스가 아직 원격 브랜치에 없어 새 CI 실행 결과가 없다**.

실행 환경: Linux x86_64, CMake, GCC/G++ 14, Python 3.13, .NET SDK 없음.

```bash
cmake -S . -B build -DNATIVEWEB_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j6
ctest --test-dir build -j4 --output-on-failure  # 28/28 PASS
cmake --install build --prefix /tmp/xytron-sdk
python tools/cli/test_artifacts.py --xytron /path/to/build/xytron \
  --repo . --sdk /tmp/xytron-sdk       # 10 PASS, C# SKIP
```

검증 범위: C++ 실제 빌드 `.so` 아카이브 검사, Python 아카이브 소비자 실행,
나쁜 매니페스트·ZIP·Symlink 거부. .NET 컴파일 및 Windows CI는 계속 미검증.

주의: ZIP CRC는 우발적 파일 손상 검사이지, 악의적 재패키징의 진위 검증이 아니다.
플러그인 코드 서명·키 배포·권한 정책·설치/실행은 아직 없음.

추가 네이티브 실물 소비자 검증:

1. `xytron new plugin camera --lang cpp`로 새 프로젝트 생성
2. `xytron build --sdk <local-sdk>`로 Linux 공유 라이브러리 생성
3. `xytron pack` 및 `python tools/cli/verify_artifact.py dist/camera-1.0.0-linux-x64.xplugin` **PASS**
4. `nativeweb_generated_xytron_plugin_regression build/libcamera.so` **PASS** (`Generated plugin.json identity/ABI DLL/SO`)
5. S1+보강 통합 패치를 원본 handoff ZIP에 `git apply --check` / `git apply`로 적용한 뒤 소스 파일 바이트 비교 **PASS**

검증 날짜와 이력은 로컬 개발 산출물에 한정한다. GitHub CI 성공을 의미하지 않는다.
