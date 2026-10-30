#include "core/binding_registry.hpp"
#include "nativeweb/error.hpp"
#include "plugin/plugin_module.hpp"

#include <iostream>
#include <string>

using nativeweb::detail::CallableSignature;
using nativeweb::detail::CallableTypeKind;

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    nativeweb::detail::BindingRegistry registry;
    nativeweb::detail::PluginModule modern(argv[1], registry);
    nativeweb::detail::PluginModule old(argv[2], registry);
    CallableSignature sig;
    if (!registry.signature("cppsample.add", &sig) ||
        sig.arity() != 2 ||
        sig.result.kind != CallableTypeKind::Integer ||
        sig.arguments[0].kind != CallableTypeKind::Integer ||
        sig.arguments[1].kind != CallableTypeKind::Integer) return 1;
    VariantList right;
    right.push_back(Any(20)); right.push_back(Any(22));
    if (AnyCast<int>(registry.find("cppsample.add").invoke(right)) != 42)
        return 1;
    try {
        registry.find("cppsample.add").invoke(VariantList());
        return 1;
    } catch (const nativeweb::Error& e) {
        if (e.code() != "invalid_arguments") return 1;
    }
    VariantList bad;
    bad.push_back(Any(std::string("20"))); bad.push_back(Any(22));
    try {
        registry.find("cppsample.add").invoke(bad);
        return 1;
    } catch (const nativeweb::Error& e) {
        if (e.code() != "invalid_arguments") return 1;
    }
    if (!registry.signature("cppsample.binary_size", &sig) ||
        sig.arity() != 1 || sig.arguments[0].kind != CallableTypeKind::Binary)
        return 1;
    // Old ABI v1 plugins without metadata remain callable, with Unknown type.
    if (!registry.signature("sample.add", &sig) ||
        sig.result.kind != CallableTypeKind::Unknown) return 1;
    if (AnyCast<int>(registry.find("sample.add").invoke(right)) != 42)
        return 1;
    std::cout << "Typed ABI + legacy compatibility PASS\n";
    return 0;
}
