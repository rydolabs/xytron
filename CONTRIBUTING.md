# Contributing

## Before working

새 작업을 시작하기 전에 다음을 읽는다.

1. `HANDOFF.md`
2. `docs/ARCHITECTURE.md`
3. `docs/DECISIONS.md`
4. `docs/TESTING.md`
5. `docs/ROADMAP.md`

## Development rules

- C++ public baseline은 C++11.
- Public API는 가능한 한 작고 안정적으로 유지.
- CEF/WebView2 구현 세부사항을 public API에 노출하지 않음.
- Host layer에 core logic을 넣지 않음.
- Node/Rust/Python 등 특정 runtime을 불필요하게 강제하지 않음.
- 새 abstraction은 초보자의 기본 사용 경로를 복잡하게 만들지 않아야 함.
- Web standard로 충분한 기능은 불필요하게 재구현하지 않음.
- Raw pointer를 Web에 노출하지 않음.
- DLL/SO plugin boundary는 stable C ABI 원칙을 지킴.

## Tests required

새 기능은 관련 테스트를 포함해야 한다.

버그 수정은 가능한 경우:

1. failing reproduction test
2. implementation fix
3. focused test pass
4. full relevant regression pass

순서로 진행한다.

## Cross-platform

가능한 core 변경은 Linux/Windows 둘 다 통과해야 한다.

Platform-specific code는 명시적으로 backend/host directory에 격리한다.

## Handoff discipline

세션/작업 종료 전에 의미 있는 변화가 있었다면 `HANDOFF.md`를 갱신한다.

반드시 기록:

- 완료 항목
- CI 결과
- known issue
- 새로운 결정
- 다음 작업
- blocker

대화 기록에만 중요한 결정을 남기지 않는다.
