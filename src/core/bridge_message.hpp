#ifndef NATIVEWEB_BRIDGE_MESSAGE_HPP_INCLUDED
#define NATIVEWEB_BRIDGE_MESSAGE_HPP_INCLUDED

#include "nativeweb/any.hpp"
#include "nativeweb/error.hpp"
#include "nativeweb/detail/request_id.hpp"

#include <string>

namespace nativeweb {
namespace detail {

enum class BridgeMessageType
{
    Request,
    Response,
    Error,
    Event,
    Cancel
};

struct BridgeMessage
{
    BridgeMessage();

    BridgeMessageType type;
    RequestId requestId;

    std::string method;
    VariantList args;

    Any value;

    std::string errorCode;
    std::string errorMessage;

    std::string eventName;
};

Any makeRequestMessage(
    RequestId requestId,
    const std::string& method,
    const VariantList& args);

Any makeResponseMessage(
    RequestId requestId,
    const Any& value);

Any makeErrorMessage(
    RequestId requestId,
    const Error& error);

// JS -> native: cancel one in-flight JS-initiated request by ID.
Any makeCancelMessage(RequestId requestId);

Any makeEventMessage(
    const std::string& eventName,
    const Any& payload);

BridgeMessage parseBridgeMessage(const Any& message);

} // namespace detail
} // namespace nativeweb

#endif
