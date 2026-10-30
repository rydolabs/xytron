> 최신 상태(2026-10-15): 아래 과거 부트스트랩 기록은 역사적 내용입니다.
> `new app/new plugin/build/pack/publish`의 로컬 구현과 검증 강화 상태는
> [개발자 경험 계획](XYTRON_DEVELOPER_EXPERIENCE_PLAN_KO.md) 및
> [CLI 검증 기록](XYTRON_CLI_VERIFICATION_KO.md)을 참조하세요.
> 전체 데스크톱 앱 실행과 서명된 플러그인 설치는 여전히 미구현입니다.

Native C++11 xytron bootstrap CLI status
====================================

Implemented: xytron new desktop NAME --backend cpp|python|csharp; xytron plugin new NAME --language cpp; --help and --version. Command is native C++11 and does not need Node/Python to run. The app manifest has one Main and no Modules by default; C++ plugin template is independently packaged and uses PluginApi plus NATIVEWEB_PLUGIN macro.

This is only scaffolding. App files explicitly state that runtime integration is a TODO. Commands restore/run/build/test/publish/doctor/module add/plugin build fail with nonzero unimplemented status; no actual desktop app has been launched by this scaffold. Cross-platform tests use tools/cli/test_xytron.cmake and .github/workflows/xytron-cli.yml, independent of Core/browser CI. Next: manifest parser, language-neutral modules, plugin build, engine restore and standalone shell.

## First bootstrap validation

Scaffold matrix in [GitHub Actions 38334300772](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334300772) has PASSED on Ubuntu and Windows at source 091a57f. That run verified file generation/invalid input only, not DLL/SO output. A follow-up workflow step now installs Core from source and builds the generated camera plugin from its own CMake project against the SDK exported CMake config. Mark binary-plugin generation PASS only after both CI jobs complete.

## Verified initial CLI code and generated plugin compilation (2026-10-13)

