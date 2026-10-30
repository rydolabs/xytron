# Xytron

**Xytron** is a C++11-based native desktop application framework and SDK
for HTML/CSS/JavaScript frontends and user-authored C++, Python, or C# backends.

> **Development snapshot — not a release.** Scope A1 remains **NO-GO**.
> This repository contains verified foundations and integration tests, but
> redistributable runtime bundles, automatic engine restore, native menus,
> full sandbox/security acceptance, clean-machine publishing, and third-party
> redistribution review are not complete.

## Code layout

| Path | Purpose |
| --- | --- |
| `include/xytron/` | Branded public C++ API (`xytron::WebView`, module authoring) |
| `include/nativeweb/` | Compatibility/Core API and stable C plugin ABI |
| `src/` | C++ Core and CEF/WebView2/WKWebView integration |
| `bindings/` | Python and .NET backend host/binding code |
| `tools/cli/` | Native `xytron` CLI and developer workflows |
| `tools/cef/` | Pinned CEF bootstrap and diagnostics |
| `examples/` | Installed SDK consumer examples |
| `tests/` | Core, plugin, managed IPC, real-browser integration |
| `docs/` | Scope, acceptance, architecture, security and verification |
| `.github/workflows/` | CI checks; not a product release pipeline |

## C++ Core API (installed SDK)

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

Legacy `nativeweb::WebView`, `NativeWeb::Core`, and the stable
`nativeweb_plugin_init_v1` plugin ABI are retained. See
[compatibility](docs/XYTRON_PUBLIC_API_COMPATIBILITY.md).

## Local build and Core SDK consumer smoke

Requirements: a C++11 compiler and CMake. Additional browser tests need
their platform-specific CEF/WebView2 dependencies, Python and/or .NET.

```sh
cmake -S . -B build -DNATIVEWEB_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The `Installed SDK Consumer` workflow also installs and relocates the
Core-only SDK, then tests the legacy and Xytron CMake consumers.
**CEF and WebView2 binaries are not part of this Core package.**

## Release status and project governance

- [Approved Scope A1](docs/RELEASE_SCOPE_A.md)
- [Release acceptance criteria](docs/ACCEPTANCE_CRITERIA.md)
- [Test matrix](docs/RELEASE_TEST_MATRIX.md)
- [SDK packaging status](docs/SDK_PACKAGING.md)
- [Source migration provenance](MIGRATION_SOURCE.md)
- [Contributor guidelines](CONTRIBUTING.md)

Do not treat passing CI as approval for publishing production binaries.
The open licensing/notice work and redistribution conditions must be
resolved before a product release. No GitHub Release is authorized by this
source import alone.

## Source provenance

Imported from a verified Xytron development snapshot in
[nssang00/chatgpt-write-test](https://github.com/nssang00/chatgpt-write-test),
source commit `c3c208b20d160982508d0756a2c78bdc72e77cd7`.
The imported source is staged on a Draft PR; original project history
and Draft PRs remain in the original repository.
