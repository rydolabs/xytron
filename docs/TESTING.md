# Testing and CI

## Definition of done

기능은 다음 세 조건을 모두 만족해야 완료로 간주한다.

```text
implementation
    +
feature/regression test
    +
full relevant CI pass
```

버그 수정:

```text
bug
  -> reproduction test
  -> test fails
  -> fix
  -> test passes
  -> full regression
```

## Current verified CI

Workflow:

```text
.github/workflows/platform-smoke.yml
```

Matrix:

- `ubuntu-latest`
- `windows-latest`

Baseline:

- C++11
- CMake
- build
- CTest

Initial verified run:

- Run ID: `37597876713`
- Head SHA: `c253803db28f0047128a4723a2e82862a26f7bf7`
- Ubuntu: success
- Windows: success

Platform Smoke 외에 `.github/workflows/core-regression.yml`이 추가되었다.

현재 검증된 NativeWeb Core/API regression:

- Run ID: `37610573658`
- Head SHA: `abf8a2c5908c1bff3ee3e8473de3a52242e7e6b7`
- Ubuntu: build + CTest success
- Windows: build + CTest success
- Any/Variant containers/binary/invalid access
- typed bind adapter
- public WebView API compile contract
- BrowserBackend compile contract

Latest full verified plugin/lifetime/C++-wrapper baseline:

- Code head: `b876a74726e33609d3603ec572c6c8d03ab592e8`
- Core Regression run `38107973666`: Ubuntu + Windows success
- Platform Smoke run `38107973650`: Ubuntu + Windows success
- Linux CEF Bridge run `38107973658`: success
- Windows CEF Bridge run `38107973662`: success
- Windows WebView2 Bridge run `38107973704`: success

Core coverage now includes Any/containers/binary, typed bind, RequestId-correlated typed pending futures without per-call `std::async`, structured errors, worker pool/task queue, concurrent async bridge routing, event dispatcher, bridge messages/runtime, ObjectRegistry, NativeObjectRuntime, root object binding, WebView orchestration/engine selection and public API compile contracts.

Real CEF integration coverage now includes `xytron.foo()` direct calls, `xytron.invoke()` compatibility, worker-thread execution, concurrent `Promise.all()` overlap, root C++ object methods, public WebView orchestration, reload reconnect, JS→C++, C++→JS, objects, arrays, binary, events, Promise resolve/reject, native exception mapping, missing-method rejection, multi-WebView isolation, destroy-with-real-pending-Promise, and native object Proxy lifecycle/round-trip.

## Target CI topology

```text
core-regression
  +-- ubuntu
  +-- windows

api-regression
  +-- ubuntu
  +-- windows

browser-contract
  +-- linux / CEF
  +-- windows / CEF
  +-- windows / WebView2

host-smoke
  +-- MFC
  +-- WinForms
  +-- WPF
  +-- Qt
  +-- GTK

plugin-regression
  +-- DLL Windows
  +-- SO Linux

cli-e2e
  +-- ubuntu
  +-- windows
```

초기 구현 순서는 core -> API -> Linux CEF -> Windows WebView2/CEF -> host/plugin/CLI 순으로 확장한다.

## Core regression

`Any.h` 확보 후 최소 테스트:

- scalar construction/copy
- string
- bool
- integer/floating conversion
- VariantList
- VariantDict
- binary vector
- nested value
- invalid cast/error
- copy/move semantics where supported
- async/future interaction

테스트는 C++11에서 동작해야 한다.

## Bridge contract regression

최소 baseline:

1. JS -> C++ int/string
2. JS Object -> C++ VariantDict
3. JS Array -> C++ VariantList
4. C++ -> JS scalar/object/list
5. C++ return -> Promise resolve
6. C++ exception -> Promise reject
7. std::future -> Promise
8. binary -> Uint8Array
9. two WebViews independent
10. reload -> bridge reconnect
11. destroy -> pending call safe termination/rejection
12. unknown method
13. invalid arguments
14. object handle lifetime
15. event subscribe/unsubscribe

## Public API compile tests

API shape 자체가 regression contract다.

예:

```cpp
nativeweb::WebView webview;
webview.create(parent, "index.html");
webview.bind("math.add", [](int a, int b) { return a + b; });
webview.destroy();
```