- Commit `091a57f4` implemented C++11-native scaffold CLI for three Main languages and a C++ independently delivered plugin.
- [CLI #38334300772](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334300772) PASSED on Windows and Linux for input validation/scaffolding.
- [CLI #38334470209](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334470209) PASSED on **both Windows and Linux**, including installed CMake Core SDK and compilation of scaffold-generated camera plugin as a real DLL or SO.
- [CLI #38334609335](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334609335) also PASS on both OS after security policy foundation was added.

This is a repeatable developer-onboarding **foundation only**. No working desktop app, JS module bridge, Python/C# language host, `restore`, `run`, `publish` or CLI-managed plugin build command exists yet. The next work must test a real executable project without source tree access.

## CLI `module add` bootstrap

The new `xytron module add ID --language cpp|python|csharp` command runs
inside a CLI-generated project, creates embedded Module source/descriptor
and appends it to `xytron.json` while preserving the original single Main.
A Python Main + C++/Python/C# feature Module combination is exercised
by the CLI's Windows/Linux CTest. This is a **scaffolding operation**;
module loading, JS dispatch and real mixed-language app execution remain
unimplemented.

The implementation intentionally accepts only the CLI-produced manifest
layout, rejects unknown user-edited Module records and refuses duplicate
IDs or directory overwrites. A general versioned JSON schema parser and
atomic manifest save are required before production-grade CLI release.

## Verified mixed-language scaffold CLI (code 7e45e72b)

`xytron module add` and C++ plugin bootstrap compile tests pass on
Windows and Ubuntu in [CLI 38336744554](https://github.com/nssang00/chatgpt-write-test/actions/runs/38336744554).
The test script typo NW-016 was fixed; no live Python/.NET module
process runs as part of this command. Real shell/run/publish is pending.

## Native `xytron plugin build` implemented (2026-10-13)

`xytron plugin build camera --sdk /installed/nativeweb/prefix` invokes
CMake configure/build in the generated `plugins/camera` tree against a
previously installed NativeWeb Core SDK. The code builds an **actual C++
DLL/SO**, not an example-text file. SDK prefix arguments are bounded and
reject shell metacharacters; Module IDs are constrained to strict ASCII.
The workflow now tests the **CLI command** on both Windows and Linux,
in addition to the existing direct CMake build.

The SDK is still supplied explicitly. `xytron restore` (download),
automatic CLI plugin SDK discovery, signing, C ABI runtime loading,
`xytron run` and `xytron publish` are separate open deliverables.
Do not confuse this build command with application deployment.

### Verified `xytron plugin build`

[Windows + Ubuntu CLI run 38343029145](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343029145)
passes all commands, including SDK installation and the **native xytron
plugin build command** that configures and compiles an actual DLL/SO.
The first CLI test failed because a semicolon in a CMake test argument was
split into multiple positional arguments (NW-021); after replacing the
probe with an ampersand, the test passed on both OS. An installed SDK
is required; this is not `xytron restore/run/publish`.

## SDK readiness check (C++11 native CLI)

`xytron doctor --sdk PREFIX` verifies the installed SDK's
`include/nativeweb/plugin.hpp` and CMake package metadata, and runs
`cmake --version` to check the build tool exists on PATH. An absent
SDK/unsafe path returns a nonzero diagnostic instead of claiming success.
The bootstrap CLI workflow calls `doctor` on the freshly installed
Core SDK, before building the DLL/SO through `xytron plugin build`.

Doctor validates **Core SDK only**; it explicitly warns that browser engine
distribution, Python/.NET hosts, standalone app shell and publish
dependencies are not checked. Full `xytron restore` is not implemented.

## Verified `xytron doctor --sdk` (Core-only) — 2026-10-13

Run `xytron doctor --sdk /path/to/installed/NativeWeb`. The native
C++11 CLI verifies plugin C++ headers, installed CMake NativeWeb package
and `cmake --version` availability; missing SDK/config or an unsafe
path returns nonzero. It does **not** install or verify browser engine
binaries, Python/.NET hosts, OS-native app shell or `xytron publish`.

GitHub Actions [CLI 38355060435](https://github.com/nssang00/chatgpt-write-test/actions/runs/38355060435)
PASS Ubuntu+Windows and still actually compiles the CLI-generated C++ DLL/SO.
First workflow attempts had YAML and PowerShell syntax problems
(NW-026/NW-027); both are fixed in the verified run.

## New application default UI protocol

`xytron new desktop APP --backend cpp|python|csharp` now emits an
`app://desktop/index.html` UI entry in `xytron.json` and a
`ui` resources directory. This is a **declaration**, not runtime:
`xytron run`, `build`, `publish` and packaging remain unavailable.
See `docs/APP_RESOURCE_PROTOCOLS.md` and NW-037.

## 2026-10-14 developer experience increment (scaffold + local packaging)

See [Korean CLI product plan](XYTRON_DEVELOPER_EXPERIENCE_PLAN_KO.md) for owner decisions,
engine/toolchain restore policy, phase gates, and security boundaries.

The native C++11 CLI now additionally accepts `xytron new app NAME` (default
C++ Main), `xytron new plugin ID --lang cpp|python|csharp` and existing
`xytron plugin new ID --language ...`. C#/Python templates are editable
normal language projects and `plugin.json` declares runtime `dotnet`/`python`
(rather than authoring language). The native one keeps the ABI v1.

Inside a generated plugin directory `xytron build [--sdk PREFIX]` delegates
to CMake, dotnet, or Python syntax compilation; `xytron pack` creates a local
ZIP `.xplugin`; `xytron publish [--output DIR]` copies an already built
archive locally. `plugin pack ID` works from the parent project. **None of
these commands imply a running app, a managed JS bridge, signed packages,
verified external dependencies, or a remote registry.** The old
`plugin build ID --sdk PREFIX` continues working. Python `build` is only a
syntax check and not a wheel build. C# template uses a temporary metadata
attribute until the official binding SDK is consumable by generated projects.

## S2-1 실험적 네이티브 자동 바인딩

C++ 플러그인 함수의 ABI v1 타입 메타데이터를 Core에 전달하고 로컬에서 TypeScript 및 ES module 래퍼를 생성할 수 있다. 자세한 계약과 테스트, 안전상의 주의사항은 [`XYTRON_S2_TYPED_BINDINGS_KO.md`](XYTRON_S2_TYPED_BINDINGS_KO.md) 참조.

```bash
xytron bindings generate --library build/libcamera.so --out bindings --trusted
```

플러그인 코드를 실제 로딩·실행하므로 검증된 로컬 빌드에만 사용한다. Python/.NET 자동 바인딩과 최종 사용자 권한 모델은 별도 작업이다.

## S2 managed binding prototype

Python `@export` 함수는 코드를 실행하지 않는 AST 분석으로 바인딩을 생성한다.
C# `[Export]` 정적 메서드는 빌드한 어셈블리를 로드해야 하므로 개발자가 신뢰한 로컬 코드에만 `--trusted`를 사용한다.

```bash
xytron build
xytron bindings generate --out bindings             # Python
xytron bindings generate --out bindings --trusted   # C# / .NET
xytron pack
```

입력과 출력 제한, 타입 매핑, 보안 가정은 [XYTRON_MANAGED_BINDINGS_KO.md](XYTRON_MANAGED_BINDINGS_KO.md)에 정리했다.
