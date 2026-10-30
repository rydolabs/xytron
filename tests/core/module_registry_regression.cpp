#include "core/module_registry.hpp"
#include "nativeweb/error.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* message, int line)
{
    if (!ok) {
        std::cerr << "module_registry_regression.cpp:" << line
                  << ": " << message << std::endl;
        ++failures;
    }
}
#define CHECK(value) check(!!(value), #value, __LINE__)
using nativeweb::detail::ModuleRegistry;
using nativeweb::detail::ModuleRegistration;
using nativeweb::detail::ModuleDescriptor;
using nativeweb::detail::ModuleLanguage;
using nativeweb::detail::ModuleDelivery;
using nativeweb::detail::ModuleRole;
ModuleDescriptor module(const char* id, ModuleLanguage language,
                        ModuleRole role = ModuleRole::Feature,
                        ModuleDelivery delivery = ModuleDelivery::Embedded)
{
    return ModuleDescriptor(id, language, role, delivery);
}
void expectCode(const ModuleDescriptor& d, ModuleRegistry& registry,
                const char* expected)
{
    bool thrown = false;
    try {
        ModuleRegistration token = registry.registerModule(d);
        (void)token;
    } catch (const nativeweb::Error& error) {
        thrown = true;
        CHECK(error.code() == expected);
    }
    CHECK(thrown);
}

void registerAllLanguageModules()
{
    ModuleRegistry registry;
    ModuleRegistration main = registry.registerModule(
        module("app", ModuleLanguage::Python, ModuleRole::Main));
    ModuleRegistration camera = registry.registerModule(
        module("camera", ModuleLanguage::Cpp, ModuleRole::Feature,
               ModuleDelivery::Plugin));
    ModuleRegistration erp = registry.registerModule(
        module("erp", ModuleLanguage::CSharp));
    CHECK(registry.mainId() == "app");
    CHECK(registry.size() == 3u);
    ModuleDescriptor read;
    CHECK(registry.find("camera", &read));
    CHECK(read.language == ModuleLanguage::Cpp);
    CHECK(read.delivery == ModuleDelivery::Plugin);
    CHECK(read.role == ModuleRole::Feature);
    CHECK(registry.find("erp", &read));
    CHECK(read.language == ModuleLanguage::CSharp);
    CHECK(registry.find("app", &read));
    CHECK(read.language == ModuleLanguage::Python);
    CHECK(read.role == ModuleRole::Main);
    CHECK(registry.snapshot().size() == 3u);
    CHECK(main.valid() && camera.valid() && erp.valid());
    expectCode(module("other", ModuleLanguage::Cpp, ModuleRole::Main),
               registry, "duplicate_main");
    expectCode(module("camera", ModuleLanguage::CSharp),
               registry, "duplicate_module");
    CHECK(registry.size() == 3u);
}

void validateIdentifiers()
{
    ModuleRegistry registry;
    const char* bad[] = {
        "", "Camel", "with.dot", "has-dash", "_private",
        "xytron", "native", "invoke", "constructor",
        "then", "with/slash", "with space"
    };
    for (std::size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        expectCode(module(bad[i], ModuleLanguage::Cpp),
                   registry, "invalid_module_id");
    CHECK(registry.size() == 0u);
}

void lifecycleAndGeneration()
{
    ModuleRegistry registry;
    ModuleRegistration prior = registry.registerModule(
        module("app", ModuleLanguage::Cpp, ModuleRole::Main));
    CHECK(registry.hasMain());
    CHECK(registry.unregisterModule("app"));
    CHECK(!registry.hasMain());

    ModuleRegistration current = registry.registerModule(
        module("app", ModuleLanguage::CSharp, ModuleRole::Main));
    CHECK(!prior.release());  // stale token must not remove new Main
    CHECK(registry.hasMain());
    ModuleRegistration moved(std::move(current));
    CHECK(!current.valid());
    CHECK(moved.valid());
    CHECK(moved.release());
    CHECK(!registry.hasMain());
    CHECK(!moved.release());
    CHECK(!registry.unregisterModule("app"));

    ModuleRegistration feature = registry.registerModule(
        module("feature", ModuleLanguage::Python));
    registry.clear();
    CHECK(!registry.has("feature"));
    CHECK(!feature.release());

    ModuleRegistration orphan;
    {
        std::unique_ptr<ModuleRegistry> ephemeral(new ModuleRegistry());
        orphan = ephemeral->registerModule(
            module("temporary", ModuleLanguage::Cpp));
    }
    CHECK(!orphan.release()); // registry owner destroyed first
}

void perApplicationAndConcurrentMain()
{
    ModuleRegistry left;
    ModuleRegistry right;
    ModuleRegistration mainLeft = left.registerModule(
        module("app", ModuleLanguage::Cpp, ModuleRole::Main));
    ModuleRegistration mainRight = right.registerModule(
        module("app", ModuleLanguage::Python, ModuleRole::Main));
    CHECK(left.size() == 1u && right.size() == 1u);
    CHECK(mainLeft.release());
    CHECK(!left.hasMain() && right.hasMain());

    ModuleRegistry parallel;
    std::atomic<int> success(0);
    std::atomic<int> duplicate(0);
    ModuleRegistration holders[2];
    std::thread threads[2];
    for (int i = 0; i < 2; ++i) {
        threads[i] = std::thread([&parallel, &success, &duplicate,
                                  &holders, i]() {
            try {
                holders[i] = parallel.registerModule(
                    module(i == 0 ? "first" : "second",
                           ModuleLanguage::Cpp, ModuleRole::Main));
                ++success;
            } catch (const nativeweb::Error& e) {
                if (e.code() == "duplicate_main") ++duplicate;
            }
        });
    }
    for (int i = 0; i < 2; ++i) threads[i].join();
    CHECK(success.load() == 1);
    CHECK(duplicate.load() == 1);
    CHECK(parallel.size() == 1u);
}
} // namespace

int main()
{
    registerAllLanguageModules();
    validateIdentifiers();
    lifecycleAndGeneration();
    perApplicationAndConcurrentMain();
    if (failures) return 1;
    std::cout << "Module registry: PASS" << std::endl;
    return 0;
}
