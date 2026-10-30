#include "core/bridge_message.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures=0;
void fail(const char* e,const char* f,int l){std::cerr<<f<<":"<<l<<": CHECK failed: "<<e<<std::endl;++failures;}
#define CHECK(e) do{if(!(e))fail(#e,__FILE__,__LINE__);}while(0)

void testRequest()
{
    VariantList args;
    args.push_back(Any(3));
    args.push_back(Any(4));

    const nativeweb::detail::RequestId id =
        static_cast<nativeweb::detail::RequestId>(9007199254740993ULL);

    nativeweb::detail::BridgeMessage message =
        nativeweb::detail::parseBridgeMessage(
            nativeweb::detail::makeRequestMessage(
                id,"math.add",args));

    CHECK(message.type == nativeweb::detail::BridgeMessageType::Request);
    CHECK(message.requestId == id);
    CHECK(message.method == "math.add");
    CHECK(message.args.size() == 2u);
    CHECK(AnyCast<int>(message.args[0]) == 3);
    CHECK(AnyCast<int>(message.args[1]) == 4);
}

void testResponseErrorEvent()
{
    nativeweb::detail::BridgeMessage response =
        nativeweb::detail::parseBridgeMessage(
            nativeweb::detail::makeResponseMessage(7,Any("ok")));

    CHECK(response.type == nativeweb::detail::BridgeMessageType::Response);
    CHECK(response.requestId == 7u);
    CHECK(AnyCast<std::string>(response.value) == "ok");

    nativeweb::detail::BridgeMessage error =
        nativeweb::detail::parseBridgeMessage(
            nativeweb::detail::makeErrorMessage(
                8,
                nativeweb::Error("bad_args","Bad arguments")));

    CHECK(error.type == nativeweb::detail::BridgeMessageType::Error);
    CHECK(error.requestId == 8u);
    CHECK(error.errorCode == "bad_args");
    CHECK(error.errorMessage == "Bad arguments");

    nativeweb::detail::BridgeMessage event =
        nativeweb::detail::parseBridgeMessage(
            nativeweb::detail::makeEventMessage(
                "camera.frame",
                Any(42)));

    CHECK(event.type == nativeweb::detail::BridgeMessageType::Event);
    CHECK(event.eventName == "camera.frame");
    CHECK(AnyCast<int>(event.value) == 42);

    const nativeweb::detail::BridgeMessage cancel =
        nativeweb::detail::parseBridgeMessage(
            nativeweb::detail::makeCancelMessage(9007199254740993ULL));
    CHECK(cancel.type == nativeweb::detail::BridgeMessageType::Cancel);
    CHECK(cancel.requestId == 9007199254740993ULL);
}

void testMalformedMessage()
{
    VariantDict bad;
    bad["type"]="request";
    bad["id"]="1";

    bool threw=false;
    try
    {
        (void)nativeweb::detail::parseBridgeMessage(Any(bad));
    }
    catch(const std::runtime_error&)
    {
        threw=true;
    }

    CHECK(threw);
}

}

int main()
{
    testRequest();
    testResponseErrorEvent();
    testMalformedMessage();

    if(failures){std::cerr<<"Bridge message regression failed: "<<failures<<std::endl;return 1;}
    std::cout<<"Bridge message regression: PASS"<<std::endl;
    return 0;
}
