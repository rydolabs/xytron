# Xytron public C++ API naming compatibility

## Scope of this change

This incremental change is part of the public-product repository migration.
It **does not** constitute approval to merge, release, publish or change the
stable native plugin ABI, the shipped browser engines, or the security model.

New consumers can use:

```cpp
#include <xytron/xytron.hpp>
xytron::WebView view;
xytron::WebViewOptions options;
options.engine = xytron::Engine::Auto;
```

```cmake
find_package(Xytron CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE Xytron::Core)
```

The existing paths remain supported:
`<nativeweb/nativeweb.hpp>`, `nativeweb::WebView`,
`find_package(NativeWeb CONFIG REQUIRED)`, and `NativeWeb::Core`.

The Xytron C++ types are **using-declarations of the original types**. They
do not introduce new ownership, vtables, symbol names or ABIs. The build-tree
target `Xytron::Core` aliases the same `nativeweb_core` target, while the
installed `XytronConfig.cmake` imports `NativeWeb::Core` transitively.
Both CMake packages are installed under the same relocatable SDK prefix.

Plugin authors use `<xytron/plugin.hpp>` and `XYTRON_MODULE(m)` with
`XYTRON_PLUGIN_ID` defined by the plugin manifest/build. The umbrella
`<xytron/xytron.hpp>` intentionally does not include plugin.hpp because
ordinary WebView consumers do not have that build definition. The exported
C ABI entry point is still `nativeweb_plugin_init_v1`, and all v1 C structs,
function signatures and plugin loading behavior remain unchanged.

## Verification

- `examples/sdk_consumer/main.cpp` preserves the old API regression.
- `examples/sdk_consumer/xytron_main.cpp` compiles the branded API against
  a relocated *installed* SDK and verifies shared type identity with
  C++11 `static_assert`.
- `.github/workflows/sdk-package.yml` runs the independent consumer on
  Linux and Windows.
- Browser E2E and the full Scope A1 release gates remain separate; passing
  this test is not evidence of redistributable browser runtime packaging.

## Migration discipline

1. Introduce branded public symbols without removing legacy exports.
2. Keep `nativeweb::` implementation and the versioned plugin C ABI stable.
3. Defer any internal namespace or file relocation to a separately reviewed
   ABI-sensitive change, if needed.
4. Do not remove compatibility headers or CMake package aliases in the first
   release without an explicit compatibility decision.
