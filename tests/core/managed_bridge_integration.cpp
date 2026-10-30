#include "browser/common/backend_registry.hpp"
#include "core/bridge_message.hpp"
#include "nativeweb/app_resources.hpp"
#include "nativeweb/webview.hpp"
#include "nativeweb/error.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {
int failures=0;
#define CHECK(x) do{if(!(x)){std::cerr<<"CHECK FAILED: "<<#x<<" at "<<__LINE__<<"\n";++failures;}}while(0)
class Backend : public nativeweb::detail::BrowserBackend {
public:
 Backend():listener(0),started(false),serial(0){instance=this;}
 ~Backend(){if(instance==this)instance=0;}
 static Backend* instance;
 nativeweb::detail::BrowserBackendListener* listener;
 bool started;
 std::mutex mu;
 std::condition_variable cv;
 unsigned serial;
 Any message;
 nativeweb::detail::VerifiedFrameContext caller;
 const char* engineName()const override{return "test-cef";}
 nativeweb::Engine engine()const override{return nativeweb::Engine::Cef;}
 nativeweb::Capabilities capabilities()const override{return nativeweb::Capabilities();}
 void setListener(nativeweb::detail::BrowserBackendListener* l)override{listener=l;}
 void create(const nativeweb::detail::BrowserCreateParams& params)override{
  started=true;
  if(listener)listener->onBrowserCreated();
  if(listener && params.source=="app://desktop/index.html"){
   caller.frameId=42;caller.navigation=1;
   caller.engineVerified=true;caller.mainFrame=true;caller.trustedContent=true;
   listener->onTrustedFrameActivated(caller);
  }
 }
 void destroy()override{started=false;if(listener)listener->onBrowserClosed();}
 bool isCreated()const override{return started;}
 void load(const std::string& u)override{if(listener)listener->onLoadStarted(u);}
 void reload()override{
  caller.engineVerified=false;
  if(listener)listener->onLoadStarted("app://desktop/index.html");
 }
 void postBridgeMessage(const Any& m)override{{std::lock_guard<std::mutex> lk(mu);message=m;++serial;}cv.notify_all();}
 unsigned count(){std::lock_guard<std::mutex> lk(mu);return serial;}
 Any wait(unsigned after,int millis=5000){std::unique_lock<std::mutex> lk(mu);
  if(!cv.wait_for(lk,std::chrono::milliseconds(millis),[this,after]{return serial>after;}))return Any();
  return message;
 }
};
Backend* Backend::instance=0;
void registration(){nativeweb::detail::registerBrowserBackendFactory(nativeweb::Engine::Cef,
 [](){return std::unique_ptr<nativeweb::detail::BrowserBackend>(new Backend());});}
