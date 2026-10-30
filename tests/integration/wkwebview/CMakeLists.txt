cmake_minimum_required(VERSION 3.16)
project(NativeWebWKWebViewApiProbe LANGUAGES CXX OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "The WKWebView API probe requires macOS")
endif()

find_library(NATIVEWEB_APPKIT_FRAMEWORK AppKit)
find_library(NATIVEWEB_WEBKIT_FRAMEWORK WebKit)
if(NOT NATIVEWEB_APPKIT_FRAMEWORK OR NOT NATIVEWEB_WEBKIT_FRAMEWORK)
  message(FATAL_ERROR "macOS AppKit/WebKit frameworks are unavailable")
endif()

# Deliberately separate from the Core SDK: this is only the Apple adapter
# compile/link feasibility gate, NOT a working WKWebView browser backend.
add_executable(nativeweb_wkwebview_api_probe wkwebview_api_probe.mm)
target_compile_options(nativeweb_wkwebview_api_probe PRIVATE -fobjc-arc)
target_link_libraries(nativeweb_wkwebview_api_probe PRIVATE
  "${NATIVEWEB_APPKIT_FRAMEWORK}" "${NATIVEWEB_WEBKIT_FRAMEWORK}")
set_target_properties(nativeweb_wkwebview_api_probe PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  OBJCXX_STANDARD 11
  OBJCXX_STANDARD_REQUIRED YES
  OBJCXX_EXTENSIONS NO)

# Real browser integration: Core + Objective-C++ adapter linked into one
# process. This target uses WKWebView, not mocks or the API-only probe.
get_filename_component(NATIVEWEB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_package(Threads REQUIRED)
include("${NATIVEWEB_ROOT}/cmake/NativeWebCoreSources.cmake")
add_library(nativeweb_wkcore STATIC ${NATIVEWEB_CORE_SOURCES})
target_include_directories(nativeweb_wkcore PUBLIC
    "${NATIVEWEB_ROOT}/include" PRIVATE "${NATIVEWEB_ROOT}/src")
target_link_libraries(nativeweb_wkcore PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
target_compile_features(nativeweb_wkcore PUBLIC cxx_std_11)

add_library(nativeweb_wkbackend STATIC
    "${NATIVEWEB_ROOT}/src/browser/wkwebview/wkwebview_backend.mm")
target_include_directories(nativeweb_wkbackend PUBLIC
    "${NATIVEWEB_ROOT}/src" "${NATIVEWEB_ROOT}/include")
target_compile_options(nativeweb_wkbackend PRIVATE -fobjc-arc)
target_link_libraries(nativeweb_wkbackend PUBLIC nativeweb_wkcore
    "${NATIVEWEB_APPKIT_FRAMEWORK}" "${NATIVEWEB_WEBKIT_FRAMEWORK}")
set_target_properties(nativeweb_wkbackend PROPERTIES
    CXX_STANDARD 11 OBJCXX_STANDARD 11)

add_executable(nativeweb_wkwebview_real_e2e bridge_e2e.mm)
target_include_directories(nativeweb_wkwebview_real_e2e PRIVATE
    "${NATIVEWEB_ROOT}/src" "${NATIVEWEB_ROOT}/include")
target_compile_options(nativeweb_wkwebview_real_e2e PRIVATE -fobjc-arc)
target_link_libraries(nativeweb_wkwebview_real_e2e PRIVATE nativeweb_wkbackend)
set_target_properties(nativeweb_wkwebview_real_e2e PROPERTIES
    CXX_STANDARD 11 OBJCXX_STANDARD 11)
enable_testing()
add_test(NAME browser.wkwebview_bridge COMMAND nativeweb_wkwebview_real_e2e)
set_tests_properties(browser.wkwebview_bridge PROPERTIES TIMEOUT 35)

# Shares the existing real backend but runs independent benchmark HTML.
add_executable(nativeweb_wkwebview_benchmark bridge_benchmark.mm)
target_include_directories(nativeweb_wkwebview_benchmark PRIVATE
    "${NATIVEWEB_ROOT}/src" "${NATIVEWEB_ROOT}/include")
target_compile_options(nativeweb_wkwebview_benchmark PRIVATE -fobjc-arc)
target_link_libraries(nativeweb_wkwebview_benchmark PRIVATE nativeweb_wkbackend)
set_target_properties(nativeweb_wkwebview_benchmark PROPERTIES
    CXX_STANDARD 11 OBJCXX_STANDARD 11)