이 예제가 C++11 Windows/Linux에서 compile되는지 항상 검증한다.

## Browser integration tests

Mock만으로 CEF backend 완료를 선언하지 않는다.

CEF integration 완료 조건 및 현재 상태:

- [x] real CEF distribution 사용
- [x] CefExecuteProcess/CefInitialize
- [x] browser create
- [x] local HTML load
- [x] JS -> C++
- [x] C++ -> JS
- [x] JS-facing Promise resolve/reject
- [x] JS-returned Promise -> C++ future resolve/reject
- [x] reload reconnect
- [x] binary
- [x] events
- [x] destroy while bridge calls are pending (real browser, C++ -> unresolved JS Promise)
- [x] multi-WebView isolation
- [x] native object Proxy method call / handle round-trip / dispose

WebView2 backend가 추가되면 같은 browser contract suite를 재사용한다.

## Host tests

Host layer는 복잡한 logic을 가지면 안 된다.

따라서 host test의 핵심은:

- 기본 framework sample/project 형태로 build
- WebView control 생성
- resize
- focus
- load
- destroy
- bridge API 접근

Host-specific business logic을 만들지 않는다.

## Plugin regression

최소 contract:

- DLL/SO load
- ABI version check
- plugin metadata/name
- function registration
- function invoke
- async result
- exception/error
- event emit
- binary
- unload/lifetime behavior
- incompatible ABI rejection

장기적으로 isolated plugin host mode도 별도 regression suite로 둔다.

## Test naming/layout target

예상 구조:

```text
tests/
  core/
    any_regression.cpp
    async_regression.cpp
    object_registry_regression.cpp

  api/
    public_api_compile.cpp

  bridge/
    bridge_contract.*

  integration/
    cef/
    webview2/

  plugin/
    sample_plugin/
    plugin_regression.*

  host/
    mfc/
    winforms/
    wpf/
    qt/
    gtk/

  cli/
```

## CI rules

- `fail-fast: false`: 한 platform 실패가 다른 platform 결과를 가리지 않게 한다.
- 테스트 실패 시 `ctest --output-on-failure` 또는 동등한 상세 로그를 남긴다.
- compiler/platform/backend matrix를 명시적으로 표시한다.
- real browser integration은 mock unit test와 job 이름을 분리한다.
- flaky test를 무조건 retry해서 숨기지 않는다. 원인을 기록한다.
- known failure를 success로 포장하지 않는다.


## Execution/concurrency regression

새 execution runtime은 최소 다음을 검증한다.

- worker pool이 caller/browser thread와 다른 thread에서 user callable 실행
- 여러 request를 동시에 queue할 수 있음
- 여러 worker에서 실제 overlap 가능
- completion order가 request order와 달라도 RequestId가 올바른 결과를 연결
- native exception이 해당 request 하나만 reject
- shutdown 후 새 task reject
- queued/running task lifecycle을 정의된 정책대로 처리
- real CEF에서 JS Promise 여러 개를 동시에 호출해도 독립적으로 완료

## JavaScript facade regression

real browser에서 둘 다 유지한다.

Recommended:

```js
await xytron.math.add(1, 2);
await xytron.camera.open();
```

Primitive compatibility:

```js
await xytron.invoke("math.add", 1, 2);
```

direct facade는 dotted namespace, Promise rejection, native object return을 모두 동일 bridge contract로 사용해야 한다.

또한 root/singleton C++ object binding은 real browser에서 검증한다.

```cpp
webview.bind("apple", new Apple())
    .method("add", &Apple::add);
```

```js
await xytron.apple.add(1, 2);
```

## Binary semantics regression

기본 Binary와 advanced ownership API는 별도 테스트한다.

- Binary: safe ordinary value semantics
- Transfer: explicit ownership transfer/detach semantics
- Shared: explicit shared lifetime/release semantics
- 크기 기반 내부 최적화가 기본 Binary의 observable semantics를 변경하지 않는지 검증


## WebView2 integration

Windows WebView2는 mock 완료로 간주하지 않는다.

현재 real Windows runner 검증:

