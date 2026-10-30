cmake_minimum_required(VERSION 3.21)

project(NativeWebWebView2Integration LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "NativeWeb WebView2 integration requires Windows")
endif()

if(NOT DEFINED WEBVIEW2_ROOT OR WEBVIEW2_ROOT STREQUAL "")
  message(FATAL_ERROR "WEBVIEW2_ROOT must point to an extracted Microsoft.Web.WebView2 NuGet package")
endif()

get_filename_component(
  NATIVEWEB_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../.."
  ABSOLUTE
)

find_package(Threads REQUIRED)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WEBVIEW2_ARCH x64)
else()
  set(WEBVIEW2_ARCH x86)
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_ROOT}/build/native/include")
set(
  WEBVIEW2_LOADER_STATIC
  "${WEBVIEW2_ROOT}/build/native/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"
)

if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
  message(FATAL_ERROR "WebView2.h not found under ${WEBVIEW2_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${WEBVIEW2_LOADER_STATIC}")
  message(FATAL_ERROR "WebView2 static loader not found: ${WEBVIEW2_LOADER_STATIC}")
endif()

include("${NATIVEWEB_ROOT}/cmake/NativeWebCoreSources.cmake")
add_library(nativeweb_core_for_webview2 STATIC ${NATIVEWEB_CORE_SOURCES})

target_include_directories(nativeweb_core_for_webview2
  PUBLIC
    "${NATIVEWEB_ROOT}/include"
  PRIVATE
    "${NATIVEWEB_ROOT}/src"
)

target_compile_features(nativeweb_core_for_webview2 PUBLIC cxx_std_11)
target_link_libraries(nativeweb_core_for_webview2 PUBLIC Threads::Threads)

add_library(nativeweb_webview2_backend STATIC
  "${NATIVEWEB_ROOT}/src/browser/webview2/webview2_backend.cpp"
)

target_include_directories(nativeweb_webview2_backend
  PUBLIC
    "${NATIVEWEB_ROOT}/include"
  PRIVATE
    "${NATIVEWEB_ROOT}/src"
    "${WEBVIEW2_INCLUDE_DIR}"
)

target_compile_features(nativeweb_webview2_backend PUBLIC cxx_std_11)

