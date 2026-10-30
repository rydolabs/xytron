#ifndef NATIVEWEB_MODULE_REGISTRY_HPP_INCLUDED
#define NATIVEWEB_MODULE_REGISTRY_HPP_INCLUDED

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nativeweb {
namespace detail {

enum class ModuleLanguage { Cpp, Python, CSharp };
enum class ModuleRole { Main, Feature };
enum class ModuleDelivery { Embedded, Plugin };

struct ModuleDescriptor {
    ModuleDescriptor()
        : language(ModuleLanguage::Cpp),
          role(ModuleRole::Feature),
          delivery(ModuleDelivery::Embedded) {}

    ModuleDescriptor(const std::string& name,
                     ModuleLanguage lang,
                     ModuleRole kind,
                     ModuleDelivery form)
        : id(name), language(lang), role(kind), delivery(form) {}

    std::string id;
    ModuleLanguage language;
    ModuleRole role;
    ModuleDelivery delivery;
};

class ModuleRegistration;

class ModuleRegistry {
public:
    ModuleRegistry();

    // Thread-safe, app-scoped metadata registry. Does not load a language
    // runtime or grant native execution permissions.
    ModuleRegistration registerModule(const ModuleDescriptor& descriptor);
    bool unregisterModule(const std::string& id);
    bool has(const std::string& id) const;
    std::uint64_t generation(const std::string& id) const;
    bool find(const std::string& id, ModuleDescriptor* output) const;
    bool hasMain() const;
    std::string mainId() const;
    std::vector<ModuleDescriptor> snapshot() const;
    std::size_t size() const;
    void clear();

private:
    struct Entry {
        Entry() : generation(0) {}
        Entry(const ModuleDescriptor& data, std::uint64_t epoch)
            : descriptor(data), generation(epoch) {}
        ModuleDescriptor descriptor;
        std::uint64_t generation;
    };
    struct State {
        State() : nextGeneration(1) {}
        mutable std::mutex mutex;
        std::map<std::string, Entry> modules;
        std::string mainId;
        std::uint64_t nextGeneration;
    };

    static bool release(const std::shared_ptr<State>& state,
                        const std::string& id,
                        std::uint64_t generation);
    std::shared_ptr<State> state_;
    friend class ModuleRegistration;
};

class ModuleRegistration {
public:
    ModuleRegistration();
    ModuleRegistration(const std::shared_ptr<ModuleRegistry::State>& state,
                       const std::string& id,
                       std::uint64_t generation);
    ~ModuleRegistration();

    ModuleRegistration(ModuleRegistration&& other);
    ModuleRegistration& operator=(ModuleRegistration&& other);
    ModuleRegistration(const ModuleRegistration&) = delete;
    ModuleRegistration& operator=(const ModuleRegistration&) = delete;

    bool valid() const;
    bool release();
private:
    std::weak_ptr<ModuleRegistry::State> state_;
    std::string id_;
    std::uint64_t generation_;
};

} // namespace detail
} // namespace nativeweb
#endif
