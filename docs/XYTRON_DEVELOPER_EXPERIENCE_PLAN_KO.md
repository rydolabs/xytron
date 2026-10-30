# Xytron 개발자 경험 · 엔진/플러그인 배포 계획

상태: **제품 방향 확정 / 개발 단계별 적용 중** (2026-10-14)
기준 코드: NativeWeb handoff `02716552ade37e2b1be1c460d7f6d8ab1d328013`

> 이 문서는 목표 상태와 **실제 구현 상태**를 구분한다. CLI 스캐폴드 또는 ZIP 패키지 생성 성공은 실행 가능한 데스크톱 앱이나 완성된 언어 호스트를 의미하지 않는다.

## 1. 제품 결정

- Xytron Core/Bridge/PluginManager는 **C++11**로 유지한다. C ABI v1을 깨거나 다른 실험용 Core로 교체하지 않는다.
- 신규 데스크톱 앱의 사용자 **Main Backend 기본 언어는 C++**이다. 사용자는 Python/C#을 선택할 수 있다. 사용자 Backend 소스만 생성하고 Core 전체 소스는 복제하지 않는다.
- 앱 UI Renderer와 Backend 언어는 **독립적인 선택**이다. Windows에서는 WebView2를 기본으로 해결하는 것이 목표이며 CEF는 선택 사항이다. Linux에서는 WebView2를 선택하지 않는다.
- Plugin은 **독립 패키지인 Module**이다. C++/Python/C# 중 하나로 만든 기본 프로젝트를 사용자가 수정한다.
- 플러그인 배포 `plugin.json`은 작성 언어가 아닌 **`runtime`: `native` / `dotnet` / `python`** 을 정의한다. `--lang`은 CLI 프로젝트 템플릿 선택용 옵션이다. Python/.NET의 실제 실행 호스트는 별도 단계에서 구축한다.
- JS/TS API는 Backend 언어와 관계없이 단일 Bridge 인터페이스를 지향하며, 타입 메타데이터와 권한 검증은 NativeWeb Core를 거친다.
- Xytron CLI는 CMake/MSVC, dotnet/NuGet, Python 패키지 도구를 **대체하지 않는다**. 설치된 도구를 우선 활용하고 프로젝트 생성/빌드/패키징/배포를 조율한다.
- CEF는 **필요할 때 버전 고정 후 다운로드·캐싱**, WebView2는 Windows **Evergreen 공유 런타임 우선**. Xytron CLI 설치 파일에는 대형 브라우저 엔진을 동봉하지 않는다. 최종 CEF 앱 패키징에는 배포에 필요한 엔진 파일을 포함해야 한다.
- `setup`/`doctor`는 도구를 먼저 탐색하고, MSVC Build Tools 등 시스템 설치에는 사용자 동의를 요구한다. 도구 자동 설치, 엔진 자동 복원은 아직 미구현이다.

## 2. 목표 CLI와 실제 제공 범위

```bash
# 신규 애플리케이션 생성 (기본 C++ Main)
xytron new app my-app
xytron new app ai-app --backend python
xytron new app erp-app --backend csharp

# 독립 플러그인 소스 프로젝트 생성
xytron new plugin camera --lang cpp
xytron new plugin automation --lang python
xytron new plugin analytics --lang csharp

# 생성된 플러그인 디렉터리에서
cd plugins/automation
xytron build
xytron pack
xytron publish --output releases
```