target_compile_definitions(nativeweb_webview2_backend
  PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(nativeweb_webview2_backend
  PUBLIC
    nativeweb_core_for_webview2
  PRIVATE
    "${WEBVIEW2_LOADER_STATIC}"
    ole32
    user32
    version
    shlwapi
    psapi
)

add_executable(nativeweb_webview2_bridge_integration
  main.cpp
)

target_include_directories(nativeweb_webview2_bridge_integration
  PRIVATE
    "${NATIVEWEB_ROOT}/include"
    "${NATIVEWEB_ROOT}/src"
    "${WEBVIEW2_INCLUDE_DIR}"
)

target_compile_features(nativeweb_webview2_bridge_integration PUBLIC cxx_std_11)

target_compile_definitions(nativeweb_webview2_bridge_integration
  PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(nativeweb_webview2_bridge_integration
  PRIVATE
    nativeweb_webview2_backend
    nativeweb_core_for_webview2
    user32
)

set_target_properties(
  nativeweb_webview2_bridge_integration
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)



# Project app:// UI host, using the same real WebView2 backend as E2E.
add_executable(xytron_desktop_host
  "${NATIVEWEB_ROOT}/tools/cli/real_desktop_host.cpp")
target_include_directories(xytron_desktop_host PRIVATE
  "${NATIVEWEB_ROOT}/include" "${NATIVEWEB_ROOT}/src"
  "${WEBVIEW2_INCLUDE_DIR}")
target_compile_features(xytron_desktop_host PRIVATE cxx_std_11)
target_compile_definitions(xytron_desktop_host PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(xytron_desktop_host PRIVATE
  nativeweb_webview2_backend nativeweb_core_for_webview2 user32)
set_target_properties(xytron_desktop_host PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

add_executable(nativeweb_webview2_multi_webview_integration
  multi_webview.cpp
)

target_include_directories(nativeweb_webview2_multi_webview_integration
  PRIVATE
    "${NATIVEWEB_ROOT}/include"
    "${NATIVEWEB_ROOT}/src"
    "${WEBVIEW2_INCLUDE_DIR}"
)

target_compile_features(nativeweb_webview2_multi_webview_integration PUBLIC cxx_std_11)

target_compile_definitions(nativeweb_webview2_multi_webview_integration
  PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(nativeweb_webview2_multi_webview_integration
  PRIVATE
    nativeweb_webview2_backend
    nativeweb_core_for_webview2
    user32
)

set_target_properties(
  nativeweb_webview2_multi_webview_integration
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# Real SO/DLL used by --plugin-path E2E. Never replace with a mock module.
add_library(nativeweb_plugin_browser_fixture SHARED
  "${NATIVEWEB_ROOT}/tests/plugin/object_factory_plugin.cpp"
)
target_include_directories(nativeweb_plugin_browser_fixture PRIVATE
  "${NATIVEWEB_ROOT}/include"
)
set_target_properties(nativeweb_plugin_browser_fixture PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# This is an opt-in external SDK build, so the browser E2E is mandatory
# whenever this integration project is configured and CTest is run.
include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
  NAME browser.plugin_object_lifecycle
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine webview2 --build-dir "${CMAKE_BINARY_DIR}"
)
set_tests_properties(browser.plugin_object_lifecycle PROPERTIES TIMEOUT 150)

add_test(
  NAME browser.app_resource_lifecycle
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine webview2 --build-dir "${CMAKE_BINARY_DIR}" --app-resources
)
set_tests_properties(browser.app_resource_lifecycle PROPERTIES TIMEOUT 150)


# Actual JavaScript -> engine bridge -> C++11 ProcessBroker -> Python object host.
add_test(
  NAME browser.managed_python
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine webview2 --build-dir "${CMAKE_BINARY_DIR}" --managed
)
set_tests_properties(browser.managed_python PROPERTIES TIMEOUT 150)

# Real browser -> C++ ProcessBroker -> .NET 8 worker (requires built test assembly).
add_test(
  NAME browser.managed_dotnet
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine webview2 --build-dir "${CMAKE_BINARY_DIR}" --managed-dotnet
)
set_tests_properties(browser.managed_dotnet PROPERTIES TIMEOUT 150)

# Actual WebView2 shared memory probe (test-only internal adapter API).
add_executable(nativeweb_webview2_sharedbuffer_probe
  sharedbuffer_probe.cpp
)
target_include_directories(nativeweb_webview2_sharedbuffer_probe PRIVATE
  "${NATIVEWEB_ROOT}/include"
  "${NATIVEWEB_ROOT}/src"
  "${WEBVIEW2_INCLUDE_DIR}"
)
target_compile_features(nativeweb_webview2_sharedbuffer_probe PRIVATE cxx_std_11)
target_compile_definitions(nativeweb_webview2_sharedbuffer_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(nativeweb_webview2_sharedbuffer_probe PRIVATE
  nativeweb_webview2_backend nativeweb_core_for_webview2 user32
)
set_target_properties(nativeweb_webview2_sharedbuffer_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME browser.webview2_sharedbuffer_probe
  COMMAND nativeweb_webview2_sharedbuffer_probe)
set_tests_properties(browser.webview2_sharedbuffer_probe PROPERTIES TIMEOUT 100)

# Crash the actual WebView2 main renderer via DevTools, then exercise
# ProcessFailed grant revocation and explicit host-initiated Reload recovery.
add_executable(nativeweb_webview2_process_failure_probe
  process_failure_probe.cpp
)
target_include_directories(nativeweb_webview2_process_failure_probe PRIVATE
  "${NATIVEWEB_ROOT}/include"
  "${NATIVEWEB_ROOT}/src"
  "${WEBVIEW2_INCLUDE_DIR}"
)
target_compile_features(nativeweb_webview2_process_failure_probe PRIVATE cxx_std_11)
target_compile_definitions(nativeweb_webview2_process_failure_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(nativeweb_webview2_process_failure_probe PRIVATE
  nativeweb_webview2_backend nativeweb_core_for_webview2 user32
)
set_target_properties(nativeweb_webview2_process_failure_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME browser.webview2_process_failure_probe
  COMMAND nativeweb_webview2_process_failure_probe)
set_tests_properties(browser.webview2_process_failure_probe PROPERTIES TIMEOUT 90)

# Two real WebView2 instances with independent request ids, grants and
# SharedBuffer quotas. Reload one while the other retains an outstanding grant.
add_executable(nativeweb_webview2_sharedbuffer_multi_probe
  sharedbuffer_multi_probe.cpp
)
target_include_directories(nativeweb_webview2_sharedbuffer_multi_probe PRIVATE
  "${NATIVEWEB_ROOT}/include"
  "${NATIVEWEB_ROOT}/src"
  "${WEBVIEW2_INCLUDE_DIR}"
)
target_compile_features(nativeweb_webview2_sharedbuffer_multi_probe PRIVATE cxx_std_11)
target_compile_definitions(nativeweb_webview2_sharedbuffer_multi_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(nativeweb_webview2_sharedbuffer_multi_probe PRIVATE
  nativeweb_webview2_backend nativeweb_core_for_webview2 user32
)
set_target_properties(nativeweb_webview2_sharedbuffer_multi_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME browser.webview2_sharedbuffer_multi_probe
  COMMAND nativeweb_webview2_sharedbuffer_multi_probe)
set_tests_properties(browser.webview2_sharedbuffer_multi_probe PROPERTIES TIMEOUT 80)

# Bounded per-document memory-pressure smoke, with an opt-in longer run.
add_executable(nativeweb_webview2_sharedbuffer_soak_probe sharedbuffer_soak_probe.cpp)
target_include_directories(nativeweb_webview2_sharedbuffer_soak_probe PRIVATE
  "${NATIVEWEB_ROOT}/include" "${NATIVEWEB_ROOT}/src" "${WEBVIEW2_INCLUDE_DIR}")
target_compile_features(nativeweb_webview2_sharedbuffer_soak_probe PRIVATE cxx_std_11)
target_compile_definitions(nativeweb_webview2_sharedbuffer_soak_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(nativeweb_webview2_sharedbuffer_soak_probe PRIVATE
  nativeweb_webview2_backend nativeweb_core_for_webview2 user32 psapi)
set_target_properties(nativeweb_webview2_sharedbuffer_soak_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
add_test(NAME browser.webview2_sharedbuffer_soak_probe
  COMMAND nativeweb_webview2_sharedbuffer_soak_probe --cycles 4)
set_tests_properties(browser.webview2_sharedbuffer_soak_probe PROPERTIES TIMEOUT 110)

# Browser-process crash recovery requires a UNIQUE user data folder.
# Never kill the main browser process attached to ordinary E2E tests.
add_executable(nativeweb_webview2_browser_process_crash_probe
  browser_process_crash_probe.cpp)
target_include_directories(nativeweb_webview2_browser_process_crash_probe PRIVATE
  "${NATIVEWEB_ROOT}/include" "${NATIVEWEB_ROOT}/src" "${WEBVIEW2_INCLUDE_DIR}")
target_compile_features(nativeweb_webview2_browser_process_crash_probe PRIVATE cxx_std_11)
target_compile_definitions(nativeweb_webview2_browser_process_crash_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(nativeweb_webview2_browser_process_crash_probe PRIVATE
  nativeweb_webview2_backend nativeweb_core_for_webview2 user32)
set_target_properties(nativeweb_webview2_browser_process_crash_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
add_test(NAME browser.webview2_browser_process_crash_probe
  COMMAND nativeweb_webview2_browser_process_crash_probe)
set_tests_properties(browser.webview2_browser_process_crash_probe PROPERTIES TIMEOUT 110)
