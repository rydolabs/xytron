#include "browser/webview2/webview2_backend.hpp"
#include "core/bridge_message.hpp"
#include "core/json_codec.hpp"
#include "nativeweb/app_resources.hpp"

#include <windows.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const char kEntry[]="app://desktop/sharedbuffer_probe.html";
const char kChildEntry[]="app://desktop/sharedbuffer_child.html";
const char kChildHtml[]=R"XYTRON_CHILD(<!doctype html><html><body>
<script>
window.addEventListener('message',event=>{
  try {
    // Parent deliberately supplies a VALID main-document session token.
    // The backend must reject by the engine-derived sender frame/url.
    chrome.webview.postMessage({
      type:'event',event:'probe.child.attack',payload:{id:999},
      session:String(event.data)
    });
    parent.postMessage('probe.child.sent','*');
  } catch(error) {
    parent.postMessage('probe.child.fail:'+String(error),'*');
  }
});
</script></body></html>)XYTRON_CHILD";

const char kHtml[]=R"XYTRON_HTML(<!doctype html><html><head>
<meta charset="utf-8"><title>WebView2 Base64 vs SharedBuffer</title>
</head><body><script>
(() => {
  'use strict';
  // Two paths have the same JS->Native control event and the same byte
  // pattern. Alternate their order to reduce first/second mover effects.
  const work=[];
  for(const [bytes,count] of [[1024,25],[65536,20],[1048576,8]]) {
    for(let i=0;i<count;++i) {
      if(i%2) work.push({transport:'shared',size:bytes},
                       {transport:'base64',size:bytes});
      else work.push({transport:'base64',size:bytes},
                     {transport:'shared',size:bytes});
    }
  }
  let current=null, nextIndex=0, startedAt=0, releases=0;
  let quotaSummary=null, quotaReceived=0, quotaRepeated=false;
  let intentionallyUnreleased=null;
  let oldSharedGrant=null, replayMode=false;
  let oldSession=null, stressActive=false, stressReceived=0;
  const requestStress=()=>xytron.postMessage('probe.stress.request',
    {id:600+stressReceived});
  xytron.onMessage('probe.stress.ready',()=>{
    stressActive=true;
    requestStress();
  });
  xytron.onMessage('probe.stress.denied',meta=>{
    if(meta.reason!=='document_budget' || stressReceived!==31)
      return fail('Unexpected stress budget outcome '+JSON.stringify(meta));
    xytron.postMessage('probe.stress.finish',{count:stressReceived});
  });
  const records=[];
  const maybeRepeatQuota=()=>{
    if(quotaSummary && quotaReceived===2 && !quotaRepeated) {
      quotaRepeated=true;
      xytron.postMessage('probe.quota.repeat');
    }
  };
  xytron.onMessage('probe.quota.summary', summary=>{
    if(summary.accepted!==2 || summary.denied!==3 ||
       summary.pending_bytes!==2097152)
      return fail('Unbounded SharedBuffer quota '+JSON.stringify(summary));
    quotaSummary=summary;
    maybeRepeatQuota();
  });
  xytron.onMessage('probe.quota.repeat.result',result=>{
    if(result.accepted!==false||result.pending_bytes!==2097152)
      return fail('Missing-release quota bypassed');
    xytron.postMessage('probe.reload',{
      intentionally_unreleased:intentionallyUnreleased ? 1 : 0,
      old_grant:oldSharedGrant,
      old_session:topToken
    });
  });
  const fail=(error)=>xytron.postMessage('probe.failure',String(error));
  xytron.onMessage('probe.replay.credentials',payload=>{
    replayMode=true;
    oldSession=payload.old_session;
    oldSharedGrant=payload.old_grant;
    xytron.postMessage('probe.replay.start');
  });
  const checkBytes=(bytes,size)=>{
    if(bytes.byteLength!==size) throw Error('Buffer length mismatch');
    for(let i=0;i<bytes.length;++i)
      if(bytes[i]!==((i*37+17)&255))
        throw Error('Buffer byte mismatch '+i);
  };
  const request=()=>{
    if(nextIndex===work.length) {
      xytron.postMessage('probe.quota.start',{samples:records,
        js_clock:'performance.now',release_count:releases});
      return;
    }
    current=work[nextIndex];
    const id=nextIndex++;
    startedAt=performance.now();
    xytron.postMessage(current.transport==='shared' ?
      'probe.shared.request' : 'probe.base64.request',{id,size:current.size});
  };
  const received=(transport, buffer)=>{
    if(!current||current.transport!==transport)
      throw Error('Unexpected probe path '+transport);
    // Capture RTT *before* validating the bytes. The same control event and
    // full-content validation are used for both paths, but excluded from RTT.
    const elapsed=performance.now()-startedAt;
    checkBytes(new Uint8Array(buffer),current.size);
    records.push({transport,size:current.size,elapsed_ms:elapsed});
  };
  xytron.onMessage('probe.base64.payload',value=>{
    try {
      if(!(value instanceof ArrayBuffer)) throw Error('Expected ArrayBuffer');
      received('base64',value);
      request();
    } catch(e) { fail(e); }
  });
  chrome.webview.addEventListener('sharedbufferreceived',e=>{
    let buffer;
    try {
      const meta=e.additionalData;
      if(!meta||meta.type!=='xytron.shared.probe')
        throw Error('Invalid shared buffer metadata');
      if(stressActive && meta.id>=600 && meta.id<700) {
        if(meta.size!==1048576 || meta.id!==600+stressReceived)
          throw Error('Stress SharedBuffer metadata mismatch');
        buffer=e.getBuffer();
        checkBytes(new Uint8Array(buffer),1048576);
        chrome.webview.releaseBuffer(buffer);
        buffer=null;
        xytron.postMessage('probe.release',{id:meta.id,grant:meta.grant});
        stressReceived++;
        requestStress();
        return;
      }
      if(replayMode && meta.id===200) {
        if(meta.size!==1024 || typeof meta.grant!=='string')
          throw Error('Invalid new document grant');
        buffer=e.getBuffer();
        checkBytes(new Uint8Array(buffer),1024);
        chrome.webview.releaseBuffer(buffer);
        buffer=null;
        // Inject an OLD document-session token with the NEW transfer grant;
        // the engine/backend must reject before this reaches Core.
        chrome.webview.postMessage({
          type:'event',event:'probe.release',
          payload:{id:200,grant:meta.grant},session:oldSession
        });
        // Even a valid current session cannot release a NEW allocation
        // using a grant recorded in the previous document.
        xytron.postMessage('probe.replay.check',{
          id:200,old_grant:oldSharedGrant,new_grant:meta.grant
        });
        return;
      }
      if(meta.id>=200 && meta.id<202) {
        if(meta.size!==1048576) throw Error('Quota buffer size mismatch');
        buffer=e.getBuffer();
        checkBytes(new Uint8Array(buffer),1048576);
        if(meta.id===200) {
          // Deliberately leak a live JS mapping until reload. A normal
          // document must not be able to send unbounded SharedBuffers.
          intentionallyUnreleased=buffer;
          oldSharedGrant=meta.grant;
          buffer=null;
        } else {
          chrome.webview.releaseBuffer(buffer);
          buffer=null;
        }
        // Deliberately omit cooperative ack in both cases.
        quotaReceived++;
        maybeRepeatQuota();
        return;
      }
      if(!current||current.transport!=='shared'||
         meta.id!==nextIndex-1||meta.size!==current.size)
        throw Error('Invalid shared buffer metadata');
      buffer=e.getBuffer();
      received('shared',buffer);
      chrome.webview.releaseBuffer(buffer);
      buffer=null;
      releases++;
      xytron.postMessage('probe.release',{id:meta.id,grant:meta.grant});
      request();
    } catch(e) {
      if(buffer) { try { chrome.webview.releaseBuffer(buffer); } catch(_) {} }
      fail(e);
    }
  });
  let topToken=null, childLoaded=false, childSent=false;
  const child=document.createElement('iframe');
  child.src='app://desktop/sharedbuffer_child.html';
  child.style.display='none';
  const sendForgedChild=()=>{
    if(topToken && childLoaded && !childSent) {
      childSent=true;
      child.contentWindow.postMessage(topToken,'*');
    }
  };
  chrome.webview.addEventListener('message',event=>{
    const m=event.data;
    if(m && m.type==='nativeweb_session' && typeof m.token==='string'){
      topToken=m.token;
      sendForgedChild();
    }
  });
  child.onload=()=>{childLoaded=true;sendForgedChild();};
  window.addEventListener('message',event=>{
    if(event.source!==child.contentWindow) return;
    if(event.data==='probe.child.sent') {
      // Child message must be denied before starting trusted main-frame work.
      setTimeout(()=>{ if(!replayMode) request(); },150);
    } else fail('Child iframe could not test sender identity: '+event.data);
  });
  document.body.appendChild(child);
})();
</script></body></html>)XYTRON_HTML";

