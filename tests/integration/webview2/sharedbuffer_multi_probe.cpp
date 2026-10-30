#include "browser/webview2/webview2_backend.hpp"
#include "core/bridge_message.hpp"
#include "nativeweb/app_resources.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
const char kHtml[]=R"XYTRON_MULTI(<!doctype html><html><body><script>
(() => {
  chrome.webview.addEventListener('sharedbufferreceived', e => {
    let buffer;
    try {
      const meta=e.additionalData;
      if(!meta || meta.type!=='xytron.shared.probe')
        throw Error('Unrecognized SharedBuffer metadata');
      buffer=e.getBuffer();
      const bytes=new Uint8Array(buffer);
      if(bytes.length!==1048576 || meta.size!==1048576 || bytes[0]!==17 ||
         bytes[bytes.length-1]!==((37*(bytes.length-1)+17)&255))
        throw Error('Bad SharedBuffer bytes');
      chrome.webview.releaseBuffer(buffer);
      buffer=null;
      // Do not ACK yet: the native harness inspects each view's independent
      // pending bytes and deliberate cross-instance grant confusion.
      xytron.postMessage('isolation.received',{id:meta.id,grant:meta.grant});
    } catch(error) {
      if(buffer) try{ chrome.webview.releaseBuffer(buffer); }catch(_){}
      xytron.postMessage('isolation.failure',String(error));
    }
  });
  xytron.postMessage('isolation.ready');
})();
</script></body></html>)XYTRON_MULTI";

class Listener : public nativeweb::detail::BrowserBackendListener
{
public:
    int ready=0;
    std::string failure;
    std::map<int,std::string> grants;
    std::string originalGrant;
    void onBrowserCreated() override {}
    void onBrowserClosed() override {}
    void onLoadStarted(const std::string&) override {}
    void onLoadFinished(const std::string&) override {}
    void onBridgeMessage(const Any&) override{ failure="Unverified message"; }
    void onVerifiedBridgeMessage(
        const Any& message,const nativeweb::detail::VerifiedFrameContext& frame) override
    {
        try {
            if(!frame.mainFrame||!frame.trustedContent||!frame.engineVerified)
                throw std::runtime_error("Frame not trusted");
            const nativeweb::detail::BridgeMessage m=
                nativeweb::detail::parseBridgeMessage(message);
            if(m.type!=nativeweb::detail::BridgeMessageType::Event)
                return;
            if(m.eventName=="isolation.ready"){++ready;return;}
            const VariantDict& env=AnyCast<const VariantDict&>(message);
            if(m.eventName=="isolation.failure")
                throw std::runtime_error(AnyCast<std::string>(env.at("payload")));
            if(m.eventName!="isolation.received")
                throw std::runtime_error("Unknown isolation event");
            const VariantDict& payload=AnyCast<const VariantDict&>(
                env.at("payload"));
            const int id=AnyCast<int>(payload.at("id"));
            const std::string grant=AnyCast<std::string>(payload.at("grant"));
            if(grant.size()!=48||grants.count(id))
                throw std::runtime_error("Invalid or duplicate SharedBuffer grant");
            grants[id]=grant;
        } catch(const std::exception& e){failure=e.what();}
    }
};

LRESULT CALLBACK wndProc(HWND w,UINT m,WPARAM p,LPARAM q)
{return DefWindowProcW(w,m,p,q);}

bool deliver(nativeweb::detail::WebView2Backend& backend,int id)
{
    nativeweb::Binary bytes(1048576);
    for(std::size_t i=0;i<bytes.size();++i)
        bytes[i]=static_cast<unsigned char>((i*37+17)&255);
    return backend.postReadOnlySharedBufferProbe(bytes,id);
}
} // namespace

