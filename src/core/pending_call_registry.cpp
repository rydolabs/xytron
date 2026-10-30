#include "core/pending_call_registry.hpp"

#include <exception>

namespace nativeweb {
namespace detail {

PendingCallRegistry::PendingCallRegistry()
    : nextId_(1)
{
}

PendingCall PendingCallRegistry::create()
{
    const std::shared_ptr<PendingResult<Any> > state(
        new PendingResult<Any>());

    std::future<Any> future =
        state->future();

    const RequestId id =
        add(state);

    return PendingCall(id, future);
}

RequestId PendingCallRegistry::add(
    const std::shared_ptr<PendingResultBase>& result)
{
    if (!result)
        throw std::invalid_argument(
            "NativeWeb pending result cannot be null");

    const RequestId id =
        nextId_.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.insert(
            std::make_pair(
                id,
                result));
    }

    return id;
}

PendingCallRegistry::StatePtr PendingCallRegistry::take(RequestId id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::map<RequestId, StatePtr>::iterator found = pending_.find(id);
    if (found == pending_.end())
        return StatePtr();

    StatePtr state = found->second;
    pending_.erase(found);
    return state;
}

bool PendingCallRegistry::resolve(RequestId id, const Any& value)
{
    const StatePtr state = take(id);
    if (!state)
        return false;

    state->resolve(value);
    return true;
}

bool PendingCallRegistry::reject(RequestId id, const Error& error)
{
    const StatePtr state = take(id);
    if (!state)
        return false;

    state->reject(error);
    return true;
}

void PendingCallRegistry::rejectAll(const Error& error)
{
    std::vector<StatePtr> states;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        states.reserve(pending_.size());

        for (std::map<RequestId, StatePtr>::iterator it = pending_.begin();
             it != pending_.end();
             ++it)
        {
            states.push_back(it->second);
        }

        pending_.clear();
    }

    for (std::size_t i = 0; i < states.size(); ++i)
    {
        states[i]->reject(error);
    }
}

std::size_t PendingCallRegistry::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

} // namespace detail
} // namespace nativeweb
