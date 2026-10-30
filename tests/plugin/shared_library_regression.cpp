#include "plugin/shared_library.hpp"
#include "nativeweb/error.hpp"

#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;

void fail(const char* expression, const char* file, int line)
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

typedef int (*AddFunction)(int, int);
typedef const char* (*NameFunction)();

void testLoadResolveUnload(
    const std::string& path)
{
    nativeweb::detail::SharedLibrary library;

    CHECK(!library.loaded());

    library.load(path);

    CHECK(library.loaded());
    CHECK(library.path() == path);

    AddFunction add =
        reinterpret_cast<AddFunction>(
            library.symbol(
                "nativeweb_test_add"));

    NameFunction name =
        reinterpret_cast<NameFunction>(
            library.symbol(
                "nativeweb_test_name"));

    CHECK(add(20, 22) == 42);
    CHECK(
        std::string(name()) ==
        "nativeweb-test-library");

    library.unload();

    CHECK(!library.loaded());
    CHECK(library.path().empty());
}

void testMissingSymbol(
    const std::string& path)
{
    nativeweb::detail::SharedLibrary library(path);

    bool threw = false;

    try
    {
        (void)library.symbol(
            "nativeweb_symbol_that_does_not_exist");
    }
    catch (const nativeweb::Error& error)
    {
        threw = true;
        CHECK(
            error.code() ==
            "shared_library_symbol_not_found");
    }

    CHECK(threw);
}

void testMoveTransfersHandle(
    const std::string& path)
{
    nativeweb::detail::SharedLibrary first(path);

    nativeweb::detail::SharedLibrary second(
        std::move(first));

    CHECK(!first.loaded());
    CHECK(second.loaded());

    AddFunction add =
        reinterpret_cast<AddFunction>(
            second.symbol(
                "nativeweb_test_add"));

    CHECK(add(1, 2) == 3);
}

void testMissingLibrary()
{
    nativeweb::detail::SharedLibrary library;

    bool threw = false;

    try
    {
#if defined(_WIN32)
        library.load(
            "Z:\\nativeweb\\definitely-missing.dll");
#else
        library.load(
            "/nativeweb/definitely-missing/libnativeweb.so");
#endif
    }
    catch (const nativeweb::Error& error)
    {
        threw = true;
        CHECK(
            error.code() ==
            "shared_library_load_failed");
    }

    CHECK(threw);
    CHECK(!library.loaded());
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr
            << "Expected shared library path"
            << std::endl;
        return 2;
    }

    const std::string path = argv[1];

    testLoadResolveUnload(path);
    testMissingSymbol(path);
    testMoveTransfersHandle(path);
    testMissingLibrary();

    if (failures)
    {
        std::cerr
            << "SharedLibrary regression failed: "
            << failures
            << std::endl;
        return 1;
    }

    std::cout
        << "SharedLibrary regression: PASS"
        << std::endl;

    return 0;
}
