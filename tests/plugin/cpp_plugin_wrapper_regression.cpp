#include "core/binding_registry.hpp"
#include "nativeweb/error.hpp"
#include "plugin/plugin_module.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void fail(
    const char* expression,
    const char* file,
    int line)
{
    std::cerr
        << file << ":" << line
        << ": CHECK failed: "
        << expression
        << std::endl;
    ++failures;
}

#define CHECK(expr) \
    do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

void testCppWrapper(
    const std::string& pluginPath)
{
    nativeweb::detail::BindingRegistry registry;

    {
        nativeweb::detail::PluginModule module(
            pluginPath,
            registry);

        CHECK(module.id() == "cppsample");
        CHECK(module.version() == "2.0.0");
        CHECK(module.registrationCount() == 4u);

        VariantList addArgs;
        addArgs.push_back(Any(20));
        addArgs.push_back(Any(22));

        CHECK(
            AnyCast<int>(
                registry.find(
                    "cppsample.add").invoke(
                    addArgs)) ==
            42);

        VariantList echoArgs;
        echoArgs.push_back(
            Any(std::string("hello")));

        CHECK(
            AnyCast<std::string>(
                registry.find(
                    "cppsample.echo").invoke(
                    echoArgs)) ==
            "cpp:hello");

        nativeweb::Binary bytes;
        bytes.push_back(1);
        bytes.push_back(2);
        bytes.push_back(3);

        VariantList binaryArgs;
        binaryArgs.push_back(
            Any(bytes));

        CHECK(
            AnyCast<int>(
                registry.find(
                    "cppsample.binary_size").invoke(
                    binaryArgs)) ==
            3);

        bool rejected = false;

        try
        {
            (void)registry.find(
                "cppsample.fail").invoke(
                    VariantList());
        }
        catch (const nativeweb::Error& error)
        {
            rejected = true;

            CHECK(
                error.code() ==
                "cpp_sample_error");

            CHECK(
                std::string(
                    error.what()) ==
                "expected C++ plugin failure");
        }

        CHECK(rejected);
    }

    CHECK(!registry.has("cppsample.add"));
    CHECK(!registry.has("cppsample.echo"));
    CHECK(!registry.has("cppsample.binary_size"));
    CHECK(!registry.has("cppsample.fail"));
}

} // namespace

int main(
    int argc,
    char* argv[])
{
    if (argc != 2)
    {
        std::cerr
            << "Expected C++ wrapper plugin path"
            << std::endl;
        return 2;
    }

    testCppWrapper(
        argv[1]);

    if (failures)
    {
        std::cerr
            << "C++ plugin wrapper regression failed: "
            << failures
            << std::endl;
        return 1;
    }

    std::cout
        << "C++ plugin wrapper regression: PASS"
        << std::endl;

    return 0;
}
