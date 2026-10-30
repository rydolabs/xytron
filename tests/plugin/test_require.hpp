#pragma once

// Unlike standard assert(), NW_REQUIRE always evaluates its argument in
// Release builds. Regression test setup/teardown must not vanish with NDEBUG.
#include <cstdlib>
#include <iostream>

#define NW_REQUIRE(expression) do { \
    if (!(expression)) { \
        std::cerr << "CHECK failed: " #expression << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (false)
