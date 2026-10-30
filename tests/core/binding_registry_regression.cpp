#include "core/binding_registry.hpp"
#include "nativeweb/detail/bind.hpp"

#include <iostream>
#include <utility>

namespace {

int failures = 0;

void fail(const char* expression, const char* file, int line)
{
    std::cerr << file << ":" << line
              << ": CHECK failed: " << expression
              << std::endl;
    ++failures;
}

#define CHECK(expr) do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

void testPermanentBinding()
{
    nativeweb::detail::BindingRegistry registry;

    registry.bind(
        "math.add",
        nativeweb::detail::makeCallable(
            [](int a, int b) { return a + b; }));

    CHECK(registry.has("math.add"));
    CHECK(registry.size() == 1u);

    VariantList args;
    args.push_back(Any(20));
    args.push_back(Any(22));

    CHECK(AnyCast<int>(registry.find("math.add").invoke(args)) == 42);
}

void testScopedRegistrationReleases()
{
    nativeweb::detail::BindingRegistry registry;

    {
        nativeweb::detail::RegistrationToken token =
            registry.registerBinding(
                "plugin.value",
                nativeweb::detail::makeCallable(
                    []() { return 42; }));

        CHECK(token.valid());
        CHECK(registry.has("plugin.value"));
    }

    CHECK(!registry.has("plugin.value"));
}

void testOldTokenCannotEraseReplacement()
{
    nativeweb::detail::BindingRegistry registry;

    nativeweb::detail::RegistrationToken first =
        registry.registerBinding(
            "plugin.value",
            nativeweb::detail::makeCallable(
                []() { return 1; }));

    nativeweb::detail::RegistrationToken second =
        registry.registerBinding(
            "plugin.value",
            nativeweb::detail::makeCallable(
                []() { return 2; }));

    CHECK(!first.release());
    CHECK(registry.has("plugin.value"));
    CHECK(AnyCast<int>(registry.find("plugin.value").invoke(VariantList())) == 2);
    CHECK(second.release());
    CHECK(!registry.has("plugin.value"));
}

void testMoveTransfersOwnership()
{
    nativeweb::detail::BindingRegistry registry;

    nativeweb::detail::RegistrationToken first =
        registry.registerBinding(
            "plugin.move",
            nativeweb::detail::makeCallable(
                []() { return true; }));

    nativeweb::detail::RegistrationToken second(std::move(first));

    CHECK(!first.valid());
    CHECK(second.valid());
    CHECK(registry.has("plugin.move"));
    CHECK(second.release());
    CHECK(!registry.has("plugin.move"));
}

void testSignatureLookup()
{
    nativeweb::detail::BindingRegistry registry;

    registry.bind(
        "camera.open",
        nativeweb::detail::makeCallable(
            [](const std::string&, int) { return true; }));

    nativeweb::detail::CallableSignature signature;

    CHECK(registry.signature("camera.open", &signature));
    CHECK(signature.arity() == 2u);
    CHECK(signature.arguments[0].kind == nativeweb::detail::CallableTypeKind::String);
    CHECK(signature.arguments[1].kind == nativeweb::detail::CallableTypeKind::Integer);
    CHECK(signature.result.kind == nativeweb::detail::CallableTypeKind::Boolean);
}

} // namespace

int main()
{
    testPermanentBinding();
    testScopedRegistrationReleases();
    testOldTokenCannotEraseReplacement();
    testMoveTransfersOwnership();
    testSignatureLookup();

    if (failures)
    {
        std::cerr << "Binding registry regression failed: "
                  << failures << std::endl;
        return 1;
    }

    std::cout << "Binding registry regression: PASS" << std::endl;
    return 0;
}
