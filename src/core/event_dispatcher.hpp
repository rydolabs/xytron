#ifndef NATIVEWEB_EVENT_DISPATCHER_HPP_INCLUDED
#define NATIVEWEB_EVENT_DISPATCHER_HPP_INCLUDED

#include "nativeweb/any.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace nativeweb {
namespace detail {

typedef std::uint64_t EventSubscriptionId;
typedef std::function<void(const Any&)> EventCallback;

class EventDispatcher
{
public:
    EventDispatcher();

    EventSubscriptionId subscribe(
        const std::string& eventName,
        const EventCallback& callback);

    bool unsubscribe(EventSubscriptionId id);

    std::size_t emit(
        const std::string& eventName,
        const Any& payload) const;

    void clear();
    std::size_t size() const;

private:
    struct Subscription
    {
        Subscription()
        {
        }

        Subscription(
            const std::string& valueName,
            const EventCallback& valueCallback)
            : name(valueName),
              callback(valueCallback)
        {
        }

        std::string name;
        EventCallback callback;
    };

    mutable std::mutex mutex_;
    std::map<EventSubscriptionId, Subscription> subscriptions_;
    std::uint64_t nextId_;
};

} // namespace detail
} // namespace nativeweb

#endif
