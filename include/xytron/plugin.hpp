#ifndef XYTRON_PLUGIN_HPP_INCLUDED
#define XYTRON_PLUGIN_HPP_INCLUDED

// Public C++11 free-function authoring shim for the existing stable
// NativeWeb plugin ABI v1. Full class/property registration is a later gate.
#include "nativeweb/plugin.hpp"

#ifndef XYTRON_PLUGIN_ID
#error "XYTRON_PLUGIN_ID must be defined from plugin.json at build time"
#endif
#ifndef XYTRON_PLUGIN_VERSION
#define XYTRON_PLUGIN_VERSION ""
#endif

namespace xytron {

class Module {
public:
    explicit Module(::nativeweb::PluginApi& api) : api_(api) {}

    ::nativeweb::PluginEventPublisher events() const {
        return api_.events();
    }

    template <typename Function>
    Module& function(const std::string& name, Function fn) {
        api_.bind(name, fn);
        return *this;
    }

private:
    ::nativeweb::PluginApi& api_;
};

} // namespace xytron

// Exactly one XYTRON_MODULE per native plugin binary; no plugin ID in
// user source. ABI exported symbol is still nativeweb_plugin_init_v1.
#define XYTRON_MODULE(m)                                                  \
    static void xytron_register_exports(::xytron::Module& m);            \
    namespace xytron { namespace detail {                                \
    class XytronPluginEntry : public ::nativeweb::Plugin {               \
    public:                                                                \
        std::string version() const override {                            \
            return XYTRON_PLUGIN_VERSION;                                 \
        }                                                                  \
        void registerApi(::nativeweb::PluginApi& api) override {          \
            ::xytron::Module m(api);                                      \
            ::xytron_register_exports(m);                                \
        }                                                                  \
    };                                                                     \
    }}                                                                     \
    NATIVEWEB_PLUGIN(::xytron::detail::XytronPluginEntry,                 \
                     XYTRON_PLUGIN_ID)                                    \
    static void xytron_register_exports(::xytron::Module& m)

#endif
