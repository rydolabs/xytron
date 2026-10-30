#include "xytron/plugin.hpp"
#include <string>

static int add(int a, int b) { return a + b; }
static std::string echo(const std::string& value) {
    return std::string("xytron:") + value;
}
static int fail() {
    throw nativeweb::Error("xytron_expected_failure", "expected failure");
}
XYTRON_MODULE(m) {
    m.function("add", &add)
     .function("echo", &echo)
     .function("fail", &fail);
}
