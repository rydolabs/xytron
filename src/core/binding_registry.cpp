#include "core/binding_registry.hpp"

#include "nativeweb/error.hpp"

namespace nativeweb {
namespace detail {

BindingRegistry::BindingRegistry()
    : state_(new State())
{
}

void BindingRegistry::bind(
    const std::string& method,
    const Callable& callable)
{
    if (method.empty())
        throw Error("invalid_method", "NativeWeb method name cannot be empty");

    if (!callable)
        throw Error("invalid_method", "NativeWeb method callback is empty");

    std::lock_guard<std::mutex> lock(state_->mutex);

    const std::uint64_t generation =
        state_->nextGeneration++;

    state_->methods[method] =
        Entry(callable, generation);
}

RegistrationToken BindingRegistry::registerBinding(
    const std::string& method,
    const Callable& callable)
{
    if (method.empty())
        throw Error("invalid_method", "NativeWeb method name cannot be empty");

    if (!callable)
        throw Error("invalid_method", "NativeWeb method callback is empty");

    std::uint64_t generation = 0;

    {
        std::lock_guard<std::mutex> lock(state_->mutex);

        generation = state_->nextGeneration++;

        state_->methods[method] =
            Entry(callable, generation);
    }

    return RegistrationToken(
        state_,
        method,
        generation);
}

bool BindingRegistry::unbind(
    const std::string& method)
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->methods.erase(method) != 0;
}

bool BindingRegistry::has(
    const std::string& method) const
{
    std::lock_guard<std::mutex> lock(state_->mutex);

    return state_->methods.find(method) !=
        state_->methods.end();
}

Callable BindingRegistry::find(
    const std::string& method) const
{
    std::lock_guard<std::mutex> lock(state_->mutex);

    std::map<std::string, Entry>::const_iterator found =
        state_->methods.find(method);

    if (found == state_->methods.end())
        return Callable();

    return found->second.callable;
}

bool BindingRegistry::signature(
    const std::string& method,
    CallableSignature* value) const
{
    if (!value)
        return false;

    const Callable callable =
        find(method);

    if (!callable)
        return false;

    *value = callable.signature();
    return true;
}

std::size_t BindingRegistry::size() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->methods.size();
}

void BindingRegistry::clear()
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->methods.clear();
}

bool BindingRegistry::release(
    const std::shared_ptr<State>& state,
    const std::string& method,
    std::uint64_t generation)
{
    if (!state)
        return false;

    std::lock_guard<std::mutex> lock(state->mutex);

    std::map<std::string, Entry>::iterator found =
        state->methods.find(method);

    if (found == state->methods.end())
        return false;

    if (found->second.generation != generation)
        return false;

    state->methods.erase(found);
    return true;
}

RegistrationToken::RegistrationToken()
    : generation_(0)
{
}

RegistrationToken::RegistrationToken(
    const std::shared_ptr<BindingRegistry::State>& state,
    const std::string& method,
    std::uint64_t generation)
    : state_(state),
      method_(method),
      generation_(generation)
{
}

RegistrationToken::~RegistrationToken()
{
    release();
}

RegistrationToken::RegistrationToken(
    RegistrationToken&& other)
    : state_(other.state_),
      method_(other.method_),
      generation_(other.generation_)
{
    other.state_.reset();
    other.method_.clear();
    other.generation_ = 0;
}

RegistrationToken& RegistrationToken::operator=(
    RegistrationToken&& other)
{
    if (this != &other)
    {
        release();

        state_ = other.state_;
        method_ = other.method_;
        generation_ = other.generation_;

        other.state_.reset();
        other.method_.clear();
        other.generation_ = 0;
    }

    return *this;
}

bool RegistrationToken::valid() const
{
    return generation_ != 0 &&
        !state_.expired();
}

bool RegistrationToken::release()
{
    if (generation_ == 0)
        return false;

    const std::shared_ptr<BindingRegistry::State> state =
        state_.lock();

    const bool removed =
        BindingRegistry::release(
            state,
            method_,
            generation_);

    state_.reset();
    method_.clear();
    generation_ = 0;

    return removed;
}

} // namespace detail
} // namespace nativeweb
