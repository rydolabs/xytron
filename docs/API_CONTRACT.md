# NativeWeb / Xytron API Contract

이 문서는 public API와 runtime semantics의 현재 계약을 기록한다.
제품 최종 branding은 별도 결정 사항이며, 아래 JavaScript 예제에서는 현재 UX 목표 이름인 `xytron`을 사용한다.
C++ namespace와 repository 내부 이름은 당장 대규모 rename하지 않는다.

## 1. Public API hierarchy

### Recommended JavaScript API

새 애플리케이션의 기본 사용법은 direct API다.

```js
const user = await xytron.getUser(42);
const sum = await xytron.apple.add(10, 20);
const otherSum = await xytron.math.add(10, 20);
const camera = await xytron.camera.open("CAM-01");
```

C++에서 등록한 dotted method name은 JavaScript namespace로 보인다.

```cpp
webview.bind("getUser", getUser);
webview.bind("camera.open", openCamera);
```

```js
await xytron.getUser(42);
await xytron.camera.open();
```

### Primitive / migration API

`invoke()`는 없애지 않는다.

```js
await xytron.invoke("getUser", 42);
await xytron.invoke("camera.open");
```

용도:

- Electron IPC migration
- dynamic method names
- debugging
- low-level framework code
- generated bridge implementation

원칙:

> `xytron.foo()` is the product API.  
> `xytron.invoke("foo")` is the primitive API.

Getting Started에서는 direct API를 먼저 보여준다.

### Bidirectional one-way messages (first baseline, 2026-10-16)

Direct `xytron.math.add()` is preferred over `xytron.invoke("math.add")`.
The latter remains available for dynamic names and compatibility.

```js
xytron.postMessage("ui.ready", { width: 1280 });        // JS -> Native, void
const id = xytron.onMessage("camera.frame", buffer => {
  // `buffer` is ArrayBuffer for nativeweb::Binary.
});
xytron.offMessage(id);
```

```cpp
nativeweb::MessageSubscriptionId id = webview.onMessage(
    "ui.ready", [](const Any& value) { /* dispatch thread */ });
webview.postMessage("camera.frame", nativeweb::Binary());
webview.offMessage(id);
```

- No acknowledgement or delivery guarantee; use RPC if the caller needs a result.
- Browser's engine-verified main-frame origin and active document token
  protect JS -> Native messages; subscriptions disappear with their context.
- Both browser adapters use the shared Core Event envelope, channel and payload.
- Callbacks run on browser dispatch thread and MUST NOT perform blocking I/O.
- Existing `xytron.on/off`, `WebView::emit` remain supported aliases.
- 1..256-byte channel names; message size limits and module-level event
  permissions require a separate hardening pass before untrusted plugins.
- `nativeweb::ByteArray` is a synonym for owned `nativeweb::Binary` bytes,
  NOT `std::vector<std::string>`.

## 2. Native object binding

C++ 객체는 JavaScript 객체처럼 사용할 수 있어야 한다.

Root/singleton service의 현재 목표 C++ 형태:

```cpp
webview.bind("apple", new Apple())
    .method("add", &Apple::add)
    .method("sub", &Apple::sub);
```

raw-pointer convenience overload는 ownership을 NativeWeb으로 즉시 이전한다.
명시적 ownership을 원하는 코드는 `std::shared_ptr<T>` overload를 사용한다.

목표 UX:

```cpp
class Apple {
public:
    int add(int a, int b);
    int sub(int a, int b);
};
```

```js
await xytron.apple.add(1, 2);
await xytron.apple.sub(5, 3);
```

또한 factory가 native object를 반환할 수 있다.

```js
const camera = await xytron.camera.open();
await camera.start();
const frame = await camera.capture();
await camera.dispose();
```

내부 계약:

```text
shared_ptr<T>
    -> ObjectRegistry
    -> opaque ObjectId
    -> JS Proxy
```

raw pointer는 JS contract가 아니다.

C++11에는 reflection이 없으므로 공개할 method 목록은 metadata/macro/builder 중 하나로 명시적으로 등록한다.
구체적인 class-binding 문법은 metadata model을 먼저 안정화한 뒤 확정한다.

## 3. Async and concurrency

### JavaScript -> C++

JavaScript에서 native 호출은 항상 Promise boundary다.

```js
const result = await xytron.process(data);
```

등록된 평범한 동기 C++ 함수도 기본적으로 browser/UI dispatch thread가 아니라 NativeWeb worker pool에서 실행한다.

```text
JS request
   -> RequestId
   -> execution queue
   -> worker pool
   -> C++ callable
   -> response
   -> Promise resolve/reject
```

사용자는 기본 경로에서 threading을 몰라도 UI가 block되지 않아야 한다.

향후 advanced execution policy:

- worker pool — default
- serial executor — thread-affine/non-thread-safe object
- UI/host thread — 명시적으로 필요한 API
- named/custom executor — expert use

advanced policy가 beginner API에 노출되어서는 안 된다.

