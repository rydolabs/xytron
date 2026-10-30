#include "nativeweb/detail/bind.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void fail(const char* expression, const char* file, int line)
{
    std::cerr << file << ":" << line << ": CHECK failed: "
              << expression << std::endl;
    ++failures;
}

#define CHECK(expr) do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

void testTypedReturn()
{
    nativeweb::DynamicFunction add =
        nativeweb::detail::makeDynamicFunction(
            [](int a, int b) {
                return a + b;
            });

    VariantList args;
    args.push_back(Any(3));
    args.push_back(Any(4));

    Any result = add(args);
    CHECK(AnyCast<int>(result) == 7);
}

void testStringAndBoolArguments()
{
    nativeweb::DynamicFunction describe =
        nativeweb::detail::makeDynamicFunction(
            [](const std::string& name, bool enabled) {
                return enabled ? name : std::string("disabled");
            });

    VariantList args;
    args.push_back(Any("camera"));
    args.push_back(Any(true));

    CHECK(AnyCast<std::string>(describe(args)) == "camera");
}

void testVoidReturn()
{
    int observed = 0;

    nativeweb::DynamicFunction sink =
        nativeweb::detail::makeDynamicFunction(
            [&observed](int value) {
                observed = value;
            });

    VariantList args;
    args.push_back(Any(42));

    Any result = sink(args);
    CHECK(observed == 42);
    CHECK(result.empty());
}

void testWrongArgumentCount()
{
    nativeweb::DynamicFunction add =
        nativeweb::detail::makeDynamicFunction(
            [](int a, int b) {
                return a + b;
            });

    VariantList args;
    args.push_back(Any(1));

    bool threw = false;
    try
    {
        (void)add(args);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    CHECK(threw);
}

void testWrongArgumentType()
{
    nativeweb::DynamicFunction addOne =
        nativeweb::detail::makeDynamicFunction(
            [](int value) {
                return value + 1;
            });

    VariantList args;
    args.push_back(Any("not-an-int"));

    bool threw = false;
    try
    {
        (void)addOne(args);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    CHECK(threw);
}

} // namespace

int main()
{
    testTypedReturn();
    testStringAndBoolArguments();
    testVoidReturn();
    testWrongArgumentCount();
    testWrongArgumentType();

    if (failures != 0)
    {
        std::cerr << "Bind adapter regression failed: "
                  << failures << " check(s)" << std::endl;
        return 1;
    }

    std::cout << "Bind adapter regression: PASS" << std::endl;
    return 0;
}
