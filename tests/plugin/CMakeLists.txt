add_library(nativeweb_test_shared_library SHARED
    sample_shared_library.cpp
)

set_target_properties(nativeweb_test_shared_library PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_executable(nativeweb_shared_library_regression
    shared_library_regression.cpp
)

target_link_libraries(nativeweb_shared_library_regression
    PRIVATE
        NativeWeb::Core
)

target_include_directories(nativeweb_shared_library_regression
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

set_target_properties(nativeweb_shared_library_regression PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_test(
    NAME plugin.shared_library
    COMMAND
        nativeweb_shared_library_regression
        $<TARGET_FILE:nativeweb_test_shared_library>
)


add_library(nativeweb_test_plugin SHARED
    sample_plugin.cpp
)

target_include_directories(nativeweb_test_plugin
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(nativeweb_test_plugin PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_library(nativeweb_test_bad_abi_plugin SHARED
    bad_abi_plugin.cpp
)

target_include_directories(nativeweb_test_bad_abi_plugin
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(nativeweb_test_bad_abi_plugin PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_executable(nativeweb_plugin_module_regression
    plugin_module_regression.cpp
)

target_link_libraries(nativeweb_plugin_module_regression
    PRIVATE
        NativeWeb::Core
)

target_include_directories(nativeweb_plugin_module_regression
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

set_target_properties(nativeweb_plugin_module_regression PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_test(
    NAME plugin.module
    COMMAND
        nativeweb_plugin_module_regression
        $<TARGET_FILE:nativeweb_test_plugin>
        $<TARGET_FILE:nativeweb_test_bad_abi_plugin>
        $<TARGET_FILE:nativeweb_test_shared_library>
)


add_library(nativeweb_test_cpp_plugin SHARED
    sample_cpp_plugin.cpp
)

target_include_directories(nativeweb_test_cpp_plugin
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(nativeweb_test_cpp_plugin PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_executable(nativeweb_cpp_plugin_wrapper_regression
    cpp_plugin_wrapper_regression.cpp
)

target_link_libraries(nativeweb_cpp_plugin_wrapper_regression
    PRIVATE
        NativeWeb::Core
)

target_include_directories(nativeweb_cpp_plugin_wrapper_regression
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

set_target_properties(nativeweb_cpp_plugin_wrapper_regression PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_test(
    NAME plugin.cpp_wrapper
    COMMAND
        nativeweb_cpp_plugin_wrapper_regression
        $<TARGET_FILE:nativeweb_test_cpp_plugin>
)

add_library(nativeweb_test_object_factory_plugin SHARED object_factory_plugin.cpp)
target_include_directories(nativeweb_test_object_factory_plugin PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(nativeweb_test_object_factory_plugin PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_executable(nativeweb_object_factory_regression object_factory_regression.cpp)
target_link_libraries(nativeweb_object_factory_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_object_factory_regression PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(nativeweb_object_factory_regression PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME plugin.object_factory COMMAND nativeweb_object_factory_regression $<TARGET_FILE:nativeweb_test_object_factory_plugin>)

add_executable(nativeweb_webview_plugin_regression webview_plugin_regression.cpp)
target_link_libraries(nativeweb_webview_plugin_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_webview_plugin_regression PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(nativeweb_webview_plugin_regression PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME plugin.webview_facade COMMAND nativeweb_webview_plugin_regression
    $<TARGET_FILE:nativeweb_test_object_factory_plugin>)

add_executable(nativeweb_plugin_async_lifetime_regression plugin_async_lifetime_regression.cpp)
target_link_libraries(nativeweb_plugin_async_lifetime_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_plugin_async_lifetime_regression PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(nativeweb_plugin_async_lifetime_regression PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME plugin.async_lifetime COMMAND nativeweb_plugin_async_lifetime_regression
    $<TARGET_FILE:nativeweb_test_object_factory_plugin>)


# New C++11 developer-facing XYTRON_MODULE function registration.
# Compile-time ID/version derived from one plugin.json (not user source).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/xytron_function_plugin.json" _manifest)
string(REGEX MATCH "\"name\"[ \t]*:[ \t]*\"([a-z][a-z0-9_]*)\"" _id_match "${_manifest}")
set(_fixture_id "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"version\"[ \t]*:[ \t]*\"([0-9]+[.][0-9]+[.][0-9]+)\"" _version_match "${_manifest}")
set(_fixture_version "${CMAKE_MATCH_1}")
if(NOT _fixture_id OR NOT _fixture_version)
    message(FATAL_ERROR "Invalid binding plugin fixture metadata")
endif()
add_library(nativeweb_test_xytron_function_plugin SHARED xytron_function_plugin.cpp)
target_include_directories(nativeweb_test_xytron_function_plugin PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(nativeweb_test_xytron_function_plugin PRIVATE
    XYTRON_PLUGIN_ID=\"${_fixture_id}\"
    XYTRON_PLUGIN_VERSION=\"${_fixture_version}\")
set_target_properties(nativeweb_test_xytron_function_plugin PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_executable(nativeweb_xytron_function_regression xytron_function_regression.cpp)
target_link_libraries(nativeweb_xytron_function_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_xytron_function_regression PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(nativeweb_xytron_function_regression PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME plugin.xytron_function_binding COMMAND nativeweb_xytron_function_regression
    $<TARGET_FILE:nativeweb_test_xytron_function_plugin>)


# Invoked by xytron-cli.yml AFTER CLI creates the real DLL/SO.
add_executable(nativeweb_generated_xytron_plugin_regression
    generated_xytron_plugin_regression.cpp)
target_link_libraries(nativeweb_generated_xytron_plugin_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_generated_xytron_plugin_regression
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(nativeweb_generated_xytron_plugin_regression PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

# S2: ABI-v1 size-gated typed registration and legacy compatibility.
add_executable(nativeweb_typed_signature_regression typed_signature_regression.cpp)
target_link_libraries(nativeweb_typed_signature_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_typed_signature_regression PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(nativeweb_typed_signature_regression PRIVATE cxx_std_11)
add_test(NAME plugin.typed_signature COMMAND nativeweb_typed_signature_regression
    $<TARGET_FILE:nativeweb_test_cpp_plugin>
    $<TARGET_FILE:nativeweb_test_plugin>)
add_test(NAME plugin.typed_bindings COMMAND ${CMAKE_COMMAND}
    -DXYTRON=$<TARGET_FILE:xytron>
    -DCPP_PLUGIN=$<TARGET_FILE:nativeweb_test_cpp_plugin>
    -DLEGACY_PLUGIN=$<TARGET_FILE:nativeweb_test_plugin>
    -DROOT=${PROJECT_BINARY_DIR}/s2-bindings-check
    -P ${PROJECT_SOURCE_DIR}/tools/cli/test_bindings.cmake)


# ABI v1 optional native->JS event publication; runs in Core CTest on
# Windows/Linux/macOS ARM64/Intel without a browser SDK.
add_library(nativeweb_test_event_plugin SHARED native_event_plugin.cpp)
target_include_directories(nativeweb_test_event_plugin PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(nativeweb_test_event_plugin PRIVATE
    XYTRON_PLUGIN_ID=\"eventfixture\"
    XYTRON_PLUGIN_VERSION=\"1.0.0\")
set_target_properties(nativeweb_test_event_plugin PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_executable(nativeweb_plugin_native_event_regression plugin_native_event_regression.cpp)
target_link_libraries(nativeweb_plugin_native_event_regression PRIVATE NativeWeb::Core)
target_include_directories(nativeweb_plugin_native_event_regression PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(nativeweb_plugin_native_event_regression PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME plugin.native_event_lifecycle COMMAND
    nativeweb_plugin_native_event_regression
    $<TARGET_FILE:nativeweb_test_event_plugin>)
