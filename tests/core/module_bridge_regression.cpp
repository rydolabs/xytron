#include "core/bridge_runtime.hpp"
#include "nativeweb/error.hpp"
#include <iostream>

using namespace nativeweb;
using namespace nativeweb::detail;
static int failures = 0;
#define CHECK(x) do { if(!(x)) { ++failures; std::cerr << "FAIL: " << #x << " at " << __LINE__ << "\n"; } } while(0)
static ModuleDescriptor desc(const char* id, ModuleLanguage language,
                             ModuleRole role = ModuleRole::Feature) {
    return ModuleDescriptor(id,language,role,ModuleDelivery::Embedded);
}
static BridgeMessage request(BridgeRuntime& runtime, const char* name) {
    return parseBridgeMessage(runtime.receive(makeRequestMessage(
        7,name,VariantList())));
}
static void expectValue(BridgeRuntime& runtime,const char* method,int expected) {
    BridgeMessage answer=request(runtime,method);
    CHECK(answer.type == BridgeMessageType::Response);
    if(answer.type == BridgeMessageType::Response)
        CHECK(AnyCast<int>(answer.value) == expected);
}
int main() {
    BridgeRuntime runtime;
    ModuleRegistration main = runtime.registerModule(
        desc("app",ModuleLanguage::Python,ModuleRole::Main));
    ModuleRegistration camera = runtime.registerModule(
        desc("camera",ModuleLanguage::Cpp));
    ModuleRegistration erp = runtime.registerModule(
        desc("erp",ModuleLanguage::CSharp));
    runtime.bindModuleMethod("app","status",
        makeCallable([](){ return 1; }));
    runtime.bindModuleMethod("camera","capture",
        makeCallable([](){ return 42; }));
    runtime.bindModuleMethod("erp","save",
        makeCallable([](){ return 7; }));
    expectValue(runtime,"app.status",1);
    expectValue(runtime,"camera.capture",42);
    expectValue(runtime,"erp.save",7);
    try {
        runtime.bindModuleMethod("camera","capture",
            makeCallable([](){ return 3; }));
        CHECK(false);
    } catch(const Error& error) { CHECK(error.code()=="duplicate_method"); }
    try {
        runtime.bindModuleMethod("missing","call",
            makeCallable([](){ return 3; }));
        CHECK(false);
    } catch(const Error& error) { CHECK(error.code()=="module_not_found"); }

    CHECK(camera.release());
    BridgeMessage gone=request(runtime,"camera.capture");
    CHECK(gone.type == BridgeMessageType::Error);
    CHECK(gone.errorCode == "module_unavailable");
    ModuleRegistration replacement = runtime.registerModule(
        desc("camera",ModuleLanguage::Python));
    CHECK(request(runtime,"camera.capture").errorCode == "module_unavailable");
    CHECK(runtime.unbind("camera.capture"));
    runtime.bindModuleMethod("camera","capture",
        makeCallable([](){ return 99; }));
    expectValue(runtime,"camera.capture",99);

    runtime.shutdown();
    CHECK(request(runtime,"app.status").errorCode == "method_not_found");
    if(failures) return 1;
    std::cout << "Module methods across BridgeRuntime: PASS\n";
    return 0;
}
