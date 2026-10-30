cmake_minimum_required(VERSION 3.16)
project(NativeWebPublicCIProbe LANGUAGES CXX)
add_executable(public_cli_process_probe process_probe.cpp)
set_target_properties(public_cli_process_probe PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
set(STAGED_DIR "${CMAKE_BINARY_DIR}/.xytron-build/out/Release")
add_custom_command(TARGET public_cli_process_probe POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${STAGED_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:public_cli_process_probe>"
    "${STAGED_DIR}/xytron_user_backend${CMAKE_EXECUTABLE_SUFFIX}"
  VERBATIM)
enable_testing()
add_test(NAME process_probe COMMAND "$<TARGET_FILE:public_cli_process_probe>")
set_tests_properties(process_probe PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 20)
