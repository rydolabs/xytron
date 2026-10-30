#include "browser/webview2/webview2_backend.hpp"
#include "core/bridge_message.hpp"
#include "nativeweb/app_resources.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
const char kPage[]="app://desktop/renderer_failure_probe.html";
const char kHtml[]=R"XYTRON_CRASH(<!doctype html><html><body><script>
(() => {
  'use strict';
  const keepAlive=[];
  chrome.webview.addEventListener('sharedbufferreceived',event=>{
    try {
      const meta=event.additionalData;
      if(!meta || meta.type!=='xytron.shared.probe'||meta.id!==7)
        throw Error('Invalid shared-memory metadata');
      const buffer=event.getBuffer();
      const bytes=new Uint8Array(buffer);
      if(bytes.length!==meta.size || bytes[0]!==17 ||
         bytes[bytes.length-1]!==((37*(bytes.length-1)+17)&255))
        throw Error('Shared buffer corrupted');
      // Deliberately retain a live mapping without release or ACK.
      keepAlive.push(buffer);
      xytron.postMessage('probe.held',{size:bytes.length,grant:meta.grant});
    } catch(error) {xytron.postMessage('probe.failure',String(error));}
  });
  xytron.postMessage('probe.ready');
})();
</script></body></html>)XYTRON_CRASH";

class Listener : public nativeweb::detail::BrowserBackendListener
{
public:
    int ready=0,held=0;
    std::string firstGrant,secondGrant,failure;
    void onBrowserCreated() override {}
    void onBrowserClosed() override {}
    void onLoadStarted(const std::string&) override {}
    void onLoadFinished(const std::string&) override {}
    void onBridgeMessage(const Any&) override {failure="Unverified bridge event";}
    void onVerifiedBridgeMessage(
        const Any& message,const nativeweb::detail::VerifiedFrameContext& frame) override
    {
        try {
            if(!frame.mainFrame||!frame.trustedContent||!frame.engineVerified)
                throw std::runtime_error("Unverified sender");
            const nativeweb::detail::BridgeMessage parsed=
                nativeweb::detail::parseBridgeMessage(message);
            if(parsed.type!=nativeweb::detail::BridgeMessageType::Event)
                return;
            if(parsed.eventName=="probe.ready"){++ready;return;}
            const VariantDict& object=AnyCast<const VariantDict&>(message);
            if(parsed.eventName=="probe.failure") {
                throw std::runtime_error(AnyCast<std::string>(object.at("payload")));
            }
            if(parsed.eventName!="probe.held")
                throw std::runtime_error("Unknown event");
            const VariantDict& payload=AnyCast<const VariantDict&>(
                object.at("payload"));
            const int bytes=AnyCast<int>(payload.at("size"));
            const std::string grant=AnyCast<std::string>(payload.at("grant"));
            if(grant.size()!=48 || (held==0 && bytes!=1048576) ||
               (held==1 && bytes!=1024) || held>=2)
                throw std::runtime_error("Unexpected buffer response");
            if(held==0)firstGrant=grant;
            else secondGrant=grant;
            ++held;
        } catch(const std::exception& e) {failure=e.what();}
    }
};

LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{return DefWindowProcW(hwnd,msg,wp,lp);}
} // namespace

int main()
{
    WNDCLASSW cls={};
    cls.lpfnWndProc=&wndProc;
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"Xytron.RendererFailureProbe";
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
        return 2;
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Renderer failure",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,700,500,
        nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return 3;

    int result=1,stage=0;
    nativeweb::detail::WebView2Backend backend;
    Listener listener;
    backend.setListener(&listener);
    try {
        std::shared_ptr<nativeweb::AppResourceBundle> bundle(
            new nativeweb::AppResourceBundle());
        bundle->add(kPage,"text/html",kHtml);
        nativeweb::detail::BrowserCreateParams params;
        params.parent=reinterpret_cast<nativeweb::NativeWindowHandle>(window);
        params.source=kPage;
        params.appResources=bundle;
        backend.create(params);
        const auto began=std::chrono::steady_clock::now();
        while(std::chrono::steady_clock::now()-began<
              std::chrono::seconds(70) && listener.failure.empty()) {
            MSG message;
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if(stage==0 && listener.ready==1) {
                nativeweb::Binary bytes(1048576);
                for(std::size_t i=0;i<bytes.size();++i)
                    bytes[i]=static_cast<unsigned char>((i*37+17)&255);
                if(!backend.postReadOnlySharedBufferProbe(bytes,7))
                    throw std::runtime_error("First buffer failed");
                stage=1;
            } else if(stage==1 && listener.held==1) {
                if(backend.pendingReadOnlySharedBufferBytesForTest()!=1048576u ||
                   !backend.requestRendererCrashForTest())
                    throw std::runtime_error("Renderer crash command failed");
                stage=2;
            } else if(stage==2 && backend.rendererFailuresForTest()!=0) {
                nativeweb::Binary bytes(1024,17);
                if(backend.pendingReadOnlySharedBufferBytesForTest()!=0 ||
                   backend.cumulativeReadOnlySharedBufferBytesForTest()!=0 ||
                   backend.postReadOnlySharedBufferProbe(bytes,7))
                    throw std::runtime_error("Renderer grants survived crash");
                backend.reload();
                stage=3;
            } else if(stage==3 && listener.ready>=2) {
                nativeweb::Binary bytes(1024);
                for(std::size_t i=0;i<bytes.size();++i)
                    bytes[i]=static_cast<unsigned char>((i*37+17)&255);
                if(!backend.postReadOnlySharedBufferProbe(bytes,7))
                    throw std::runtime_error("Recovered renderer cannot send");
                stage=4;
            } else if(stage==4 && listener.held>=2) {
                if(listener.firstGrant==listener.secondGrant ||
                   backend.pendingReadOnlySharedBufferBytesForTest()!=1024u ||
                   backend.browserProcessFailuresForTest()!=0)
                    throw std::runtime_error("Recovered grants crossed generation");
                std::cout<<"xytron-sharedbuffer-renderer-crash:PASS"
                         <<" recovery_ready="<<listener.ready
                         <<" renderer_failures="<<backend.rendererFailuresForTest()
                         <<std::endl;
                result=0;
                break;
            }
            Sleep(5);
        }
        if(result && listener.failure.empty())
            listener.failure="Renderer recovery timed out in stage "+std::to_string(stage);
    } catch(const std::exception& e){listener.failure=e.what();}
    backend.destroy();
    DestroyWindow(window);
    if(!listener.failure.empty())
        std::cerr<<"Renderer probe failure: "<<listener.failure<<std::endl;
    std::cout<<"webview2-renderer-crash-e2e: "<<(result==0?"PASS":"FAIL")<<std::endl;
    return result;
}