- pinned Microsoft.Web.WebView2 SDK bootstrap
- actual Evergreen Runtime availability check
- hidden Win32 host HWND + real WebView2 controller
- `Engine::Auto` -> WebView2
- same `bridge_test.html` contract used by Linux CEF
- JS -> C++ / C++ -> JS
- Promise resolve/reject
- worker-pool concurrency
- root object/native object
- objects/arrays/binary/events
- reload
- destroy with pending C++ future
- two-WebView request/event/future isolation

Latest run: `37787721031`.


## Callable metadata regression

Callable refactor는 public API 변경으로 간주하지 않는다.

최소 regression:

- free function
- lambda
- mutable/stateful callable where supported
- member function
- const member function
- void result
- argument count mismatch
- argument conversion failure
- native exception -> structured error
- signature argument count metadata
- signature argument type metadata
- return type metadata
- explicit overloaded function selection
- root object lifetime retained by bound method
- existing `webview.bind(...)` source compatibility
- existing `bind("apple", ...).method(...)` source compatibility
- CEF/WebView2 browser contract unchanged

## Plugin lifetime regression

Plugin 완료 조건은 단순 DLL/SO load 성공이 아니다.

최소 contract:

- valid DLL/SO load
- missing entry symbol rejection
- ABI version mismatch rejection
- duplicate plugin id/API registration rejection
- registration removed with module teardown
- bound callable keeps module alive while callable is reachable
- native object keeps module alive while object is reachable
- queued/running worker task keeps module alive until completion
- unload rejects with `plugin_busy` (or final equivalent) while unsafe
- controlled unload after calls/objects are released
- plugin exception does not cross C ABI boundary
- Windows DLL + Linux SO real integration


## SharedLibrary regression

Plugin loader foundation은 mock handle로 완료 처리하지 않는다.

Windows/Linux Core CI에서 실제 shared library를 빌드하고 다음을 검증한다.

- DLL/SO path load
- exported C symbol resolve
- resolved function 실제 호출
- missing symbol structured error
- missing library structured error
- move ownership
- explicit unload

이 테스트는 PluginModule/C ABI의 전제 조건일 뿐 plugin 완료 기준은 아니다.


## PluginModule v1 vertical-slice regression

현재 `plugin.module`은 Ubuntu/Windows에서 실제 SO/DLL을 생성한다.

검증:

- valid plugin init symbol load
- ABI v1 validation
- plugin id/version
- relative namespace registration
- int32 function call result
- plugin structured error mapping
- bad ABI rejection
- missing entry symbol rejection
- module teardown unregisters methods
- retained Callable keeps plugin library code alive after module teardown

다음 regression 우선순위:

1. [x] queued/running worker task retains module lease
2. [x] generic managed native object retains optional owner lease
3. [ ] real plugin-created native object retains module lease
4. [ ] plugin-owned event/listener lifetime
5. [ ] explicit safe unload / busy state


## C++ plugin wrapper regression

`include/nativeweb/plugin.hpp` convenience layer는 C ABI를 대체하지 않는다.

실제 Ubuntu/Windows Core CI에서 별도 DLL/SO를 다음 형태로 빌드한다.

```cpp
class SampleCppPlugin : public nativeweb::Plugin {
public:
    void registerApi(nativeweb::PluginApi& api) override {
        api.bind("add", ...);
        api.bind("echo", ...);
    }
};

NATIVEWEB_PLUGIN(SampleCppPlugin, "cppsample")
```

검증:

- plugin target은 NativeWeb Core library에 링크하지 않음
- macro-exported C ABI v1 entry point를 PluginModule이 load
- plugin id/version
- int32 call
- string call/result
- binary argument
- C++ `nativeweb::Error` -> ABI error -> host `nativeweb::Error`
- module teardown -> wrapper registrations 제거

이 테스트가 성공해도 native object/event/async ABI가 완료되었다는 뜻은 아니다.

## Plugin browser E2E (local continuation)

The standalone CEF/WebView2 integration builds now contain the real-DLL/SO
`browser.plugin_object_lifecycle` CTest. See `docs/BROWSER_PLUGIN_E2E.md`
for CLI usage, expected checkpoints, and runner prerequisites. Passing Core
CTest alone does not establish that the browser test passed.
