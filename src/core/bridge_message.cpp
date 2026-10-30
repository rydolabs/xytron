#include "core/bridge_message.hpp"

#include <sstream>
#include <stdexcept>

namespace nativeweb {
namespace detail {

namespace {

std::string requestIdToString(RequestId id)
{
    std::ostringstream stream;
    stream << id;
    return stream.str();
}

RequestId requestIdFromString(const std::string& text)
{
    std::istringstream stream(text);
    RequestId id = 0;
    stream >> id;

    if (!stream || !stream.eof())
        throw std::runtime_error("Invalid NativeWeb bridge request id");

    return id;
}

const Any& requireField(
    const VariantDict& dict,
    const std::string& key)
{
    VariantDict::const_iterator found = dict.find(key);

    if (found == dict.end())
        throw std::runtime_error(
            "Missing NativeWeb bridge field: " + key);

    return found->second;
}

std::string requireString(
    const VariantDict& dict,
    const std::string& key)
{
    return AnyCast<std::string>(
        requireField(dict, key));
}

} // namespace

BridgeMessage::BridgeMessage()
    : type(BridgeMessageType::Request),
      requestId(0)
{
}

Any makeRequestMessage(
    RequestId requestId,
    const std::string& method,
    const VariantList& args)
{
    VariantDict dict;
    dict["type"] = "request";
    dict["id"] = requestIdToString(requestId);
    dict["method"] = method;
    dict["args"] = args;
    return Any(dict);
}

Any makeResponseMessage(
    RequestId requestId,
    const Any& value)
{
    VariantDict dict;
    dict["type"] = "response";
    dict["id"] = requestIdToString(requestId);
    dict["value"] = value;
    return Any(dict);
}

Any makeErrorMessage(
    RequestId requestId,
    const Error& error)
{
    VariantDict dict;
    dict["type"] = "error";
    dict["id"] = requestIdToString(requestId);
    dict["code"] = error.code();
    dict["message"] = std::string(error.what());
    return Any(dict);
}

Any makeCancelMessage(RequestId requestId)
{
    VariantDict dict;
    dict["type"]="cancel";
    dict["id"]=requestIdToString(requestId);
    return Any(dict);
}

Any makeEventMessage(
    const std::string& eventName,
    const Any& payload)
{
    VariantDict dict;
    dict["type"] = "event";
    dict["event"] = eventName;
    dict["payload"] = payload;
    return Any(dict);
}

BridgeMessage parseBridgeMessage(const Any& message)
{
    const VariantDict& dict =
        AnyCast<const VariantDict&>(message);

    const std::string type =
        requireString(dict, "type");

    BridgeMessage result;

    if (type == "request")
    {
        result.type = BridgeMessageType::Request;
        result.requestId = requestIdFromString(
            requireString(dict, "id"));
        result.method = requireString(dict, "method");
        result.args = AnyCast<VariantList>(
            requireField(dict, "args"));
        return result;
    }

    if (type == "response")
    {
        result.type = BridgeMessageType::Response;
        result.requestId = requestIdFromString(
            requireString(dict, "id"));
        result.value = requireField(dict, "value");
        return result;
    }

    if (type == "error")
    {
        result.type = BridgeMessageType::Error;
        result.requestId = requestIdFromString(
            requireString(dict, "id"));
        result.errorCode = requireString(dict, "code");
        result.errorMessage = requireString(dict, "message");
        return result;
    }

    if (type == "cancel")
    {
        result.type=BridgeMessageType::Cancel;
        result.requestId=requestIdFromString(requireString(dict,"id"));
        return result;
    }

    if (type == "event")
    {
        result.type = BridgeMessageType::Event;
        result.eventName = requireString(dict, "event");
        result.value = requireField(dict, "payload");
        return result;
    }

    throw std::runtime_error(
        "Unknown NativeWeb bridge message type: " + type);
}

} // namespace detail
} // namespace nativeweb
