#include "browser/webview2/webview2_backend.hpp"
#include "core/bridge_message.hpp"
#include "nativeweb/app_resources.hpp"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
const char kEntry[]="app://desktop/sharedbuffer_soak.html";
const char kHtml[]=R"XYTRON_SOAK(<!doctype html><html><body><script>
(() => {
  chrome.webview.addEventListener('sharedbufferreceived',event=>{
    let buffer;
    try {
      const meta=event.additionalData;
      if(!meta||meta.type!=='xytron.shared.probe' ||
         typeof meta.id!=='number'||typeof meta.grant!=='string')
        throw Error('Malformed memory metadata');
      buffer=event.getBuffer();
      const bytes=new Uint8Array(buffer);
      if(bytes.length!==1048576 || meta.size!==bytes.length ||
         bytes[0]!==17 || bytes[bytes.length-1]!==((37*(bytes.length-1)+17)&255))
        throw Error('Corrupted shared buffer');
      chrome.webview.releaseBuffer(buffer);
      buffer=null;
      xytron.postMessage('soak.ack',{id:meta.id,grant:meta.grant});
    } catch(error) {
      if(buffer) try{chrome.webview.releaseBuffer(buffer);}catch(_){}
      xytron.postMessage('soak.failure',String(error));
    }
  });
  xytron.postMessage('soak.ready');
})();
</script></body></html>)XYTRON_SOAK";

std::size_t workingSetBytes(DWORD pid)
{
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,
                              FALSE,pid);
    if(!process)return 0;
    PROCESS_MEMORY_COUNTERS_EX pmc={};
    pmc.cb=sizeof(pmc);
    std::size_t bytes=0;
    if(GetProcessMemoryInfo(process,
       reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),sizeof(pmc)))
        bytes=pmc.WorkingSetSize;
    CloseHandle(process);
    return bytes;
}

class Listener : public nativeweb::detail::BrowserBackendListener
{
public:
    explicit Listener(nativeweb::detail::WebView2Backend& backend)
        : backend_(backend) {}
    int readyCount=0,ackCount=0;
    std::string failure;
    void onBrowserCreated() override {}
    void onBrowserClosed() override {}
    void onLoadStarted(const std::string&) override {}
    void onLoadFinished(const std::string&) override {}
    void onBridgeMessage(const Any&) override {failure="Unverified renderer event";}
    void onVerifiedBridgeMessage(
        const Any& message,const nativeweb::detail::VerifiedFrameContext& frame) override
    {
        try {
            if(!frame.mainFrame||!frame.trustedContent||!frame.engineVerified)
                throw std::runtime_error("Untrusted sender");
            const nativeweb::detail::BridgeMessage parsed=
                nativeweb::detail::parseBridgeMessage(message);
            if(parsed.type!=nativeweb::detail::BridgeMessageType::Event)
                return;
            if(parsed.eventName=="soak.ready") {++readyCount;return;}
            const VariantDict& env=AnyCast<const VariantDict&>(message);
            if(parsed.eventName=="soak.failure")
                throw std::runtime_error(AnyCast<std::string>(env.at("payload")));
            if(parsed.eventName!="soak.ack")
                throw std::runtime_error("Unknown renderer event");
            const VariantDict& payload=AnyCast<const VariantDict&>(
                env.at("payload"));
            const int id=AnyCast<int>(payload.at("id"));
            const std::string nonce=AnyCast<std::string>(payload.at("grant"));
            if(id!=ackCount%32 || !backend_.acknowledgeReadOnlySharedBufferProbe(id,nonce))
                throw std::runtime_error("Invalid or out-of-order release ACK");
            ++ackCount;
        } catch(const std::exception& e) {failure=e.what();}
    }
private:
    nativeweb::detail::WebView2Backend& backend_;
};

LRESULT CALLBACK wndProc(HWND w,UINT m,WPARAM p,LPARAM q)
{return DefWindowProcW(w,m,p,q);}
} // namespace

