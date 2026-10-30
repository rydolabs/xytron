#include "core/bridge_runtime.hpp"

#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

int failures=0;
void fail(const char* e,const char* f,int l){std::cerr<<f<<":"<<l<<": CHECK failed: "<<e<<std::endl;++failures;}
#define CHECK(e) do{if(!(e))fail(#e,__FILE__,__LINE__);}while(0)

void testIncomingRequest()
{
    nativeweb::detail::BridgeRuntime bridge;

    bridge.bind(
        "math.add",
        nativeweb::detail::makeDynamicFunction(
            [](int a,int b){return a+b;}));

    VariantList args;
    args.push_back(Any(3));
    args.push_back(Any(4));

    Any reply=bridge.receive(
        nativeweb::detail::makeRequestMessage(
            10,"math.add",args));

    nativeweb::detail::BridgeMessage parsed=
        nativeweb::detail::parseBridgeMessage(reply);

    CHECK(parsed.type==nativeweb::detail::BridgeMessageType::Response);
    CHECK(parsed.requestId==10u);
    CHECK(AnyCast<int>(parsed.value)==7);
}

void testMissingAndException()
{
    nativeweb::detail::BridgeRuntime bridge;
    VariantList args;

    nativeweb::detail::BridgeMessage missing=
        nativeweb::detail::parseBridgeMessage(
            bridge.receive(
                nativeweb::detail::makeRequestMessage(
                    11,"missing",args)));

    CHECK(missing.type==nativeweb::detail::BridgeMessageType::Error);
    CHECK(missing.errorCode=="method_not_found");

    bridge.bind(
        "fail",
        nativeweb::detail::makeDynamicFunction(
            []()->int{
                throw std::runtime_error("broken");
            }));

    nativeweb::detail::BridgeMessage failed=
        nativeweb::detail::parseBridgeMessage(
            bridge.receive(
                nativeweb::detail::makeRequestMessage(
                    12,"fail",args)));

    CHECK(failed.errorCode=="native_exception");
    CHECK(failed.errorMessage=="broken");
}

void testOutboundResolveReject()
{
    nativeweb::detail::BridgeRuntime bridge;
    VariantList args;
    args.push_back(Any("hello"));

    nativeweb::detail::OutboundCall call=
        bridge.call("ui.echo",args);

    CHECK(bridge.pendingCount()==1u);

    nativeweb::detail::BridgeMessage request=
        nativeweb::detail::parseBridgeMessage(call.message);

    CHECK(request.type==nativeweb::detail::BridgeMessageType::Request);
    CHECK(request.requestId==call.id);
    CHECK(request.method=="ui.echo");

    (void)bridge.receive(
        nativeweb::detail::makeResponseMessage(
            call.id,
            Any("world")));

    CHECK(bridge.pendingCount()==0u);
    CHECK(AnyCast<std::string>(call.result.get())=="world");

    nativeweb::detail::OutboundCall rejected=
        bridge.call("ui.fail",VariantList());

    (void)bridge.receive(
        nativeweb::detail::makeErrorMessage(
            rejected.id,
            nativeweb::Error("js_error","JS failed")));

    bool threw=false;
    try{(void)rejected.result.get();}
    catch(const nativeweb::Error& e)
    {
        threw=true;
        CHECK(e.code()=="js_error");
        CHECK(std::string(e.what())=="JS failed");
    }
    CHECK(threw);
}

void testTypedOutboundCall()
{
    nativeweb::detail::BridgeRuntime bridge;

    const std::shared_ptr<nativeweb::detail::PendingResult<int> > pending(
        new nativeweb::detail::PendingResult<int>());

    std::future<int> result =
        pending->future();

    nativeweb::detail::OutboundRequest call =
        bridge.callWithPending(
            "ui.answer",
            VariantList(),
            pending);

    CHECK(bridge.pendingCount() == 1u);

    (void)bridge.receive(
        nativeweb::detail::makeResponseMessage(
            call.id,
            Any(42)));

    CHECK(bridge.pendingCount() == 0u);
    CHECK(result.get() == 42);
}

void testCallableMetadataAndScopedBinding()
{
    nativeweb::detail::BridgeRuntime bridge;

    bridge.bind(
        "typed.add",
        nativeweb::detail::makeCallable(
            [](int a, int b) { return a + b; }));

    nativeweb::detail::CallableSignature signature;

    CHECK(bridge.methodSignature("typed.add", &signature));
    CHECK(signature.arity() == 2u);
    CHECK(signature.result.kind == nativeweb::detail::CallableTypeKind::Integer);

    {
        nativeweb::detail::RegistrationToken token =
            bridge.registerBinding(
                "plugin.temp",
                nativeweb::detail::makeCallable(
                    []() { return 9; }));

        CHECK(bridge.hasMethod("plugin.temp"));
        CHECK(token.valid());
    }

    CHECK(!bridge.hasMethod("plugin.temp"));
}

void testEvents()
{
    nativeweb::detail::BridgeRuntime bridge;
    int observed=0;

    bridge.subscribe("camera.frame",[&](const Any& value){
        observed=AnyCast<int>(value);
    });

    Any envelope=bridge.eventMessage("camera.frame",Any(77));
    Any reply=bridge.receive(envelope);

    CHECK(reply.empty());
    CHECK(observed==77);
}

void testShutdownRejectsPendingAndClearsState()
{
    nativeweb::detail::BridgeRuntime bridge;
    bridge.bind("noop",nativeweb::detail::makeDynamicFunction([](){return 1;}));
    bridge.subscribe("event",[](const Any&){});

    nativeweb::detail::OutboundCall call=
        bridge.call("ui.wait",VariantList());

    bridge.shutdown();

    CHECK(bridge.pendingCount()==0u);
    CHECK(bridge.methodCount()==0u);

    bool threw=false;
    try{(void)call.result.get();}
    catch(const nativeweb::Error& e)
    {
        threw=true;
        CHECK(e.code()=="webview_destroyed");
    }
    CHECK(threw);
}

}

int main()
{
    testIncomingRequest();
    testMissingAndException();
    testOutboundResolveReject();
    testTypedOutboundCall();
    testCallableMetadataAndScopedBinding();
    testEvents();
    testShutdownRejectsPendingAndClearsState();

    if(failures){std::cerr<<"Bridge runtime regression failed: "<<failures<<std::endl;return 1;}
    std::cout<<"Bridge runtime regression: PASS"<<std::endl;
    return 0;
}
