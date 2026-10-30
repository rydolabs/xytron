#import <AppKit/AppKit.h>
#include "browser/wkwebview/wkwebview_backend.hpp"
#include "nativeweb/webview.hpp"
#include "nativeweb/app_resources.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <future>
#include "nativeweb/error.hpp"

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];

        nativeweb::detail::registerWkWebViewBackendFactory();
        nativeweb::WebView webview;
        std::atomic<int> checkpoints(0);
        std::atomic<bool> completed(false);
        std::atomic<bool> good(false);
        std::atomic<bool> jsReady(false);
        std::atomic<int> forbiddenEffects(0);
        webview.bind("math.add",[](int a,int b){return a+b;});
        webview.bind("test.sideEffect",[&](){
            forbiddenEffects.fetch_add(1);
            return true;
        });
        webview.bind("test.effects",[&](){
            return forbiddenEffects.load();
        });
        webview.bind("test.jsReady",[&](){
            jsReady.store(true);
            return true;
        });
        webview.bind("test.binaryEcho",[](const nativeweb::Binary& data){
            return data;
        });
        webview.bind("test.bytes",[](){
            nativeweb::Binary out;
            out.push_back(0);out.push_back(127);out.push_back(255);
            return out;
        });
        webview.onMessage("test.ping",[&](const Any& payload){
            const VariantDict& value=AnyCast<const VariantDict&>(payload);
            const int number=AnyCast<int>(value.find("number")->second);
            if(number==42) ++checkpoints;
            webview.postMessage("test.pong",nativeweb::Binary{0,127,255});
        });
        webview.bind("test.complete",[&](bool ok) {
            good.store(ok && checkpoints.load()==1);
            completed.store(true);
            return true;
        });

        std::shared_ptr<nativeweb::AppResourceBundle> resources(
            new nativeweb::AppResourceBundle());
        const std::string html=R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>Xytron macOS E2E</title></head>
<body><script>
(async () => {
  try {
    xytron.expose('ui.multiply',(a,b)=>a*b);
    xytron.expose('ui.fail',()=>{throw Error('expected JS rejection');});
    await xytron.test.jsReady();
    const count=await xytron.math.add(10,32);
    const b=await xytron.test.bytes();
    if(count!==42 || !(b instanceof ArrayBuffer) ||
       Array.from(new Uint8Array(b)).join(',')!=='0,127,255')
      throw Error('RPC or binary failed');
    const source=new Uint8Array([99,0,127,255,99]);
    const echo=await xytron.test.binaryEcho(source.subarray(1,4));
    if(!(echo instanceof ArrayBuffer) ||
       Array.from(new Uint8Array(echo)).join(',')!=='0,127,255')
      throw Error('TypedArray offset/roundtrip failed');
    await window.webkit.messageHandlers.xytron.postMessage(JSON.stringify({
      type:'request',id:'900001',method:'test.sideEffect',args:[],
      session:'000000000000000000000000000000000000000000000000'
    }));
    if(await xytron.test.effects()!==0)
      throw Error('Forged document-session request reached Core');
    const pong=new Promise((resolve,reject)=>{
      const id=xytron.onMessage('test.pong',value=>{
        xytron.offMessage(id);resolve(value);
      });
      setTimeout(()=>reject(Error('Event timeout')),6000);
    });
    xytron.postMessage('test.ping',{number:42});
    const received=await pong;
    if(!(received instanceof ArrayBuffer) ||
      Array.from(new Uint8Array(received)).join(',')!=='0,127,255')
      throw Error('native event Binary failed');
    await xytron.test.complete(true);
  } catch(error) {
    document.body.textContent=String(error);
    await xytron.test.complete(false);
  }
})();
</script></body></html>)HTML";
        resources->add("app://desktop/index.html","text/html",html);
        nativeweb::WebViewOptions options;
        options.engine=nativeweb::Engine::WkWebView;
        options.appResources=resources;
        webview.create(nullptr,"app://desktop/index.html",options);

        const auto start=std::chrono::steady_clock::now();
        const auto pump=[](){
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        };
        while(!jsReady.load() && !completed.load() &&
              std::chrono::steady_clock::now()-start<
              std::chrono::seconds(25)) pump();
        bool reversePassed=false;
        if(jsReady.load()) {
            std::future<int> multiply=webview.execute<int>("ui.multiply",6,7);
            std::future<Any> failure=webview.execute(
                "ui.fail",VariantList());
            while((multiply.wait_for(std::chrono::milliseconds(0))!=
                    std::future_status::ready ||
                   failure.wait_for(std::chrono::milliseconds(0))!=
                    std::future_status::ready) &&
                  std::chrono::steady_clock::now()-start<
                    std::chrono::seconds(25)) pump();
            try {
                const bool product=multiply.wait_for(
                    std::chrono::milliseconds(0))==std::future_status::ready &&
                    multiply.get()==42;
                bool rejected=false;
                if(failure.wait_for(std::chrono::milliseconds(0))==
                   std::future_status::ready) {
                    try { (void)failure.get(); }
                    catch(const nativeweb::Error& error) {
                        rejected=error.code()=="js_error";
                    }
                }
                reversePassed=product && rejected;
            } catch(const std::exception&) { reversePassed=false; }
        }
        while(!completed.load() && std::chrono::steady_clock::now()-start
            <std::chrono::seconds(25)) pump();
        const bool passed=completed.load() && good.load() &&
            reversePassed && forbiddenEffects.load()==0;
        webview.destroy();
        std::cout << "wkwebview-real-browser-e2e: "
                  << (passed?"PASS":"FAIL") << std::endl;
        return passed?0:1;
    }
}
