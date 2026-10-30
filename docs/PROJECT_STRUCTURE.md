# Target Project Structure

현재 저장소는 아직 초기 단계다. 아래는 구현이 진행되면서 맞춰갈 target structure다.

```text
/
├─ CMakeLists.txt
├─ README.md
├─ HANDOFF.md
├─ CONTRIBUTING.md
│
├─ cmake/
│
├─ include/
│  └─ nativeweb/
│     ├─ any.hpp            # 실제 사용자 Any.h 통합 시 이름/위치 결정
│     ├─ webview.hpp
│     ├─ plugin.hpp
│     ├─ capabilities.hpp
│     └─ ...
│
├─ src/
│  ├─ core/
│  │  ├─ bridge/
│  │  ├─ async/
│  │  ├─ object/
│  │  ├─ binary/
│  │  ├─ permissions/
│  │  └─ plugin/
│  │
│  ├─ browser/
│  │  ├─ common/
│  │  ├─ cef/
│  │  └─ webview2/
│  │
│  ├─ host/
│  │  ├─ win32/
│  │  ├─ mfc/
│  │  ├─ winforms/
│  │  ├─ wpf/
│  │  ├─ qt/
│  │  └─ gtk/
│  │
│  ├─ c_api/
│  └─ dotnet/
│
├─ web/
│  ├─ sdk/
│  └─ ui/                  # optional high-level UI packages
│
├─ cli/
│
├─ tests/
│  ├─ core/
│  ├─ api/
│  ├─ bridge/
│  ├─ integration/
│  │  ├─ cef/
│  │  └─ webview2/
│  ├─ plugin/
│  ├─ host/
│  └─ cli/
│
├─ examples/
│  ├─ cpp-basic/
│  ├─ win32/
│  ├─ mfc/
│  ├─ winforms/
│  ├─ wpf/
│  ├─ qt/
│  ├─ gtk/
│  ├─ plugin-camera/
│  └─ sidecar/
│
├─ docs/
│  ├─ ARCHITECTURE.md
│  ├─ DECISIONS.md
│  ├─ TESTING.md
│  ├─ ROADMAP.md
│  └─ PROJECT_STRUCTURE.md
│
└─ .github/
   └─ workflows/
```

## Dependency direction

의존성은 가능한 한 아래 방향으로만 흐른다.

```text
Host adapter  ---> public SDK ---> Core
Browser impl  -----------------> Core interfaces
Plugin C++ API ---> C ABI ------> Core plugin runtime
.NET wrapper ---> C ABI
Web SDK ------> Bridge protocol
```

금지:

```text
Core -> MFC
Core -> WinForms
Core -> Qt
Core -> GTK
Public API -> CefClient
Public API -> WebView2 COM details
```

## Naming rule

제품 최종 이름은 아직 확정되지 않았다.

현재 문서에서는 프로젝트 개념명으로 `NativeWeb`, CLI placeholder로 `xweb`을 사용한다.

최종 branding이 정해지기 전까지 namespace/file/package 이름을 대규모로 고정하지 않는다.

## Source-of-truth rule

- 현재 상태: `HANDOFF.md`
- 설계: `docs/ARCHITECTURE.md`
- 확정 결정: `docs/DECISIONS.md`
- 테스트 정책: `docs/TESTING.md`
- 개발 순서: `docs/ROADMAP.md`

구현과 문서가 다르면 구현을 확인한 뒤 문서를 즉시 수정한다.
