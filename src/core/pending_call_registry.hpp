#ifndef NATIVEWEB_PENDING_CALL_REGISTRY_HPP_INCLUDED
#define NATIVEWEB_PENDING_CALL_REGISTRY_HPP_INCLUDED

#include "nativeweb/any.hpp"
#include "nativeweb/error.hpp"
#include "nativeweb/detail/request_id.hpp"
#include "nativeweb/detail/pending_result.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace nativeweb {
namespace detail {

class PendingCall
{
public:
    PendingCall(RequestId requestId, std::future<Any>& future)
        : id(requestId),
          result(std::move(future))
    {
    }

    PendingCall(PendingCall&& other)
        : id(other.id),
          result(std::move(other.result))
    {
        other.id = 0;
    }

    PendingCall& operator=(PendingCall&& other)
    {
        if (this != &other)
        {
            id = other.id;
            result = std::move(other.result);
            other.id = 0;
        }

        return *this;
    }

    PendingCall(const PendingCall&) = delete;
    PendingCall& operator=(const PendingCall&) = delete;

    RequestId id;
    std::future<Any> result;
};

class PendingCallRegistry
{
public:
    PendingCallRegistry();

    PendingCall create();

    RequestId add(
        const std::shared_ptr<PendingResultBase>& result);

    bool resolve(RequestId id, const Any& value);
    bool reject(RequestId id, const Error& error);

    void rejectAll(const Error& error);

    std::size_t size() const;

private:
    typedef std::shared_ptr<PendingResultBase> StatePtr;

    StatePtr take(RequestId id);

    mutable std::mutex mutex_;
    std::map<RequestId, StatePtr> pending_;
    std::atomic<RequestId> nextId_;
};

} // namespace detail
} // namespace nativeweb

#endif