| 명령/기능 | 1차 구현 | 제한 사항 |
|---|---|---|
| `new app` / `new desktop` | C++ 기본값, 3개 언어 템플릿 | 생성 소스는 아직 Core/Bridge에 연결되지 않은 스캐폴드 |
| `plugin new` / `new plugin --lang` | C++/Python/C# 기본 소스, 빌드 설명, `plugin.json` | C# `ExportAttribute`는 임시 메타데이터 전용 정의. 정식 SDK NuGet 참조 전환 필요 |
| `plugin build ID --sdk PREFIX` | 기존 C++ 실바이너리 빌드 유지 | 설치된 SDK를 사용자가 지정해야 함 |
| `build --sdk PREFIX` | 플러그인 디렉터리에서 C++ CMake 빌드 | 플랫폼별 NativeWeb SDK 설치 필요 |
| `build` (C#) | `dotnet build` 위임 | .NET SDK 필요, 언어 호스트 미연결 |
| `build` (Python) | `compileall` 구문 검사 | wheel 의존성 복원·정식 빌드 **아님** |
| `pack` / `plugin pack ID` | 바이너리/소스/매니페스트를 저장 방식 ZIP `.xplugin`에 묶음 | 런타임·외부 종속성 자동 포함, 디지털 서명, 플랫폼 전 범위 미완성 |
| `publish [--output DIR]` | 로컬 디렉터리로 `.xplugin` 복사 | 원격 레지스트리, 업로드/인증 없음 |
| `restore`, 앱 `run`, 앱 `build`, 앱 `publish` | 미구현 | 실 데스크톱 셸 및 브라우저 엔진 통합 선행 |
| `doctor --sdk` | Core 헤더/CMake 패키지 확인 | MSVC, dotnet, Python, 브라우저 엔진 검사는 향후 확대 |

현재 `xytron new app` 의 `xytron.json`에는 `main.language`가 저장된다. 이는 앱 백엔드 소스 언어이므로 그대로 둔다. `plugin.json`만 `runtime`을 공식 실행환경 필드로 사용한다.

## 3. 플러그인 패키지 계약 초안 (v1)

C++:

```json
{
  "schemaVersion": 1,
  "name": "camera",
  "version": "1.0.0",
  "runtime": "native",
  "entry": {
    "win-x64": "bin/win-x64/camera.dll",
    "linux-x64": "bin/linux-x64/libcamera.so"
  }
}
```

C#:

```json
{
  "schemaVersion": 1,
  "name": "analytics",
  "version": "1.0.0",
  "runtime": "dotnet",
  "entry": "bin/analytics.dll"
}
```

Python:

```json
{
  "schemaVersion": 1,
  "name": "automation",
  "version": "1.0.0",
  "runtime": "python",
  "entry": "automation.plugin"
}
```

- 현재 이름/ID는 소문자 영문으로 시작하는 `[a-z][a-z0-9_]*` 형식이다. 표시 이름과 별도의 패키지 ID를 지원하는 것은 후속 과제다.
- `entry`는 네이티브의 경우 플랫폼별 바이너리 경로, .NET은 어셈블리 파일, Python은 모듈 import 이름이다.
- 초기 `.xplugin`은 ZIP 포맷이다. 아카이브에 `plugin.json`을 포함하고 이름/버전/플랫폼을 파일명에 사용한다.
- 수정한 Python 소스 파일과 빌드된 .NET 출력은 패키지에 포함한다. Python의 `pyproject.toml`도 포함하되 종속성 설치 정책은 아직 구현되지 않았다.
- 패키저는 명시적 위치의 파일만 수집하고 심볼릭 링크/reparse point를 거부한다. 중복 아카이브 덮어쓰기도 거부한다.
- `pack`은 현재 마지막 `build`의 성공 여부를 일반화해서 기록하지 않는다. Python은 별도의 `build` 실행 후 패키징해야 하며, 소스를 수정했으면 반드시 다시 빌드한다.
- 아직 ABI 서명, 코드 서명, 패키지 해시 매니페스트, lockfile 기반 재현성, 권한 스키마 및 완전한 dependency closure를 지원하지 않으므로 이 패키지 형식은 **실험 단계**다.

## 4. 엔진 및 빌드 도구의 설치 정책

| 환경 | 기본 전략 | 사용자에게 표시할 내용 |
|---|---|---|
| Windows C++ | 기존 MSVC/Windows SDK/Visual Studio Build Tools 탐지 | C++ 워크로드 누락 시 설치/수정 승인 |
| Linux C++ | 기존 GCC/Clang 탐지 | 컴파일러가 없거나 호환되지 않으면 구체적 오류 |
| CMake/Ninja/vcpkg | 기존 설치 우선, 향후 검증된 버전 캐시로 복원 | 다운로드 위치·버전·해시 |
| .NET | 프로젝트 SDK 요구 버전 확인 | `global.json` 등 버전 정책 존중 |
| Python | 기존 버전 탐색, 향후 격리 환경에 의존성 복원 | 패키지 소스 및 버전 확인 |
| CEF | 앱에 필요한 버전만 가져와 캐시 | 빌드 재현성·라이선스·보안 업데이트 |
| WebView2 | Windows 설치 런타임 확인, Evergreen 우선 | 앱 설치 시 없으면 설치 프로그램에서 처리 |

CLI가 빌드 도구를 직접 재구현하지 않는다. 시스템 범위의 설치/수정은 무단으로 수행하지 않는다.

## 5. 구현 계획 / 완료 게이트

### S1 — 템플릿과 패키지 최소 경로 (로컬 구현 / 원격 CI 대기)

- [x] 기존 ABI/C++11/Core를 유지한 CLI 확장
- [x] `new app` 기본 C++ Main + C#/Python 선택
- [x] C++/Python/C# 편집 가능한 플러그인 템플릿과 `runtime` 메타데이터
- [x] 로컬 `build` 진입점, `.xplugin` 생성, 로컬 `publish`
- [x] C++ 기본 바이너리의 빌드/호출 및 Python export 호출 테스트
- [ ] .NET 8 SDK가 설치된 Windows/Linux에서 실제 C# 템플릿 빌드 확인
- [x] 구조적 JSON 필드 판독과 독립적인 ZIP 소비자 검증(파일 경로·중복·CRC·런타임 엔트리)
- [x] 수정된 Python 소스의 아카이브 배포 후 독립 프로세스 실행 검증
- [ ] 패키지 SHA-256 인덱스/디지털 서명/호환 ABI 검증 및 PluginManager 설치 연동 (별도 단계)

### S2 — 바인딩·플러그인 통합

- [ ] 공통 Export/FunctionDescriptor, 함수 인자·반환 타입·클래스 수명 계약
- [ ] JS/TS 바인딩 자동 생성 및 C ABI 메타데이터 유지
- [ ] 단일 PluginManager로 로드·언로드·버전·권한 정책 통합
- [ ] Python/.NET 언어 호스트와 실제 JS 호출/에러/취소 검증
- [ ] NW-014 출처·프레임·메서드 권한 검증

### S3 — 실행 가능한 앱과 엔진 복원

- [ ] `xytron restore` (SDK, CEF, 빌드 도구 버전/해시, 오프라인 오류)
- [ ] OS 셸 + 실제 네이티브 메뉴·컨텍스트 메뉴
- [ ] `xytron dev/run/build` 앱 실행·프런트엔드 및 백엔드 연결
- [ ] WebView2 Evergreen / 선택적 CEF, 각각 클린 환경 E2E

### S4 — 배포 수준 강화

- [ ] `.xplugin` 스키마/ABI 호환성 검사, 서명·무결성·의존성 잠금
- [ ] Windows/Linux 설치 패키지, 런타임 포함 정책, 재현 빌드
- [ ] 배포 채널, 안전한 업데이트 및 실패 복구
- [ ] 원격 플러그인 저장소는 필요성이 검증된 후 추가

## 6. 테스트 케이스

| ID | 검증 | 결과/게이트 |
|---|---|---|
| CLI-01 | app 기본 언어 C++, 3가지 Main 소스 생성 | 로컬 PASS |
| CLI-02 | 세 언어 플러그인 트리, 매니페스트 `runtime`/`entry` | 로컬 PASS |
| CLI-03 | 중복 이름·상위 경로 탈출·잘못된 언어 거부 | 로컬 PASS |
| CLI-04 | SDK로 생성 C++ DLL/SO 컴파일, 실제 ABI 호출 | Linux 로컬 PASS |
| CLI-05 | Python export 실제 호출 및 metadata discovery | Linux 로컬 PASS |
| CLI-06 | `.xplugin` ZIP CRC·파일 목록·수정한 코드 포함 | Linux 로컬 PASS |
| CLI-07 | 무빌드 네이티브 pack/배포 덮어쓰기 거부 | 로컬 PASS |
| CLI-08 | C# 생성 프로젝트 `dotnet build`와 Reflection discovery | .NET 환경의 CI 필요 |
| CLI-09 | CEF/WebView2에서 Python/.NET 모듈 실제 JS 호출 | 미구현 |
| CLI-10 | 보안: 무단 origin/중복/권한·패키지 변조 차단 | 향후 S2/S4 게이트 |
| CLI-11 | 중첩 필드에 의한 schemaVersion/entry 우회 및 중복 키 거부 | Linux 로컬 PASS |
| CLI-12 | ZIP 경로 탈출·중복 항목·CRC 손상·명시적 심볼릭 링크 거부 | Linux 로컬 PASS |
| CLI-13 | `.xplugin`에서 직접 Python 코드 로드하여 실행 | Linux 로컬 PASS |

## 7. 배포 정책

`xytron publish`가 성공했다는 사실은 **서명되고 실행 가능한 플러그인 설치**와 같지 않다. 로컬 아카이브가 생산된 것일 뿐이다. 아직 Xytron 언어 호스트와 프로덕션용 PluginManager에서 이를 설치·실행하는 통합 경로는 구현되지 않았다. 이 제한을 CLI 도움말, 문서, 테스트 결과에 계속 명시한다.

`main` 브랜치 병합 또는 공개 릴리스는 별도의 명시적 승인 뒤에만 진행한다.

테스트 실행 증거: [1차 CLI 검증 기록](XYTRON_CLI_VERIFICATION_KO.md).
