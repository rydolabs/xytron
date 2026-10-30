cmake_minimum_required(VERSION 3.21)

project(NativeWebCefIntegration LANGUAGES C CXX)

if(NOT DEFINED CEF_ROOT OR CEF_ROOT STREQUAL "")
  message(FATAL_ERROR "CEF_ROOT must point to the extracted CEF distribution")
endif()

get_filename_component(
  NATIVEWEB_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../.."
  ABSOLUTE
)

# Normalize command-line paths before CEF's CMake modules propagate them into
# nested try_compile projects. Raw Windows backslashes such as D:\a\... can
# otherwise be parsed as CMake escape sequences (for example \a).
file(TO_CMAKE_PATH "${CEF_ROOT}" CEF_ROOT)
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

find_package(CEF REQUIRED)

if(NOT WIN32)
  find_package(Threads REQUIRED)
endif()

add_subdirectory(
  "${CEF_LIBCEF_DLL_WRAPPER_PATH}"
  "${CMAKE_BINARY_DIR}/libcef_dll_wrapper"
)

ADD_LOGICAL_TARGET(
  "libcef_lib"
  "${CEF_LIB_DEBUG}"
  "${CEF_LIB_RELEASE}"
)

include("${NATIVEWEB_ROOT}/cmake/NativeWebCoreSources.cmake")
add_library(nativeweb_core_for_cef STATIC ${NATIVEWEB_CORE_SOURCES})

target_include_directories(nativeweb_core_for_cef
  PUBLIC
    "${NATIVEWEB_ROOT}/include"
  PRIVATE
    "${NATIVEWEB_ROOT}/src"
)

