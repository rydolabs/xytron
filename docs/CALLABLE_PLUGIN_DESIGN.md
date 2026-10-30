# Callable and Plugin Architecture Notes

이 문서는 NativeWeb의 함수 등록 / object binding / plugin runtime을 설계할 때 참고한 구현들의 패턴을 정리하고,
무엇을 Adopt / Adapt / Reject/Defer할지 기록한다.

참고 구현:

- PothosCore Callable
- PothosCore Plugin
- ROS class_loader
- ROS pluginlib
- Qt QLibrary / QPluginLoader
- Boost.DLL shared_library / import lifetime patterns
- POCO SharedLibrary / ClassLoader

이 문서는 외부 구현을 복제하기 위한 문서가 아니다.
현재 NativeWeb의 public API와 C++11 baseline을 유지하면서 검증된 패턴만 선택적으로 반영한다.

## 1. Public API stability

이번 검토로 beginner-facing API를 교체하지 않는다.

계속 유지할 API:

```cpp
webview.bind("math.add", [](int a, int b) {
    return a + b;
});

webview.bind("apple", new Apple())
    .method("add", &Apple::add)
    .method("sub", &Apple::sub);

std::future<int> value =
    webview.execute<int>("ui.calculate", 3, 4);
```

```js
await xytron.math.add(1, 2);
await xytron.apple.add(10, 20);
await xytron.invoke("math.add", 1, 2);
```

새 구조는 이 API 아래의 implementation foundation을 강화한다.

원칙:

> Public API should remain simple while internal metadata and lifetime rules become stronger.

고급 사용자에게만 overload selection / execution policy / plugin control 같은 additive API를 제공할 수 있다.

## 2. Callable: Adopt / Adapt

Pothos Callable에서 참고할 핵심은 typed C++ callable을 type-erased 호출 경계로 내리는 구조다.

NativeWeb 목표:

```text
free function / lambda / member function
                |
        template traits
                |
             Callable
                |
      SignatureMetadata
        - argument count
        - argument types
        - return type
                |
          opaque invoke
       VariantList -> Any
                |
         BindingRegistry
```

현재 `DynamicFunction = std::function<Any(const VariantList&)>`는 immediate execution contract로 유효하다.
그러나 장기적으로 registry entry는 단순 function 하나가 아니라 다음 정보를 가진다.

```cpp
struct BindingEntry {
    Callable callable;
    SignatureMetadata signature;
    ExecutionPolicy execution;
    // owner/module reference when applicable
};
```

### Adopt

- C++11 template traits로 free function/lambda/member/const-member signature를 추출
- typed callable -> type-erased invocation
- argument count/type metadata
- return type metadata
- bound instance/argument 개념
- explicit overload selection escape hatch
- metadata를 TS code generation에 재사용

### Adapt

Pothos의 generic runtime Object 개념을 NativeWeb의 wire value 전체로 가져오지 않는다.

NativeWeb은 계속 다음 둘을 분리한다.

```text
Any
  = bridge-safe value world
    scalar/string/list/map/binary

NativeObjectHandle
  = native object identity world
    shared_ptr<T> -> ObjectRegistry -> ObjectId -> JS Proxy
```

arbitrary C++ object / raw pointer / STL implementation object를 JS wire Any에 암묵적으로 넣지 않는다.

### Defer

overload helper의 정확한 spelling은 metadata layer 구현 후 확정한다.

개념 예:

```cpp
.method(
    "add",
    nativeweb::overload<int(int, int)>(&Apple::add));
```

일반적인 non-overloaded method에는 추가 문법을 요구하지 않는다.

## 3. Signature metadata

Callable metadata는 단지 reflection 편의를 위한 것이 아니다.

같은 metadata를 다음 기능에서 재사용한다.

- argument validation
- better error messages
- TypeScript declaration generation
- plugin inspection
- permission/capability mapping
- documentation generation
- future sidecar schema

C++11에는 language reflection이 없으므로 template traits / explicit binding metadata를 사용한다.

RTTI / `std::type_info`는 in-process implementation aid로 사용할 수 있지만 stable plugin ABI identity로 사용하지 않는다.

