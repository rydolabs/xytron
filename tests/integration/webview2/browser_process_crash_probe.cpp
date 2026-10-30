#include "browser/webview2/webview2_backend.hpp"
#include "core/bridge_message.hpp"
#include "nativeweb/app_resources.hpp"

#include <windows.h>
#include <chrono>
#include <cwchar>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

const char kEntry[]="app://desktop/browser_process_crash.html";
const char kHtml[]=R"XYTRON_BCRASH(<!doctype html><html><body><script>
(() => {
  'use strict';
  const retained=[];
  chrome.webview.addEventListener('sharedbufferreceived', event => {
    try {
      const meta=event.additionalData;
      if(!meta || meta.type!=='xytron.shared.probe' || meta.id!==7 ||
         (meta.size!==1048576 && meta.size!==1024))
        throw Error('Invalid SharedBuffer metadata');
      const buffer=event.getBuffer();
      const bytes=new Uint8Array(buffer);
      if(bytes.length!==meta.size || bytes[0]!==17 ||
         bytes[bytes.length-1]!==((37*(bytes.length-1)+17)&255))
        throw Error('Corrupted SharedBuffer bytes');
      if(meta.size===1048576) retained.push(buffer);
      else chrome.webview.releaseBuffer(buffer);
      xytron.postMessage('bcrash.held',{size:meta.size,grant:meta.grant});
    } catch(error) {
      xytron.postMessage('bcrash.failure',String(error));
    }
  });
  xytron.postMessage('bcrash.ready');
})();
</script></body></html>)XYTRON_BCRASH";

struct Listener final : nativeweb::detail::BrowserBackendListener
{
    int ready=0,held=0;
    std::string oldGrant,newGrant,failed;
    void onBrowserCreated() override {}
    void onBrowserClosed() override {}
    void onLoadStarted(const std::string&) override {}
    void onLoadFinished(const std::string&) override {}
    void onBridgeMessage(const Any&) override {
        failed="Unverified bridge event";
    }
    void onVerifiedBridgeMessage(
        const Any& message,const nativeweb::detail::VerifiedFrameContext& frame) override
    {
        try {
            if(!frame.mainFrame || !frame.trustedContent || !frame.engineVerified)
                throw std::runtime_error("Unverified sender");
            const nativeweb::detail::BridgeMessage parsed=
                nativeweb::detail::parseBridgeMessage(message);
            if(parsed.type!=nativeweb::detail::BridgeMessageType::Event) return;
            if(parsed.eventName=="bcrash.ready") { ++ready; return; }
            const VariantDict& envelope=AnyCast<const VariantDict&>(message);
            if(parsed.eventName=="bcrash.failure")
                throw std::runtime_error(AnyCast<std::string>(envelope.at("payload")));
            if(parsed.eventName!="bcrash.held")
                throw std::runtime_error("Unknown bridge event");
            const VariantDict& payload=AnyCast<const VariantDict&>(
                envelope.at("payload"));
            const int size=AnyCast<int>(payload.at("size"));
            const std::string grant=AnyCast<std::string>(payload.at("grant"));
            if(grant.size()!=48 || held>=2 ||
               (held==0 && size!=1048576) || (held==1 && size!=1024))
                throw std::runtime_error("Invalid response or sequence");
            if(held==0) oldGrant=grant;
            else newGrant=grant;
            ++held;
        } catch(const std::exception& error) {failed=error.what();}
    }
};

LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM w,LPARAM lp)
{
    return DefWindowProcW(hwnd,msg,w,lp);
}

// Each crash test has its own Windows temporary User Data Folder; NEVER
// terminate the default WebView2 runtime process of an unrelated client.
std::wstring isolatedFolder()
{
    wchar_t root[MAX_PATH]={};
    if(!GetTempPathW(MAX_PATH,root))
        throw std::runtime_error("GetTempPathW failed");
    wchar_t filename[MAX_PATH]={};
    if(!GetTempFileNameW(root,L"xyw",0,filename))
        throw std::runtime_error("GetTempFileNameW failed");
    if(!DeleteFileW(filename) || !CreateDirectoryW(filename,nullptr))
        throw std::runtime_error("Could not reserve isolated WebView2 folder");
    return std::wstring(filename);
}

void killOnlyIsolatedBrowser(DWORD pid)
{
    if(!pid || pid==GetCurrentProcessId())
        throw std::runtime_error("Invalid WebView2 browser PID");
    HANDLE process=OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION |
        SYNCHRONIZE,FALSE,pid);
    if(!process) throw std::runtime_error("Cannot open isolated browser");
    wchar_t name[4096]={};
    DWORD length=sizeof(name)/sizeof(wchar_t);
    const BOOL named=QueryFullProcessImageNameW(process,0,name,&length);
    const wchar_t* file=named?std::wcsrchr(name,L'\\'):nullptr;
    const wchar_t* base=file?file+1:name;
    if(!named || _wcsicmp(base,L"msedgewebview2.exe")!=0) {
        CloseHandle(process);
        throw std::runtime_error("Refused to kill a non-WebView2 process");
    }
    const BOOL killed=TerminateProcess(process,123);
    const DWORD finished=killed?WaitForSingleObject(process,15000):WAIT_FAILED;
    CloseHandle(process);
    if(!killed || finished!=WAIT_OBJECT_0)
        throw std::runtime_error("Isolated browser process did not exit");
}

