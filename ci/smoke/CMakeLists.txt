cmake_minimum_required(VERSION 3.16)
project(platform_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(platform_smoke main.cpp)
target_link_libraries(platform_smoke PRIVATE Threads::Threads)

enable_testing()
add_test(NAME platform_smoke COMMAND platform_smoke)