## 4. Registration ownership

Pothos Plugin의 module-to-registration tracking 패턴을 반영한다.

모든 registration은 내부적으로 소유권을 추적할 수 있어야 한다.

개념:

```text
BindingRegistry
    |
 RegistrationToken
    |
PluginModule / WebView / other owner
```

public beginner API에서 token을 직접 다룰 필요는 없다.

```cpp
api.bind("camera.open", ...);
api.bind("camera.capture", ...);
```

내부적으로는 plugin module이 registration token들을 보유한다.

이 구조는 plugin 외에도 per-WebView registration, temporary feature modules 등에 재사용할 수 있다.

## 5. Shared library layer

Qt QLibrary / Boost.DLL / POCO SharedLibrary에서 공통적으로 보이는 low-level 역할만 작은 abstraction으로 둔다.

```cpp
class SharedLibrary {
public:
    void load(...);
    void unload();
    bool loaded() const;
    void* symbol(...);
};
```

platform implementation:

```text
Windows: LoadLibraryEx / GetProcAddress / FreeLibrary
Linux:   dlopen / dlsym / dlclose
```

Boost/Qt/POCO 자체를 NativeWeb Core의 필수 dependency로 만들 필요는 현재 없다.

## 6. Plugin binary boundary

사용자-facing plugin API는 C++답게 유지하지만 DLL/SO boundary는 stable C ABI를 사용한다.

개념:

```c
extern "C" NW_EXPORT
int nativeweb_plugin_init_v1(
    const nw_host_api_v1* host,
    nw_plugin_api_v1* plugin);
```

user convenience:

```cpp
class CameraPlugin : public nativeweb::Plugin {
public:
    void registerApi(nativeweb::Api& api) override {
        api.bind("open", ...);
        api.bind("capture", ...);
    }
};

NATIVEWEB_PLUGIN(CameraPlugin, "camera");
```

macro는 static-global registry를 ABI mechanism으로 사용하기보다 well-known C entry point를 export하는 convenience layer로 만드는 것을 선호한다.

## 7. Plugin metadata and validation

Pothos/ROS/Qt 계열에서 확인되는 discovery/version/metadata 패턴은 반영한다.

최소 plugin descriptor 목표:

```text
plugin id
plugin version
ABI version
runtime compatibility
architecture
capabilities / permissions
exported API metadata
```

C++ STL object, compiler-specific RTTI, exception object를 stable ABI boundary로 직접 넘기지 않는다.

## 8. Module lifetime: high priority

ROS class_loader / Boost.DLL / POCO ClassLoader가 강조하는 핵심 위험은 동일하다.

plugin DLL/SO 코드로 생성한 callable/object가 살아 있는데 library를 unload하면 안 된다.

NativeWeb lifetime rule:

```text
BindingEntry
NativeObjectEntry
queued/running Task
event subscription (when plugin-owned)
       |
       +---- strong PluginModule lifetime reference
```

즉 plugin code를 실행할 수 있는 어떤 entity도 module lifetime보다 오래 살아서는 안 된다.

Native object entry의 목표 형태:

```text
NativeObjectEntry
  - shared_ptr<void> object
  - type metadata
  - shared_ptr<PluginModule> owner
```

Callable/binding도 같은 owner reference를 가진다.

worker queue에 plugin callable을 넣을 때 task도 owner module reference를 capture한다.

## 9. Unload policy

초기 안정성 정책은 aggressive hot unload가 아니다.

기본:

```text
load plugin
  -> use for application/runtime lifetime
  -> unload during controlled shutdown
```

향후 explicit unload는 state machine으로 구현한다.

```text
Loaded
  -> Quiescing
     - reject new calls
     - unregister public entry points
     - drain/cancel queued work by defined policy
     - active calls == 0
     - live native objects == 0
     - plugin-owned listeners/resources == 0
  -> Unloaded
```

안전 조건을 만족하지 못하면 강제 unload보다 `plugin_busy` 같은 structured error를 선호한다.

raw/unmanaged plugin object는 unload safety를 깨므로 beginner API로 제공하지 않는다.

## 10. Plugin layers