class ProbeListener final : public nativeweb::detail::BrowserBackendListener
{
public:
    explicit ProbeListener(nativeweb::detail::WebView2Backend* backend)
        : backend_(backend),done_(false),passed_(false),
          reloadRequested_(false),navigationChecked_(false) {}

    bool done() const { return done_; }
    bool passed() const { return passed_; }
    void onBrowserCreated() override {}
    void onLoadStarted(const std::string&) override
    {
        if (!reloadRequested_) return;
        // Browser backend must revoke all outstanding grants at navigation
        // start and deny further SharedBuffer posting until activation.
        const std::size_t pending =
            backend_->pendingReadOnlySharedBufferBytesForTest();
        const nativeweb::Binary bytes(1024,static_cast<unsigned char>(17));
        const bool rejected =
            !backend_->postReadOnlySharedBufferProbe(bytes,500);
        navigationChecked_=pending==0 && rejected;
        if(!navigationChecked_) fail("Quota survived navigation start");
        else std::cout << "xytron-sharedbuffer-navigation:PASS"
                       << " pending_after_navigation=" << pending << std::endl;
    }
    void onLoadFinished(const std::string&) override
    {
        if(!reloadRequested_ || !navigationChecked_) return;
        VariantDict values;
        values["old_grant"]=oldGrant_;
        values["old_session"]=oldSession_;
        backend_->postBridgeMessage(nativeweb::detail::makeEventMessage(
            "probe.replay.credentials",Any(values)));
    }
    void onBrowserClosed() override {}
    void onBridgeMessage(const Any&) override
    {
        fail("Unverified renderer bridge message");
    }
    void onVerifiedBridgeMessage(
        const Any& message,
        const nativeweb::detail::VerifiedFrameContext& frame) override
    {
        if(!frame.mainFrame || !frame.trustedContent ||
           !frame.engineVerified || frame.navigation==0) {
            fail("Untrusted frame provenance");
            return;
        }
        try {
            const nativeweb::detail::BridgeMessage decoded=
                nativeweb::detail::parseBridgeMessage(message);
            if(decoded.type!=nativeweb::detail::BridgeMessageType::Event)
                return;
            const VariantDict& envelope=AnyCast<const VariantDict&>(message);
            const Any& body=envelope.at("payload");
            if(decoded.eventName=="probe.child.attack")
                throw std::runtime_error("Forged same-origin iframe reached verified Core");
            if(decoded.eventName=="probe.shared.request" ||
               decoded.eventName=="probe.base64.request") {
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                const int requestId=AnyCast<int>(payload.at("id"));
                const int size=AnyCast<int>(payload.at("size"));
                if(requestId<0 || requestId>=106 ||
                   (size!=1024 && size!=65536 && size!=1048576))
                    throw std::runtime_error("Invalid shared buffer request");
                nativeweb::Binary bytes(static_cast<std::size_t>(size));
                for(std::size_t i=0;i<bytes.size();++i)
                    bytes[i]=static_cast<unsigned char>((i*37+17)&255);
                if(decoded.eventName=="probe.shared.request") {
                    if(!backend_->postReadOnlySharedBufferProbe(bytes,requestId))
                        throw std::runtime_error("WebView2 SharedBuffer API unavailable");
                } else {
                    backend_->postBridgeMessage(
                        nativeweb::detail::makeEventMessage(
                            "probe.base64.payload",Any(bytes)));
                }
            } else if(decoded.eventName=="probe.release") {
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                if(!backend_->acknowledgeReadOnlySharedBufferProbe(
                    AnyCast<int>(payload.at("id")),
                    AnyCast<std::string>(payload.at("grant"))))
                    throw std::runtime_error("Invalid SharedBuffer release ack");
            } else if(decoded.eventName=="probe.quota.start") {
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                const VariantList& samples=AnyCast<const VariantList&>(
                    payload.at("samples"));
                if(samples.size()!=106u ||
                    AnyCast<int>(payload.at("release_count"))!=53 ||
                    backend_->pendingReadOnlySharedBufferBytesForTest()!=0u)
                    throw std::runtime_error("Baseline SharedBuffer leaked");
                std::cout << "xytron-sharedbuffer-probe-json:"
                          << nativeweb::detail::anyToJson(body) << std::endl;
                int accepted=0, denied=0;
                for(int id=200;id<205;++id) {
                    nativeweb::Binary bytes(1048576);
                    for(std::size_t i=0;i<bytes.size();++i)
                        bytes[i]=static_cast<unsigned char>((i*37+17)&255);
                    if(backend_->postReadOnlySharedBufferProbe(bytes,id))
                        ++accepted;
                    else ++denied;
                }
                VariantDict result;
                result["accepted"]=accepted;
                result["denied"]=denied;
                result["pending_bytes"]=static_cast<int>(
                    backend_->pendingReadOnlySharedBufferBytesForTest());
                quotaChecked_=accepted==2 && denied==3 &&
                    backend_->pendingReadOnlySharedBufferBytesForTest()==2097152u;
                backend_->postBridgeMessage(
                    nativeweb::detail::makeEventMessage("probe.quota.summary",Any(result)));
            } else if(decoded.eventName=="probe.quota.repeat") {
                if(!quotaChecked_) throw std::runtime_error("Quota never verified");
                const nativeweb::Binary bytes(1048576,static_cast<unsigned char>(17));
                const bool allowed=backend_->postReadOnlySharedBufferProbe(bytes,205);
                VariantDict result;
                result["accepted"]=allowed;
                result["pending_bytes"]=static_cast<int>(
                    backend_->pendingReadOnlySharedBufferBytesForTest());
                if(allowed) throw std::runtime_error("Release omission bypassed quota");
                backend_->postBridgeMessage(
                    nativeweb::detail::makeEventMessage(
                        "probe.quota.repeat.result",Any(result)));
            } else if(decoded.eventName=="probe.reload") {
                const VariantDict& state=AnyCast<const VariantDict&>(body);
                if(AnyCast<int>(state.at("intentionally_unreleased"))!=1)
                    throw std::runtime_error("Renderer did not retain a live SharedBuffer");
                oldGrant_=AnyCast<std::string>(state.at("old_grant"));
                oldSession_=AnyCast<std::string>(state.at("old_session"));
                if(oldGrant_.size()!=48 || oldSession_.size()!=48)
                    throw std::runtime_error("Old document credentials unavailable");
                if(!quotaChecked_ ||
                    backend_->pendingReadOnlySharedBufferBytesForTest()!=2097152u)
                    throw std::runtime_error("Expected leaked quota before reload");
                reloadRequested_=true;
                backend_->reload();
            } else if(decoded.eventName=="probe.replay.start") {
                if(!reloadRequested_ || !navigationChecked_ ||
                   backend_->pendingReadOnlySharedBufferBytesForTest()!=0u)
                    throw std::runtime_error("Replay started without clean new document");
                nativeweb::Binary bytes(1024);
                for(std::size_t i=0;i<bytes.size();++i)
                    bytes[i]=static_cast<unsigned char>((i*37+17)&255);
                if(!backend_->postReadOnlySharedBufferProbe(bytes,200))
                    throw std::runtime_error("Could not reuse request id in new document");
            } else if(decoded.eventName=="probe.replay.check") {
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                const int id=AnyCast<int>(payload.at("id"));
                const std::string stale=AnyCast<std::string>(payload.at("old_grant"));
                const std::string fresh=AnyCast<std::string>(payload.at("new_grant"));
                if(id!=200 || stale!=oldGrant_ || stale==fresh ||
                   backend_->pendingReadOnlySharedBufferBytesForTest()!=1024u)
                    throw std::runtime_error("Old session token/reused ID escaped isolation");
                if(backend_->acknowledgeReadOnlySharedBufferProbe(id,stale) ||
                   backend_->pendingReadOnlySharedBufferBytesForTest()!=1024u)
                    throw std::runtime_error("Old allocation grant released NEW allocation");
                if(!backend_->acknowledgeReadOnlySharedBufferProbe(id,fresh) ||
                   backend_->pendingReadOnlySharedBufferBytesForTest()!=0u)
                    throw std::runtime_error("Current allocation grant did not release");
                if(backend_->acknowledgeReadOnlySharedBufferProbe(id,fresh))
                    throw std::runtime_error("Duplicate release ACK accepted");
                replayChecked_=true;
                std::cout << "xytron-sharedbuffer-replay:PASS"
                          << " stale_session_and_grant_rejected=1" << std::endl;
                backend_->postBridgeMessage(
                    nativeweb::detail::makeEventMessage("probe.stress.ready",Any()));
            } else if(decoded.eventName=="probe.stress.request") {
                if(!replayChecked_) throw std::runtime_error("Replay not verified");
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                const int id=AnyCast<int>(payload.at("id"));
                if(id<600 || id>631)
                    throw std::runtime_error("Invalid stress request");
                nativeweb::Binary bytes(1048576);
                for(std::size_t i=0;i<bytes.size();++i)
                    bytes[i]=static_cast<unsigned char>((i*37+17)&255);
                const bool posted=backend_->postReadOnlySharedBufferProbe(bytes,id);
                if(!posted) {
                    if(id!=631 || backend_->pendingReadOnlySharedBufferBytesForTest()!=0 ||
                       backend_->cumulativeReadOnlySharedBufferBytesForTest()!=
                         31u*1024u*1024u+1024u)
                        throw std::runtime_error("Unexpected SharedBuffer cap failure");
                    VariantDict denial;
                    denial["reason"]=std::string("document_budget");
                    backend_->postBridgeMessage(
                        nativeweb::detail::makeEventMessage("probe.stress.denied",
                                                            Any(denial)));
                }
            } else if(decoded.eventName=="probe.stress.finish") {
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                if(AnyCast<int>(payload.at("count"))!=31 ||
                   backend_->pendingReadOnlySharedBufferBytesForTest()!=0 ||
                   backend_->cumulativeReadOnlySharedBufferBytesForTest()!=
                     31u*1024u*1024u+1024u)
                    throw std::runtime_error("Document lifetime budget not enforced");
                done_=true;
                passed_=quotaChecked_ && navigationChecked_ && replayChecked_;
                std::cout << "xytron-sharedbuffer-budget:"
                          << (passed_?"PASS":"FAIL")
                          << " total_bytes=32506880" << std::endl;
            } else if(decoded.eventName=="probe.complete") {
                const VariantDict& payload=AnyCast<const VariantDict&>(body);
                const VariantList& samples=AnyCast<const VariantList&>(
                    payload.at("samples"));
                if(samples.size()!=106u ||
                   AnyCast<int>(payload.at("release_count"))!=53)
                    throw std::runtime_error("Incomplete shared buffer run");
                std::cout << "xytron-sharedbuffer-probe-json:"
                          << nativeweb::detail::anyToJson(body)
                          << std::endl;
                done_=true;passed_=true;
            } else if(decoded.eventName=="probe.failure") {
                fail(AnyCast<std::string>(body));
            } else {
                fail("Unknown probe event");
            }
        } catch(const std::exception& e) {
            fail(e.what());
        }
    }
private:
    void fail(const std::string& why)
    {
        std::cerr << "SharedBuffer probe failure: " << why << std::endl;
        passed_=false;done_=true;
    }
    nativeweb::detail::WebView2Backend* backend_;
    bool done_,passed_;
    bool reloadRequested_,navigationChecked_,quotaChecked_=false;
    bool replayChecked_=false;
    std::string oldGrant_,oldSession_;
};

LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace

int main()
{
    const wchar_t* name=L"Xytron.SharedBuffer.Probe";
    WNDCLASSW cls={};
    cls.lpfnWndProc=&windowProc;
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=name;
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
        return 2;
    HWND window=CreateWindowExW(0,name,L"WebView2 SharedBuffer E2E",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
        800,600,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window) return 3;

    int result=1;
    nativeweb::detail::WebView2Backend backend;
    ProbeListener listener(&backend);
    backend.setListener(&listener);
    try {
        std::shared_ptr<nativeweb::AppResourceBundle> assets(
            new nativeweb::AppResourceBundle());
        assets->add(kEntry,"text/html",kHtml);
        assets->add(kChildEntry,"text/html",kChildHtml);
        nativeweb::detail::BrowserCreateParams params;
        params.parent=reinterpret_cast<nativeweb::NativeWindowHandle>(window);
        params.source=kEntry;
        params.appResources=assets;
        backend.create(params);
        const auto started=std::chrono::steady_clock::now();
        MSG message;
        while(!listener.done() &&
              std::chrono::steady_clock::now()-started<
                std::chrono::seconds(80)) {
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(5);
        }
        result=listener.done() && listener.passed()?0:1;
    } catch(const std::exception& e) {
        std::cerr << "SharedBuffer probe exception: "<<e.what()<<std::endl;
    }
    backend.destroy();
    DestroyWindow(window);
    std::cout<<"webview2-sharedbuffer-real-e2e: "
             <<(result==0?"PASS":"FAIL")<<std::endl;
    return result;
}
