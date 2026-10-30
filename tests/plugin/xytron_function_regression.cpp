#include "core/binding_registry.hpp"
#include "nativeweb/error.hpp"
#include "plugin/plugin_module.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << __LINE__ << " " << #x << "\n"; } } while(0)
    nativeweb::detail::BindingRegistry registry;
    {
        nativeweb::detail::PluginModule plugin(argv[1], registry);
        CHECK(plugin.id() == "xytronfixture");
        CHECK(plugin.version() == "1.2.3");
        CHECK(plugin.registrationCount() == 3u);
        VariantList args;
        args.push_back(Any(20)); args.push_back(Any(22));
        CHECK(AnyCast<int>(registry.find("xytronfixture.add").invoke(args)) == 42);
        VariantList name; name.push_back(Any(std::string("hi")));
        CHECK(AnyCast<std::string>(
            registry.find("xytronfixture.echo").invoke(name)) == "xytron:hi");
        bool threw = false;
        try {
            (void)registry.find("xytronfixture.fail").invoke(VariantList());
        } catch (const nativeweb::Error& error) {
            threw = true;
            CHECK(error.code() == "xytron_expected_failure");
        }
        CHECK(threw);
    }
    CHECK(!registry.has("xytronfixture.add"));
    CHECK(!registry.has("xytronfixture.echo"));
    return failures ? 1 : 0;
}