int main()
{
    WNDCLASSW cls={};
    cls.lpfnWndProc=wndProc;
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"Xytron.MultiSharedBuffer";
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
        return 2;
    HWND windows[2]={};
    for(int i=0;i<2;++i)
        windows[i]=CreateWindowExW(0,cls.lpszClassName,L"Multi SharedBuffer",
            WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,700,450,
            nullptr,nullptr,cls.hInstance,nullptr);
    if(!windows[0]||!windows[1])return 3;
    nativeweb::detail::WebView2Backend views[2];
    Listener listeners[2];
    int stage=0,result=1;
    try {
        for(int i=0;i<2;++i) {
            views[i].setListener(&listeners[i]);
            const std::string url=i==0 ?
                "app://xytron-first/isolation.html" :
                "app://xytron-second/isolation.html";
            std::shared_ptr<nativeweb::AppResourceBundle> assets(
                new nativeweb::AppResourceBundle());
            assets->add(url,"text/html",kHtml);
            nativeweb::detail::BrowserCreateParams params;
            params.parent=reinterpret_cast<nativeweb::NativeWindowHandle>(windows[i]);
            params.source=url;
            params.appResources=assets;
            views[i].create(params);
        }
        const auto started=std::chrono::steady_clock::now();
        while(std::chrono::steady_clock::now()-started<
                std::chrono::seconds(65) &&
              listeners[0].failure.empty() && listeners[1].failure.empty()) {
            MSG msg;
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if(stage==0 && listeners[0].ready==1 && listeners[1].ready==1) {
                if(!deliver(views[0],10)||!deliver(views[0],11) ||
                   deliver(views[0],12)||!deliver(views[1],10))
                    throw std::runtime_error("Per-view quota crossed while filling");
                stage=1;
            } else if(stage==1 &&
                listeners[0].grants.count(10)&&listeners[0].grants.count(11)&&
                listeners[1].grants.count(10)) {
                if(views[0].pendingReadOnlySharedBufferBytesForTest()!=2097152u ||
                   views[1].pendingReadOnlySharedBufferBytesForTest()!=1048576u ||
                   views[0].acknowledgeReadOnlySharedBufferProbe(
                       10,listeners[1].grants.at(10)) ||
                   views[1].acknowledgeReadOnlySharedBufferProbe(
                       10,listeners[0].grants.at(10)))
                    throw std::runtime_error("Cross-view allocation/grant confusion");
                listeners[0].originalGrant=listeners[0].grants.at(10);
                listeners[0].grants.clear();
                views[0].reload();
                stage=2;
            } else if(stage==2 && listeners[0].ready>=2) {
                if(views[0].pendingReadOnlySharedBufferBytesForTest()!=0 ||
                   views[0].cumulativeReadOnlySharedBufferBytesForTest()!=0 ||
                   views[1].pendingReadOnlySharedBufferBytesForTest()!=1048576u ||
                   !deliver(views[0],10))
                    throw std::runtime_error("Reload reset another view's quota");
                stage=3;
            } else if(stage==3 && listeners[0].grants.count(10)) {
                if(listeners[0].originalGrant==listeners[0].grants.at(10) ||
                   !views[1].acknowledgeReadOnlySharedBufferProbe(
                       10,listeners[1].grants.at(10)) ||
                   views[1].pendingReadOnlySharedBufferBytesForTest()!=0 ||
                   views[0].pendingReadOnlySharedBufferBytesForTest()!=1048576u ||
                   !views[0].acknowledgeReadOnlySharedBufferProbe(
                       10,listeners[0].grants.at(10)) ||
                   views[0].pendingReadOnlySharedBufferBytesForTest()!=0)
                    throw std::runtime_error("Independent quota recovery failed");
                std::cout<<"xytron-sharedbuffer-multi-isolation:PASS"
                         <<" distinct_grants=1 views=2" <<std::endl;
                result=0;
                break;
            }
            Sleep(5);
        }
        if(result && listeners[0].failure.empty() && listeners[1].failure.empty())
            listeners[0].failure="Timed out at stage "+std::to_string(stage);
    } catch(const std::exception& e){listeners[0].failure=e.what();}
    for(int i=0;i<2;++i)views[i].destroy();
    for(int i=0;i<2;++i)DestroyWindow(windows[i]);
    for(int i=0;i<2;++i)
        if(!listeners[i].failure.empty())
            std::cerr<<"Multi SharedBuffer failure "<<i<<": "
                     <<listeners[i].failure<<std::endl;
    std::cout<<"webview2-multi-isolation-e2e: "<<(result==0?"PASS":"FAIL")<<std::endl;
    return result;
}