### C++ -> JavaScript

```cpp
std::future<int> result =
    webview.execute<int>("ui.calculate", 3, 4);
```

각 호출은 독립 RequestId를 갖는다.

```text
RequestId -> pending promise/future
```

응답 완료 순서는 요청 순서와 달라도 된다.
동시에 여러 호출을 수행하는 것이 정상적인 사용법이다.

Browser dispatch thread에서 Xytron future를 blocking wait하면 안 된다.

Typed C++ futures are resolved directly by the RequestId-correlated pending state.
`execute<T>()` must not create a helper thread per call merely to convert `Any` into `T`.

### Shutdown

- C++ -> JS pending future는 WebView destroy 시 deterministic error로 종료한다.
- queued JS -> C++ work는 이후 cancellation policy에서 명시적으로 다룬다.
- 이미 실행 중인 arbitrary C++ 함수를 강제 terminate하지 않는다.
- cooperative cancellation은 후속 고급 기능이다.

## 4. Error semantics

기본 error contract:

```text
code
message
```

C++ exception은 JS Promise rejection으로 변환한다.
JS Promise rejection은 C++ future exception으로 변환한다.

unknown method, invalid argument, destroyed context, queue overload 등은 machine-readable code를 가져야 한다.

## 5. Binary contract

### Default: Binary

일반 사용자 API:

```cpp
nativeweb::Binary capture();
```

```js
const frame = await xytron.camera.capture();
```

JavaScript에서는 ArrayBuffer/Uint8Array로 자연스럽게 소비할 수 있다.

기본 Binary는 안전한 value semantics를 가진다.
runtime은 observable semantics를 바꾸지 않는 범위에서 Chromium IPC, internal buffer optimization 등을 자동 선택할 수 있다.

### Advanced: Transfer

ownership 이동이 필요한 경우 별도 API/type으로 명시한다.

개념:

```js
await xytron.process(
    xytron.transfer(buffer)
);
```

source buffer detach/invalid 같은 observable ownership 변화는 자동으로 적용하지 않는다.

### Advanced: Shared

shared backing storage는 별도 API/type으로 명시한다.

개념:

```js
const shared = await xytron.camera.captureShared();

try {
    use(shared.buffer);
} finally {
    shared.release();
}
```

CEF/WebView2의 실제 transport 차이는 public API에 노출하지 않는다.

원칙:

> Implementation optimization may be automatic.  
> Ownership semantics must be explicit.

### Later: Stream / ring buffer

고정밀도 카메라/영상처럼 반복되는 대용량 데이터는 Transfer/Shared 하나로 모두 해결하려 하지 않는다.
필요하면 shared ring buffer/streaming abstraction을 별도로 제공한다.

## 6. Type mapping

Core baseline은 C++11이다.

우선 Core contract:

- bool
- integer types supported by explicit adapters
- double
- std::string
- VariantList
- VariantDict
- nativeweb::Binary
- NativeObjectHandle
- std::future on async boundary

C++17 기능은 optional convenience adapter다.

- std::optional
- std::variant
- std::string_view

C++17 편의를 위해 Core baseline을 올리지 않는다.

다음 항목은 별도 specification으로 확정한다.

- int64_t / uint64_t <-> JS BigInt policy
- enum mapping
- map key restrictions
- time/chrono
- optional null vs undefined
- class/struct code generation

## 7. ABI

사용자-facing API는 C++답게 제공한다.

Plugin/.NET/other-language binary boundary는 stable C ABI를 원칙으로 한다.

```text
C++ convenience layer
        ->
stable C ABI
        ->
NativeWeb runtime
```

C++ STL object ownership을 DLL/SO ABI 경계에 무분별하게 노출하지 않는다.

## 8. Design input review rule

외부/별도 설계 문서는 그대로 합치지 않는다.

각 제안은 다음 셋 중 하나로 판단한다.

- Adopt — 현재 제품 철학과 일치하여 그대로 채택
- Adapt — 방향은 맞지만 NativeWeb 기준으로 수정
- Reject/Defer — 현재 방향과 맞지 않거나 premature

이번 검토에서 Adopt/Adapt한 핵심:

- existing C++ assets + modern Web UI positioning
- API stability and predictability
- explicit threading/lifetime/error semantics
- stable C ABI
- direct JS call as primary DX
- native object binding
- safe Binary default + explicit advanced ownership APIs

그대로 채택하지 않은 항목:

- 특정 문서의 기능 우선순위를 그대로 강제
- C++17 타입을 Core baseline으로 사용
- product identity를 C++-only framework로 축소
- 크기만 보고 Transfer/Shared ownership semantics를 자동 변경

## 9. Test gate

모든 public/runtime contract는 테스트로 고정한다.

우선순위:

1. unit/core contract test
2. Windows + Linux Core Regression
3. real browser integration where browser behavior matters
4. backend parity contract

새 기능을 구현했더라도 관련 regression이 green이 아니면 완료가 아니다.