ROS class_loader / pluginlib의 low-level loader와 high-level discovery 분리는 좋은 모델이다.

NativeWeb 목표:

```text
PluginManager / PluginRegistry
  - id/version/metadata
  - discovery
  - permissions
  - inspect
           |
       PluginModule
  - library lifetime
  - registrations
  - active objects/calls
           |
      SharedLibrary
  - load
  - symbol
  - unload
           |
  stable C ABI entry point
```

ROS XML manifest를 그대로 채택하지 않는다.
우선 embedded descriptor를 기본으로 하고 외부 manifest가 실제로 필요해질 때 추가한다.

## 11. Diagnostics

POCO SharedLibrary 계열에서 참고할 가치가 큰 부분은 load failure diagnostics다.

향후:

```text
xweb plugin inspect camera.dll
xweb doctor
```

가 다음을 설명할 수 있게 한다.

- ABI mismatch
- architecture mismatch
- runtime version mismatch
- missing dependency
- duplicate plugin/API registration
- permission/capability mismatch

missing dependency PE/ELF/Mach-O 분석은 loader baseline 이후의 diagnostics feature로 둔다.

## 12. What we deliberately do not adopt

현재 다음은 의도적으로 채택하지 않는다.

- arbitrary C++ Object를 bridge Any처럼 사용
- compiler RTTI를 stable plugin ABI type identity로 사용
- STL/C++ exception ownership을 C ABI 밖으로 노출
- static constructor side effect만으로 plugin ABI registration을 정의
- ROS XML manifest를 필수로 요구
- plugin load/unload를 beginner workflow에 노출
- public API를 Callable/Opaque/Registry 중심으로 재작성

## 13. Implementation order

browser backend green baseline을 유지한 상태에서 다음 순서로 진행한다.

1. [x] `Callable` type-erasure + signature metadata baseline
2. [x] 기존 `bind()` / root object `.method()`를 Callable 위로 이관
3. [x] free/lambda/member/const-member/void metadata regression
4. [ ] explicit overload-selection helper
5. [x] generation-safe `RegistrationToken` + registration ownership
6. [x] small `SharedLibrary` abstraction + real DLL/SO loader regression
7. [x] `PluginModule` + stable C ABI v1 function-registration baseline
8. [x] real DLL/SO plugin load/register/call/callable-lifetime regression on Windows/Linux
9. [x] queued/running worker task -> PluginModule lease regression
10. [ ] native object -> PluginModule lease coupling
    - [x] generic ObjectRegistry/NativeObjectRuntime owner-lease foundation
    - [ ] real plugin-created object ABI/lifetime regression
11. [x] C++ `Plugin / PluginApi / NATIVEWEB_PLUGIN` convenience wrapper
12. [ ] conservative unload / `plugin_busy` state machine
13. [ ] plugin inspect / TypeScript declaration generation

중요:

> 내부 리팩터링 때문에 현재 public API regression이나 CEF/WebView2 browser contract가 변경되어서는 안 된다.


## 14. Implemented C ABI v1 baseline

현재 구현된 binary boundary:

```c
nativeweb_plugin_init_v1(nw_plugin_api_v1* out)
```

Plugin은 descriptor에 다음을 제공한다.

- `abi_version`
- `id`
- `version`
- `plugin_context`
- `register_api`
- optional `shutdown`

Host는 `nw_host_api_v1::register_function`을 통해 plugin의 상대 method name을 받으며, 실제 registry 이름은:

```text
<plugin-id>.<relative-name>
```

으로 등록한다.

현재 value ABI v1 baseline:

- null
- bool
- int32
- double
- string view
- binary view

string/binary input view는 call 동안만 유효하다.
plugin output string/binary/error view는 callback이 반환할 때까지만 유효하면 되고 NativeWeb이 callback 반환 전에 복사한다.

아직 ABI v1 baseline에 넣지 않은 것:

- list/map
- native object handle
- async callback/future ABI
- event ABI
- capability/permission descriptor
- runtime min/max compatibility descriptor

이 항목들은 C ABI struct versioning/size compatibility를 유지하면서 확장한다.