set_target_properties(nativeweb_core_for_cef PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

# CEF's Windows targets use the static MSVC runtime. nativeweb_core_for_cef is
# our own integration target and does not pass through CEF's target-property
# macros, so match its CRT explicitly to avoid /MD + /MT linker conflicts.
if(MSVC)
  set_property(
    TARGET nativeweb_core_for_cef
    PROPERTY MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()

if(NOT WIN32)
  target_link_libraries(nativeweb_core_for_cef
    PUBLIC
      Threads::Threads
      ${CMAKE_DL_LIBS}
  )
endif()

# CEF intentionally builds its own wrapper with C++ exceptions disabled.
# NativeWeb's public/runtime contract converts user C++ exceptions into
# structured Promise errors, so do not propagate CEF's _HAS_EXCEPTIONS=0
# policy into NativeWeb-owned backend/host targets.
if(MSVC)
  list(REMOVE_ITEM CEF_COMPILER_DEFINES _HAS_EXCEPTIONS=0)
endif()

add_library(nativeweb_cef_backend STATIC
  "${NATIVEWEB_ROOT}/src/browser/cef/cef_value_converter.cpp"
  "${NATIVEWEB_ROOT}/src/browser/cef/cef_renderer_app.cpp"
  "${NATIVEWEB_ROOT}/src/browser/cef/cef_backend.cpp"
)

target_include_directories(nativeweb_cef_backend
  PUBLIC
    "${NATIVEWEB_ROOT}/include"
  PRIVATE
    "${NATIVEWEB_ROOT}/src"
    "${CEF_ROOT}"
)

SET_LIBRARY_TARGET_PROPERTIES(nativeweb_cef_backend)

# Test-only renderer IPC injection; never enable for redistributable CEF.
target_compile_definitions(nativeweb_cef_backend PRIVATE
  NATIVEWEB_CEF_ENABLE_E2E_REPLAY_HOOKS=1
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nativeweb_cef_backend PRIVATE -frtti -fexceptions)
elseif(MSVC)
  target_compile_options(nativeweb_cef_backend PRIVATE /EHsc)
  target_compile_definitions(nativeweb_cef_backend PRIVATE _HAS_EXCEPTIONS=1)
endif()

target_link_libraries(nativeweb_cef_backend
  PUBLIC
    nativeweb_core_for_cef
  PRIVATE
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
)

set(TARGET nativeweb_cef_bridge_integration)

add_executable(${TARGET}
  main.cpp
)

if(WIN32)
  target_sources(${TARGET} PRIVATE nativeweb_cef_windows_manifest.rc)
endif()

target_include_directories(${TARGET}
  PRIVATE
    "${NATIVEWEB_ROOT}/include"
    "${NATIVEWEB_ROOT}/src"
    "${CEF_ROOT}"
)

SET_EXECUTABLE_TARGET_PROPERTIES(${TARGET})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(${TARGET} PRIVATE -frtti -fexceptions)
elseif(MSVC)
  target_compile_options(${TARGET} PRIVATE /EHsc)
  target_compile_definitions(${TARGET} PRIVATE _HAS_EXCEPTIONS=1)
endif()

target_link_libraries(${TARGET}
  PRIVATE
    nativeweb_cef_backend
    nativeweb_core_for_cef
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
)

set(CEF_TARGET_OUT_DIR "${CMAKE_BINARY_DIR}/bin")
set_target_properties(${TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CEF_TARGET_OUT_DIR}"
)

COPY_FILES(
  "${TARGET}"
  "${CEF_BINARY_FILES}"
  "${CEF_BINARY_DIR}"
  "${CEF_TARGET_OUT_DIR}"
)

COPY_FILES(
  "${TARGET}"
  "${CEF_RESOURCE_FILES}"
  "${CEF_RESOURCE_DIR}"
  "${CEF_TARGET_OUT_DIR}"
)



# Generated app host that serves exactly one project app:// UI entry.
add_executable(xytron_desktop_host
  "${NATIVEWEB_ROOT}/tools/cli/real_desktop_host.cpp")
if(WIN32)
  target_sources(xytron_desktop_host PRIVATE nativeweb_cef_windows_manifest.rc)
endif()
target_include_directories(xytron_desktop_host PRIVATE
  "${NATIVEWEB_ROOT}/include" "${NATIVEWEB_ROOT}/src" "${CEF_ROOT}")
target_compile_definitions(xytron_desktop_host PRIVATE XYTRON_APP_HOST_CEF=1)
SET_EXECUTABLE_TARGET_PROPERTIES(xytron_desktop_host)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(xytron_desktop_host PRIVATE -frtti -fexceptions)
elseif(MSVC)
  target_compile_options(xytron_desktop_host PRIVATE /EHsc)
  target_compile_definitions(xytron_desktop_host PRIVATE _HAS_EXCEPTIONS=1)
endif()
target_link_libraries(xytron_desktop_host PRIVATE
  nativeweb_cef_backend nativeweb_core_for_cef
  libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
set_target_properties(xytron_desktop_host PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CEF_TARGET_OUT_DIR}")
add_dependencies(xytron_desktop_host nativeweb_cef_bridge_integration)

set(MULTI_TARGET nativeweb_cef_multi_webview_integration)

add_executable(${MULTI_TARGET}
  multi_webview.cpp
)

if(WIN32)
  target_sources(${MULTI_TARGET} PRIVATE nativeweb_cef_windows_manifest.rc)
endif()

target_include_directories(${MULTI_TARGET}
  PRIVATE
    "${NATIVEWEB_ROOT}/include"
    "${NATIVEWEB_ROOT}/src"
    "${CEF_ROOT}"
)

SET_EXECUTABLE_TARGET_PROPERTIES(${MULTI_TARGET})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(${MULTI_TARGET} PRIVATE -frtti -fexceptions)
elseif(MSVC)
  target_compile_options(${MULTI_TARGET} PRIVATE /EHsc)
  target_compile_definitions(${MULTI_TARGET} PRIVATE _HAS_EXCEPTIONS=1)
endif()

target_link_libraries(${MULTI_TARGET}
  PRIVATE
    nativeweb_cef_backend
    nativeweb_core_for_cef
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
)

set_target_properties(${MULTI_TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CEF_TARGET_OUT_DIR}"
)

# Both executables share one runtime directory. Running two CEF COPY_FILES
# post-build rules in parallel races on bin/locales under MSBuild (NW-009).
# The bridge target owns the runtime copy; multi-WebView depends on it so
# building multi-WebView also finishes runtime deployment first.
add_dependencies(${MULTI_TARGET} ${TARGET})

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
    --engine cef --build-dir "${CMAKE_BINARY_DIR}"
)
set_tests_properties(browser.plugin_object_lifecycle PROPERTIES TIMEOUT 150)

# Real in-memory app:// scheme test: same JS->native plugin E2E,
# but host entry and child fixtures are served without file:// navigation.
add_test(
  NAME browser.app_resource_lifecycle
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine cef --build-dir "${CMAKE_BINARY_DIR}" --app-resources
)
set_tests_properties(browser.app_resource_lifecycle PROPERTIES TIMEOUT 150)


# Actual JavaScript -> engine bridge -> C++11 ProcessBroker -> Python object host.
add_test(
  NAME browser.managed_python
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine cef --build-dir "${CMAKE_BINARY_DIR}" --managed
)
set_tests_properties(browser.managed_python PROPERTIES TIMEOUT 150)

# Real browser -> C++ ProcessBroker -> .NET 8 worker (requires built test assembly).
add_test(
  NAME browser.managed_dotnet
  COMMAND "${Python3_EXECUTABLE}"
    "${NATIVEWEB_ROOT}/tools/run_browser_plugin_e2e.py"
    --engine cef --build-dir "${CMAKE_BINARY_DIR}" --managed-dotnet
)
set_tests_properties(browser.managed_dotnet PROPERTIES TIMEOUT 150)
