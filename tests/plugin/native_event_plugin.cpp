#include "xytron/plugin.hpp"

#include <string>
#include <stdexcept>

XYTRON_MODULE(m) {
    const nativeweb::PluginEventPublisher events=m.events();
    m.function("fire", [events]() {
        events.emit("changed", "hello-native");
        return std::string("published");
    });
    m.function("oversized", [events]() {
        try {
            events.emit("changed", std::string(4097, 'x'));
        } catch(const std::exception&) {
            return std::string("rejected");
        }
        return std::string("incorrectly_accepted");
    });
    m.function("invalid", [events]() {
        try {
            events.emit("bad.event", "hello-native");
        } catch(const std::exception&) {
            return std::string("rejected");
        }
        return std::string("incorrectly_accepted");
    });
}
