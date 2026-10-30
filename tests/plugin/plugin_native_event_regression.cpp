#include "core/binding_registry.hpp"
#include "nativeweb/error.hpp"
#include "plugin/plugin_module.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    int errors=0;
#define CHECK(expr) do { if (!(expr)) { ++errors; std::cerr << __LINE__ << ": " << #expr << '\n'; } } while(0)
    nativeweb::detail::BindingRegistry registry;
    std::vector<std::string> events;
    std::vector<std::string> payloads;
    {
        nativeweb::detail::PluginModule module(
            argv[1],registry,std::function<void(const std::string&)>(),
            [&](const std::string& name,const Any& payload) {
                events.push_back(name);
                payloads.push_back(AnyCast<std::string>(payload));
            });
        CHECK(module.id()=="eventfixture");
        CHECK(registry.has("eventfixture.fire"));
        CHECK(AnyCast<std::string>(
            registry.find("eventfixture.fire").invoke(VariantList()))=="published");
        CHECK(events.size()==1);
        CHECK(events[0]=="eventfixture.changed");
        CHECK(payloads.size()==1 && payloads[0]=="hello-native");
        CHECK(AnyCast<std::string>(
            registry.find("eventfixture.oversized").invoke(VariantList()))=="rejected");
        CHECK(AnyCast<std::string>(
            registry.find("eventfixture.invalid").invoke(VariantList()))=="rejected");
        CHECK(events.size()==1);
        module.unload();
        CHECK(module.unloaded());
        CHECK(!registry.has("eventfixture.fire"));
    }
    // Legacy/no-event host: plugin must receive an error, not invoke a
    // missing function pointer or silently succeed.
    {
        nativeweb::detail::PluginModule noSink(argv[1],registry);
        bool denied=false;
        try {
            registry.find("eventfixture.fire").invoke(VariantList());
        } catch(const nativeweb::Error& e) {
            denied=true;
            CHECK(e.code()=="plugin_event_rejected");
        }
        CHECK(denied);
        CHECK(events.size()==1);
    }
    // A queued/copied callable may outlive PluginModule itself; it must
    // retain its DLL lease but MUST NOT dereference a deleted event host.
    nativeweb::detail::Callable retained;
    {
        nativeweb::detail::PluginModule temporary(
            argv[1],registry,std::function<void(const std::string&)>(),
            [&](const std::string& name,const Any& payload) {
                events.push_back(name);
                payloads.push_back(AnyCast<std::string>(payload));
            });
        retained=registry.find("eventfixture.fire");
    }
    CHECK(!registry.has("eventfixture.fire"));
    bool destroyedDenied=false;
    try {
        retained.invoke(VariantList());
    } catch(const nativeweb::Error& e) {
        destroyedDenied=true;
        CHECK(e.code()=="plugin_event_rejected");
    }
    CHECK(destroyedDenied);
    CHECK(events.size()==1);
    retained=nativeweb::detail::Callable();
    if (errors==0) std::cout<<"Plugin ABI native-event lifecycle and denials PASS\n";
    return errors?1:0;
}