void pump()
{
    MSG message;
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Sleep(5);
}

nativeweb::Binary pattern(std::size_t count)
{
    nativeweb::Binary bytes(count);
    for(std::size_t i=0;i<count;++i)
        bytes[i]=static_cast<unsigned char>((i*37+17)&255);
    return bytes;
}

} // namespace

int main()
{
    WNDCLASSW cls={};
    cls.lpfnWndProc=&wndProc;
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"Xytron.Isolated.BrowserCrash";
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
        return 2;
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"BrowserProcessCrash",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,760,550,
        nullptr,nullptr,cls.hInstance,nullptr);
    if(!window) return 3;

    int result=1,stage=0;
    Listener listener;
    std::unique_ptr<nativeweb::detail::WebView2Backend> backend;
    try {
        std::shared_ptr<nativeweb::AppResourceBundle> resources(
            new nativeweb::AppResourceBundle());
        resources->add(kEntry,"text/html",kHtml);
        nativeweb::detail::BrowserCreateParams params;
        params.parent=reinterpret_cast<nativeweb::NativeWindowHandle>(window);
        params.source=kEntry;
        params.appResources=resources;
        // Separate profiles ensure the hard kill never affects unrelated
        // WebViews, and new environment creation cannot attach to a dying UDF.
        const std::wstring firstFolder=isolatedFolder();
        const std::wstring secondFolder=isolatedFolder();
        backend.reset(new nativeweb::detail::WebView2Backend());
        backend->setListener(&listener);
        backend->setIsolatedUserDataFolderForTest(firstFolder);
        backend->create(params);

        const auto started=std::chrono::steady_clock::now();
        while(std::chrono::steady_clock::now()-started<std::chrono::seconds(90)
              && listener.failed.empty()) {
            pump();
            if(stage==0 && listener.ready==1) {
                if(!backend->postReadOnlySharedBufferProbe(pattern(1048576),7))
                    throw std::runtime_error("First grant failed");
                stage=1;
            } else if(stage==1 && listener.held==1) {
                const DWORD originalPid=backend->browserProcessIdForTest();
                if(backend->pendingReadOnlySharedBufferBytesForTest()!=1048576u)
                    throw std::runtime_error("Old grant not pending");
                killOnlyIsolatedBrowser(originalPid);
                stage=2;
            } else if(stage==2 && backend->browserProcessFailuresForTest()>0) {
                if(backend->pendingReadOnlySharedBufferBytesForTest()!=0 ||
                   backend->cumulativeReadOnlySharedBufferBytesForTest()!=0 ||
                   backend->postReadOnlySharedBufferProbe(pattern(1024),7))
                    throw std::runtime_error("Dead browser accepted a stale grant");
                backend->destroy();
                backend.reset();
                backend.reset(new nativeweb::detail::WebView2Backend());
                backend->setListener(&listener);
                backend->setIsolatedUserDataFolderForTest(secondFolder);
                backend->create(params);
                stage=3;
            } else if(stage==3 && listener.ready>=2) {
                if(!backend->postReadOnlySharedBufferProbe(pattern(1024),7))
                    throw std::runtime_error("New browser cannot send");
                stage=4;
            } else if(stage==4 && listener.held>=2) {
                if(listener.oldGrant==listener.newGrant ||
                   backend->browserProcessFailuresForTest()!=0 ||
                   backend->pendingReadOnlySharedBufferBytesForTest()!=1024 ||
                   backend->acknowledgeReadOnlySharedBufferProbe(7,listener.oldGrant) ||
                   !backend->acknowledgeReadOnlySharedBufferProbe(7,listener.newGrant) ||
                   backend->pendingReadOnlySharedBufferBytesForTest()!=0)
                    throw std::runtime_error("New browser inherited old grants");
                std::cout<<"xytron-webview2-browser-process-recreate:PASS"
                         <<" process_failure_detected=1"
                         <<" isolated_profiles=2"
                         <<" old_grant_denied=1"
                         <<std::endl;
                result=0;
                break;
            }
        }
        if(result && listener.failed.empty())
            listener.failed="Timed out in browser crash stage "+std::to_string(stage);
    } catch(const std::exception& error) {
        listener.failed=error.what();
    }
    if(backend) backend->destroy();
    backend.reset();
    DestroyWindow(window);
    if(!listener.failed.empty())
        std::cerr<<"Browser process crash probe failure: "<<listener.failed<<std::endl;
    std::cout<<"webview2-browser-process-crash-e2e: "
             <<(result==0?"PASS":"FAIL")<<std::endl;
    return result;
}
