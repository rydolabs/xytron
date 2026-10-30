cmake_minimum_required(VERSION 3.16)

project(NativeWeb VERSION 0.1.0 LANGUAGES CXX)

option(NATIVEWEB_BUILD_TESTS "Build NativeWeb regression tests" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

set(NATIVEWEB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${NATIVEWEB_ROOT}/cmake/NativeWebCoreSources.cmake")
add_library(nativeweb_core STATIC ${NATIVEWEB_CORE_SOURCES})
add_library(NativeWeb::Core ALIAS nativeweb_core)
# Branding-compatible build-tree target; NativeWeb::Core remains supported.
add_library(Xytron::Core ALIAS nativeweb_core)
set_target_properties(nativeweb_core PROPERTIES EXPORT_NAME Core)

target_include_directories(nativeweb_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nativeweb_core
    PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

target_compile_features(nativeweb_core PUBLIC cxx_std_11)

set_target_properties(nativeweb_core PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

if(NATIVEWEB_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests/core)
    add_subdirectory(tests/api)
    add_subdirectory(tests/plugin)
endif()

# The Core library and public headers form a standalone, engine-neutral SDK.
# CEF/WebView2 backends are optional integration targets and are not shipped
# implicitly with the Core export.
install(TARGETS nativeweb_core EXPORT NativeWebTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/nativeweb DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/xytron DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/NativeWebConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/NativeWebConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NativeWeb")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NativeWebConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NativeWebConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NativeWebConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NativeWeb")
install(EXPORT NativeWebTargets
    FILE NativeWebTargets.cmake
    NAMESPACE NativeWeb::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NativeWeb")

# Public Xytron CMake facade. Keep the original NativeWeb package and
# nativeweb:: Core ABI intact so existing consumers/plugins keep working.
configure_package_config_file(
    cmake/XytronConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/XytronConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Xytron")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/XytronConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/XytronConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/XytronConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Xytron")


# A C++11-native bootstrap CLI; no Python/Node required for CLI itself.
add_executable(xytron tools/cli/xytron.cpp)
set_target_properties(xytron PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_include_directories(xytron PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(xytron PRIVATE nativeweb_core)
target_compile_definitions(xytron PRIVATE
  XYTRON_CLI_DEV_TOOLS=\"${CMAKE_CURRENT_SOURCE_DIR}/tools/cli\")
install(FILES tools/cli/generate_managed_bindings.py
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xytron/cli)
install(FILES tools/cli/dotnet_reflector/Program.cs
              tools/cli/dotnet_reflector/Xytron.Metadata.csproj
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xytron/cli/dotnet_reflector)

# Managed Main backends use the existing reviewed private JSONL hosts.
# Install sources under the Core SDK prefix so no private checkout or
# arbitrary PYTHONPATH is required at application run time.
install(DIRECTORY bindings/python/xytron/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xytron/python/xytron
  FILES_MATCHING PATTERN "*.py")
install(FILES bindings/python/app_worker.py
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xytron/python)
install(DIRECTORY bindings/dotnet/Xytron.Binding/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xytron/dotnet/Xytron.Binding
  FILES_MATCHING PATTERN "*.cs" PATTERN "*.csproj")

install(TARGETS xytron RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NATIVEWEB_BUILD_TESTS)
  add_executable(xytron_cli_output_guard_regression tools/cli/test_existing_output_guard.cpp)
  target_include_directories(xytron_cli_output_guard_regression PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/cli")
  target_compile_features(xytron_cli_output_guard_regression PRIVATE cxx_std_11)
  set_target_properties(xytron_cli_output_guard_regression PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
  add_test(NAME cli.existing_output_guard
    COMMAND $<TARGET_FILE:xytron_cli_output_guard_regression>)
  set_tests_properties(cli.existing_output_guard PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" TIMEOUT 15)
  add_test(NAME cli.manifest_matrix COMMAND ${CMAKE_COMMAND}
    -DXYTRON=$<TARGET_FILE:xytron>
    -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/xytron-manifest-matrix
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/cli/test_manifest_matrix.cmake)
  set_tests_properties(cli.manifest_matrix PROPERTIES TIMEOUT 35)
  add_test(NAME cli.scaffold COMMAND ${CMAKE_COMMAND}
    -DXYTRON=$<TARGET_FILE:xytron>
    -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/xytron-cli-smoke
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/cli/test_xytron.cmake)
endif()