## 15. Implemented module lifetime baseline

`PluginModule`은 registration token들과 library lease를 분리한다.

```text
PluginModule
  ├─ RegistrationToken[]
  └─ PluginLibraryLease
          └─ SharedLibrary

BindingRegistry
  └─ Callable
       └─ strong PluginLibraryLease
```

Module destructor는 먼저 registration을 제거한다.

이미 registry에서 복사되어 실행 중이거나 외부에서 보유 중인 `Callable`은 strong lease를 보유하므로 DLL/SO code가 premature unload되지 않는다.

실제 regression은 다음을 검증한다.

1. real DLL/SO load
2. C ABI init
3. plugin id/version 확인
4. `sample.add` registration/call
5. plugin error -> NativeWeb structured Error
6. ABI mismatch reject
7. missing init symbol reject
8. module destruction -> registry removal
9. module destruction 이후 retained Callable 호출 성공

queued/running worker task는 real DLL/SO regression으로 검증됐다.

native object 쪽은 generic foundation까지 구현됐다:

```text
ObjectRegistry Entry
  - shared_ptr<void> object
  - shared_ptr<void> owner

get<T>()
  -> aliasing shared_ptr<T>
  -> object + owner state를 함께 retain
```

따라서 registry entry가 release된 뒤에도 이미 전달된 managed pointer가 살아 있으면 owner lease도 같이 살아 있다.

아직 남은 것은 C ABI가 plugin-created native object/factory를 표현하도록 확장하고,
그 실제 DLL/SO object가 이 owner lease를 사용하는 end-to-end regression이다.


## 16. Revised lifetime-first implementation order

현재 function-call C ABI vertical slice는 실제 Windows DLL / Linux SO에서 검증되었다.
따라서 다음 단계는 API convenience보다 lifetime correctness를 먼저 증명한다.

우선순위:

1. queued/running worker task가 copied `Callable`을 통해 `PluginLibraryLease`를 보유하는지 real plugin으로 검증
2. plugin-created native object가 module lease를 보유하도록 ObjectRegistry/NativeObjectRuntime ownership 확장
3. 그 위에 C++ `Plugin` / `PluginApi` wrapper와 `NATIVEWEB_PLUGIN(...)` macro 제공
4. explicit unload는 active calls/live objects/registrations가 모두 안전할 때만 허용
5. overload helper와 TypeScript/codegen은 lifetime/ABI foundation 이후 진행

이 순서는 사용자-facing API를 변경하지 않는다.

```cpp
webview.bind(...);
webview.bind("apple", ...).method(...);
```

는 계속 동일하며 plugin convenience API도 stable C ABI를 감싸는 additive layer로만 추가한다.


## 17. Implemented C++ plugin convenience layer

public header:

```text
include/nativeweb/plugin.hpp
```

사용 예:

```cpp
class CameraPlugin : public nativeweb::Plugin {
public:
    void registerApi(nativeweb::PluginApi& api) override {
        api.bind("open", ...);
        api.bind("capture", ...);
    }
};

NATIVEWEB_PLUGIN(CameraPlugin, "camera")
```

특징:

- header-only convenience layer
- plugin shared library가 NativeWeb Core에 링크할 필요 없음
- 기존 `plugin_abi.h` v1 위에서 동작
- macro는 well-known `nativeweb_plugin_init_v1` C symbol을 export
- C++ exception은 C ABI 밖으로 나가지 않고 structured error로 변환
- v1 wrapper baseline은 null/bool/int32/double/string/binary를 사용
- function registration lifetime은 PluginExportContext가 소유

실제 regression은 C++ wrapper로 작성한 DLL/SO를 빌드하고 PluginModule로 다시 로드해
add/string/binary/error mapping과 teardown unregister를 검증한다.

## Implementation note (2026-10-12)

The previous pending checklist reflects the historical design stage.
`PluginModule` now implements C ABI v1 optional object factories/object
methods and explicit quiescing `unload()` with `plugin_busy` checks; Linux SO
regressions are green. WebView plugin-manager wiring and real-browser
integration remain open. See `PLUGIN_OBJECT_LIFETIME.md`.
