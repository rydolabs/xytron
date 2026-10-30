#include "nativeweb/detail/bind.hpp"

#include <iostream>
#include <memory>
#include <string>

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

struct Calculator
{
    explicit Calculator(int value) : base(value) {}
    int add(int value) { return base + value; }
    std::string name() const { return "calculator"; }
    int base;
};

void testFreeFunctionSignature()
{
    const nativeweb::detail::Callable callable =
        nativeweb::detail::makeCallable(
            [](int a, double b, const std::string& text) -> bool {
                return a == 3 && b > 1.0 && text == "ok";
            });

    const nativeweb::detail::CallableSignature& signature =
        callable.signature();

    CHECK(signature.arity() == 3u);
    CHECK(signature.result.kind == nativeweb::detail::CallableTypeKind::Boolean);
    CHECK(signature.arguments[0].kind == nativeweb::detail::CallableTypeKind::Integer);
    CHECK(signature.arguments[1].kind == nativeweb::detail::CallableTypeKind::Floating);
    CHECK(signature.arguments[2].kind == nativeweb::detail::CallableTypeKind::String);

    VariantList args;
    args.push_back(Any(3));
    args.push_back(Any(2.0));
    args.push_back(Any("ok"));

    CHECK(AnyCast<bool>(callable.invoke(args)));
}

void testVoidAndContainers()
{
    int observed = 0;

    const nativeweb::detail::Callable callable =
        nativeweb::detail::makeCallable(
            [&observed](const VariantList& list, const VariantDict&) {
                observed = static_cast<int>(list.size());
            });

    CHECK(callable.signature().result.kind == nativeweb::detail::CallableTypeKind::Void);
    CHECK(callable.signature().arguments[0].kind == nativeweb::detail::CallableTypeKind::List);
    CHECK(callable.signature().arguments[1].kind == nativeweb::detail::CallableTypeKind::Dictionary);

    VariantList list;
    list.push_back(Any(1));
    list.push_back(Any(2));

    VariantDict dict;
    dict["value"] = Any(true);

    VariantList args;
    args.push_back(Any(list));
    args.push_back(Any(dict));

    (void)callable.invoke(args);
    CHECK(observed == 2);
}

void testBoundMemberSignature()
{
    std::shared_ptr<Calculator> calculator(new Calculator(40));

    const nativeweb::detail::Callable add =
        nativeweb::detail::makeCallable(
            nativeweb::detail::bindMember(
                calculator,
                &Calculator::add));

    CHECK(add.signature().arity() == 1u);
    CHECK(add.signature().arguments[0].kind == nativeweb::detail::CallableTypeKind::Integer);
    CHECK(add.signature().result.kind == nativeweb::detail::CallableTypeKind::Integer);

    VariantList args;
    args.push_back(Any(2));
    CHECK(AnyCast<int>(add.invoke(args)) == 42);

    const nativeweb::detail::Callable name =
        nativeweb::detail::makeCallable(
            nativeweb::detail::bindMember(
                calculator,
                &Calculator::name));

    CHECK(name.signature().arity() == 0u);
    CHECK(name.signature().result.kind == nativeweb::detail::CallableTypeKind::String);
}

void testKnownBridgeTypes()
{
    const nativeweb::detail::Callable callable =
        nativeweb::detail::makeCallable(
            [](const nativeweb::Binary& bytes) -> nativeweb::NativeObjectHandle {
                return nativeweb::NativeObjectHandle(
                    static_cast<std::uint64_t>(bytes.size()),
                    "BinaryObject");
            });

    CHECK(callable.signature().arguments[0].kind == nativeweb::detail::CallableTypeKind::Binary);
    CHECK(callable.signature().result.kind == nativeweb::detail::CallableTypeKind::NativeObject);
}

} // namespace

int main()
{
    testFreeFunctionSignature();
    testVoidAndContainers();
    testBoundMemberSignature();
    testKnownBridgeTypes();

    if (failures)
    {
        std::cerr << "Callable regression failed: "
                  << failures << std::endl;
        return 1;
    }

    std::cout << "Callable regression: PASS" << std::endl;
    return 0;
}