nativeweb::detail::BridgeMessage call(unsigned id,const std::string& op,
 const VariantList& args=VariantList(),
 const nativeweb::detail::VerifiedFrameContext* context=nullptr){
 Backend* b=Backend::instance;unsigned n=b->count();
 b->listener->onVerifiedBridgeMessage(
  nativeweb::detail::makeRequestMessage(id,op,args),
  context?*context:b->caller);
 Any msg=b->wait(n);
 if(msg.empty())throw std::runtime_error("Managed bridge no response");
 return nativeweb::detail::parseBridgeMessage(msg);
}
bool denied(nativeweb::WebView& w,const std::vector<std::string>& ops,
 const std::vector<std::string>& cmd,const std::string& code){
 try{w.bindManagedWorker("example_py",ops,cmd,3000);}
 catch(const nativeweb::Error& e){return e.code()==code;}return false;
}
void run(const std::string& python,const std::string& fixture){
 registration();
 const std::vector<std::string> cmd{python,fixture,"normal"};
 const std::vector<std::string> ops{"example_py.Player.create",
 "example_py.Player.method.hit","example_py.Player.get.health",
 "example_py.Player.set.health","example_py.Player.dispose"};
 nativeweb::WebView w;
 CHECK(denied(w,ops,cmd,"permission_denied"));
 nativeweb::WebViewOptions opt;opt.engine=nativeweb::Engine::Cef;
 w.create(0,"file:///tmp/legacy.html",opt);
 CHECK(denied(w,ops,cmd,"permission_denied"));w.destroy();
 std::shared_ptr<nativeweb::AppResourceBundle> bundle(new nativeweb::AppResourceBundle());
 bundle->add("app://desktop/index.html","text/html","<html></html>");
 nativeweb::WebViewOptions packed;packed.engine=nativeweb::Engine::Cef;packed.appResources=bundle;
 nativeweb::WebView app;app.create(0,"app://desktop/index.html",packed);
 CHECK(denied(app,{"example_py.Player.method.__bad"},cmd,"invalid_grant"));
 CHECK(denied(app,{"example_py.Player.create","example_py.Player.create"},cmd,"invalid_grant"));
 app.bindManagedWorker("example_py",ops,cmd,3000);
 CHECK(denied(app,ops,cmd,"invalid_grant"));
 bool reserved=false;try{app.bind("example_py.Player.create",[](){return 1;});}
 catch(const nativeweb::Error& e){reserved=e.code()=="reserved_method";}
 CHECK(reserved);
 VariantList name;name.push_back(std::string("Ada"));
 Backend* active=Backend::instance;
 nativeweb::detail::VerifiedFrameContext invalid=active->caller;
 invalid.mainFrame=false;
 CHECK(call(50,"example_py.Player.create",name,&invalid).errorCode=="permission_denied");
 invalid=active->caller; invalid.navigation+=1;
 CHECK(call(51,"example_py.Player.create",name,&invalid).errorCode=="permission_denied");
 invalid=active->caller; invalid.engineVerified=false;
 CHECK(call(52,"example_py.Player.create",name,&invalid).errorCode=="permission_denied");
 invalid=active->caller; invalid.frameId+=1;
 CHECK(call(53,"example_py.Player.create",name,&invalid).errorCode=="permission_denied");
 invalid=active->caller; invalid.trustedContent=false;
 CHECK(call(54,"example_py.Player.create",name,&invalid).errorCode=="permission_denied");
 unsigned legacyVersion=active->count();
 active->listener->onBridgeMessage(
  nativeweb::detail::makeRequestMessage(55,"example_py.Player.create",name));
 CHECK(nativeweb::detail::parseBridgeMessage(active->wait(legacyVersion))
       .errorCode=="permission_denied");
 auto res=call(1,"example_py.Player.create",name);
 CHECK(res.type==nativeweb::detail::BridgeMessageType::Response);
 std::string handle=AnyCast<std::string>(res.value);
 VariantList h;h.push_back(handle);
 VariantList v=h;v.push_back(9);
 CHECK(AnyCast<int>(call(2,"example_py.Player.method.hit",v).value)==91);
 CHECK(AnyCast<int>(call(3,"example_py.Player.get.health",h).value)==91);
 v[1]=Any(43);
 CHECK(call(4,"example_py.Player.set.health",v).type==nativeweb::detail::BridgeMessageType::Response);
 CHECK(AnyCast<int>(call(5,"example_py.Player.get.health",h).value)==43);
 CHECK(!app.revokeManagedOperation("other","example_py.Player.set.health"));
 CHECK(!app.revokeManagedOperation("example_py","example_py.Player.method.secret"));
 CHECK(app.revokeManagedOperation("example_py","example_py.Player.set.health"));
 CHECK(!app.revokeManagedOperation("example_py","example_py.Player.set.health"));
 v[1]=Any(99);
 CHECK(call(20,"example_py.Player.set.health",v).errorCode=="permission_denied");
 CHECK(AnyCast<int>(call(21,"example_py.Player.get.health",h).value)==43);
 CHECK(call(6,"example_py.Player.method.secret",h).errorCode=="method_not_found");
 CHECK(call(7,"example_py.Player.dispose",h).type==nativeweb::detail::BridgeMessageType::Response);
 CHECK(call(8,"example_py.Player.dispose",h).errorCode=="worker_rejected");
 CHECK(!app.revokeManagedWorker("other"));
 CHECK(app.revokeManagedWorker("example_py"));
 CHECK(!app.revokeManagedWorker("example_py"));
 CHECK(call(22,"example_py.Player.create",name).errorCode=="method_not_found");
 app.reload();
 CHECK(call(9,"example_py.Player.create",name).errorCode=="method_not_found");
 app.destroy();
 nativeweb::WebView late;late.create(0,"app://desktop/index.html",packed);
 late.bindManagedWorker("example_py",{"example_py.Player.create"},
  {python,fixture,"stall"},150);
 Backend* backend=Backend::instance;unsigned n=backend->count();
 backend->listener->onVerifiedBridgeMessage(nativeweb::detail::makeRequestMessage(
  10,"example_py.Player.create",VariantList()),backend->caller);
 std::this_thread::sleep_for(std::chrono::milliseconds(55));
 backend->reload();
 CHECK(backend->wait(n,500).empty());
 late.destroy();
}
}
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"expected ABS_PYTHON and fixture\n";return 2;}
 try{run(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
 if(failures)return 1;
 std::cout<<"PASS: managed packaged bridge -> real Python worker and navigation revoke\n";
 return 0;
}
