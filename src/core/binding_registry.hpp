#ifndef NATIVEWEB_BINDING_REGISTRY_HPP_INCLUDED
#define NATIVEWEB_BINDING_REGISTRY_HPP_INCLUDED

#include "nativeweb/detail/callable.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace nativeweb {
namespace detail {

class RegistrationToken;

class BindingRegistry
{
public:
    BindingRegistry();

    void bind(
        const std::string& method,
        const Callable& callable);

    RegistrationToken registerBinding(
        const std::string& method,
        const Callable& callable);

    bool unbind(const std::string& method);
    bool has(const std::string& method) const;
    Callable find(const std::string& method) const;

    bool signature(
        const std::string& method,
        CallableSignature* value) const;

    std::size_t size() const;
    void clear();

private:
    struct Entry
    {
        Entry()
            : generation(0)
        {
        }

        Entry(
            const Callable& valueCallable,
            std::uint64_t valueGeneration)
            : callable(valueCallable),
              generation(valueGeneration)
        {
        }

        Callable callable;
        std::uint64_t generation;
    };

    struct State
    {
        State()
            : nextGeneration(1)
        {
        }

        mutable std::mutex mutex;
        std::map<std::string, Entry> methods;
        std::uint64_t nextGeneration;
    };

    static bool release(
        const std::shared_ptr<State>& state,
        const std::string& method,
        std::uint64_t generation);

    std::shared_ptr<State> state_;

    friend class RegistrationToken;
};

class RegistrationToken
{
public:
    RegistrationToken();

    RegistrationToken(
        const std::shared_ptr<BindingRegistry::State>& state,
        const std::string& method,
        std::uint64_t generation);

    ~RegistrationToken();

    RegistrationToken(RegistrationToken&& other);
    RegistrationToken& operator=(RegistrationToken&& other);

    RegistrationToken(const RegistrationToken&) = delete;
    RegistrationToken& operator=(const RegistrationToken&) = delete;

    bool valid() const;
    bool release();

private:
    std::weak_ptr<BindingRegistry::State> state_;
    std::string method_;
    std::uint64_t generation_;
};

} // namespace detail
} // namespace nativeweb

#endif