int main(int argc,char* argv[])
{
    int cycles=4;
    if(argc==3 && std::string(argv[1])=="--cycles")
        cycles=std::atoi(argv[2]);
    else if(argc!=1) {
        std::cerr<<"Usage: sharedbuffer_soak_probe [--cycles 1..500]\n";
        return 2;
    }
    if(cycles<1||cycles>500)return 2;
    WNDCLASSW cls={};
    cls.lpfnWndProc=wndProc;
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"Xytron.SharedBuffer.Soak";
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 3;
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"SharedBuffer soak",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,640,480,
        nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return 3;

    int result=1,complete=0,sent=0;
    nativeweb::detail::WebView2Backend backend;
    Listener listener(backend);
    backend.setListener(&listener);
    std::size_t hostStart=0,hostPeak=0,browserStart=0,browserPeak=0,browserEnd=0;
    std::size_t groupPrivateStart=0,groupPrivatePeak=0,groupPrivateEnd=0;
    std::size_t rendererPrivatePeak=0;
    unsigned int processPeak=0,rendererCountPeak=0,samples=0;
    try {
        std::shared_ptr<nativeweb::AppResourceBundle> resources(
            new nativeweb::AppResourceBundle());
        resources->add(kEntry,"text/html",kHtml);
        nativeweb::detail::BrowserCreateParams params;
        params.parent=reinterpret_cast<nativeweb::NativeWindowHandle>(window);
        params.source=kEntry;
        params.appResources=resources;
        backend.create(params);
        hostStart=hostPeak=workingSetBytes(GetCurrentProcessId());
        const DWORD browserPid=backend.browserProcessIdForTest();
        browserStart=browserPeak=workingSetBytes(browserPid);
        bool initialMemorySampleTaken=false;
        nativeweb::Binary bytes(1048576);
        for(std::size_t i=0;i<bytes.size();++i)
            bytes[i]=static_cast<unsigned char>((i*37+17)&255);
        const auto start=std::chrono::steady_clock::now();
        const int timeoutSeconds=std::max(80,cycles*4);
        while(std::chrono::steady_clock::now()-start<
              std::chrono::seconds(timeoutSeconds) && listener.failure.empty()) {
            MSG msg;
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if(!initialMemorySampleTaken && listener.readyCount>0) {
                // Wait for real app-document activation before asking for
                // renderer processes; Navigate() returns before JS executes.
                nativeweb::detail::WebView2Backend::ProcessMemorySnapshot first;
                if(!backend.processGroupMemoryForTest(&first) ||
                   first.rendererCount==0)
                    throw std::runtime_error("WebView2 group renderer snapshot unavailable");
                groupPrivateStart=groupPrivatePeak=first.totalPrivateBytes;
                rendererPrivatePeak=first.rendererPrivateBytes;
                processPeak=first.processCount;
                rendererCountPeak=first.rendererCount;
                ++samples;
                initialMemorySampleTaken=true;
            }
            if(initialMemorySampleTaken &&
               listener.readyCount>complete &&
               listener.ackCount==complete*32+sent && sent<32) {
                if(!backend.postReadOnlySharedBufferProbe(bytes,sent))
                    throw std::runtime_error("SharedBuffer failed inside document budget");
                ++sent;
            } else if(sent==32 && listener.ackCount==(complete+1)*32) {
                if(backend.pendingReadOnlySharedBufferBytesForTest()!=0u ||
                   backend.cumulativeReadOnlySharedBufferBytesForTest()!=33554432u ||
                   backend.postReadOnlySharedBufferProbe(bytes,32))
                    throw std::runtime_error("Per-document cap or grant leak");
                ++complete;
                hostPeak=std::max(hostPeak,workingSetBytes(GetCurrentProcessId()));
                browserEnd=workingSetBytes(browserPid);
                browserPeak=std::max(browserPeak,browserEnd);
                nativeweb::detail::WebView2Backend::ProcessMemorySnapshot snapshot;
                if(!backend.processGroupMemoryForTest(&snapshot) ||
                   snapshot.rendererCount==0)
                    throw std::runtime_error("WebView2 renderer memory sample missing");
                groupPrivateEnd=snapshot.totalPrivateBytes;
                groupPrivatePeak=std::max(groupPrivatePeak,groupPrivateEnd);
                rendererPrivatePeak=std::max(rendererPrivatePeak,
                                             snapshot.rendererPrivateBytes);
                processPeak=std::max(processPeak,snapshot.processCount);
                rendererCountPeak=std::max(rendererCountPeak,snapshot.rendererCount);
                ++samples;
                std::cout << "xytron-sharedbuffer-soak-sample:"
                          << " cycle=" << complete
                          << " private_bytes=" << snapshot.totalPrivateBytes
                          << " renderer_private_bytes=" << snapshot.rendererPrivateBytes
                          << " processes=" << snapshot.processCount
                          << " sampled_processes=" << snapshot.sampledCount
                          << " renderers=" << snapshot.rendererCount
                          << std::endl;
                if(complete==cycles) {
                    result=0;
                    break;
                }
                sent=0;
                backend.reload();
            }
            Sleep(2);
        }
        if(result && listener.failure.empty())
            listener.failure="Soak timed out at cycle "+std::to_string(complete);
    } catch(const std::exception& e) {listener.failure=e.what();}
    backend.destroy();
    DestroyWindow(window);
    if(!listener.failure.empty())
        std::cerr<<"SharedBuffer soak failure: "<<listener.failure<<std::endl;
    std::cout<<"xytron-sharedbuffer-soak:"
             <<(result==0?"PASS":"FAIL")
             <<" cycles="<<complete
             <<" logical_megabytes="<<complete*32
             <<" host_working_set_start_bytes="<<hostStart
             <<" host_working_set_peak_bytes="<<hostPeak
             <<" browser_main_working_set_start_bytes="<<browserStart
             <<" browser_main_working_set_peak_bytes="<<browserPeak
             <<" browser_main_working_set_end_bytes="<<browserEnd
             <<" process_group_private_start_bytes="<<groupPrivateStart
             <<" process_group_private_peak_bytes="<<groupPrivatePeak
             <<" process_group_private_end_bytes="<<groupPrivateEnd
             <<" renderer_private_peak_bytes="<<rendererPrivatePeak
             <<" process_group_peak_count="<<processPeak
             <<" renderer_peak_count="<<rendererCountPeak
             <<" memory_samples="<<samples
             <<std::endl;
    // Process-group PrivateUsage includes available WebView2 runtime
    // renderer/GPU/utility processes, but excludes crashpad and does not
    // account for file cache/shared mappings globally. Never used as a hard
    // leak threshold on variable shared GitHub-hosted runners.
    return result;
}
