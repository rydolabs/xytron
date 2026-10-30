#include "nativeweb/plugin.hpp"

#include <stdexcept>
#include <string>

class SampleCppPlugin :
    public nativeweb::Plugin
{
public:
    std::string version() const override
    {
        return "2.0.0";
    }

    void registerApi(
        nativeweb::PluginApi& api) override
    {
        api.bind(
            "add",
            [](int a, int b) {
                return a + b;
            });

        api.bind(
            "echo",
            [](const std::string& value) {
                return std::string("cpp:") +
                    value;
            });

        api.bind(
            "binary_size",
            [](const nativeweb::Binary& bytes) {
                return static_cast<int>(
                    bytes.size());
            });

        api.bind(
            "fail",
            []() -> int {
                throw nativeweb::Error(
                    "cpp_sample_error",
                    "expected C++ plugin failure");
            });
    }
};

NATIVEWEB_PLUGIN(
    SampleCppPlugin,
    "cppsample")
