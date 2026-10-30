#include "core/event_dispatcher.hpp"

#include <vector>

namespace nativeweb {
namespace detail {

EventDispatcher::EventDispatcher()
    : nextId_(1)
{
}

EventSubscriptionId EventDispatcher::subscribe(
    const std::string& eventName,
    const EventCallback& callback)
{
    if (!callback)
        return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    const EventSubscriptionId id = nextId_++;
    subscriptions_.insert(
        std::make_pair(
            id,
            Subscription(eventName, callback)));

    return id;
}

bool EventDispatcher::unsubscribe(EventSubscriptionId id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(id) != 0;
}

std::size_t EventDispatcher::emit(
    const std::string& eventName,
    const Any& payload) const
{
    std::vector<EventCallback> callbacks;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (std::map<EventSubscriptionId, Subscription>::const_iterator it =
                 subscriptions_.begin();
             it != subscriptions_.end();
             ++it)
        {
            if (it->second.name == eventName)
                callbacks.push_back(it->second.callback);
        }
    }

    // One-way messages have no response channel for callback exceptions.
    // Never let one application subscriber unwind through browser IPC or
    // prevent later subscribers from receiving the same event.
    for (std::size_t i = 0; i < callbacks.size(); ++i)
    {
        try { callbacks[i](payload); }
        catch (...) { /* isolate untrusted callback failures */ }
    }

    return callbacks.size();
}

void EventDispatcher::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.clear();
}

std::size_t EventDispatcher::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.size();
}

} // namespace detail
} // namespace nativeweb
