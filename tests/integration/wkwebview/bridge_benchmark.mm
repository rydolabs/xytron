#import <AppKit/AppKit.h>
#include "browser/wkwebview/wkwebview_backend.hpp"
#include "nativeweb/webview.hpp"
#include "nativeweb/app_resources.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: nativeweb_wkwebview_benchmark /absolute/path.html\n";
        return 2;
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        std::ifstream htmlFile(argv[1],std::ios::binary);
        if (!htmlFile) return 3;
        std::ostringstream html;html<<htmlFile.rdbuf();
        nativeweb::detail::registerWkWebViewBackendFactory();
        nativeweb::WebView webview;
        std::atomic<bool> finished(false);
        std::atomic<bool> success(false);
        webview.bind("math.add",[](int a,int b){return a+b;});
        webview.bind("bench.echo",[](const nativeweb::Binary& bytes){return bytes;});
        webview.onMessage("bench.ping",[&](const Any& payload){
            webview.postMessage("bench.pong",payload);
        });
        webview.bind("bench.config",[](){
            VariantDict info;
            const char* w=std::getenv("XYTRON_BENCH_WARMUP");
            const char* s=std::getenv("XYTRON_BENCH_SAMPLES");
            info["warmup"]=w?std::atoi(w):20;
            info["samples"]=s?std::atoi(s):100;
            return info;
        });
        webview.bind("bench.finish",[&](const std::string& json){
            if(!json.empty() && json.size()<=1000000u) {
                std::cout<<"xytron-benchmark-json:"<<json<<std::endl;
                success.store(true);
            }
            finished.store(true);
            return true;
        });
        webview.bind("bench.abort",[&](const std::string& error){
            std::cerr<<"bridge-fail: "<<error<<std::endl;
            finished.store(true);
            return true;
        });
        std::shared_ptr<nativeweb::AppResourceBundle> resources(
            new nativeweb::AppResourceBundle());
        resources->add("app://desktop/bridge_benchmark.html","text/html",html.str());
        nativeweb::WebViewOptions options;
        options.engine=nativeweb::Engine::WkWebView;
        options.appResources=resources;
        webview.create(nullptr,"app://desktop/bridge_benchmark.html",options);
        const auto start=std::chrono::steady_clock::now();
        while(!finished.load() && std::chrono::steady_clock::now()-start<
              std::chrono::seconds(110)) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }
        const bool passed=finished.load() && success.load();
        webview.destroy();
        return passed ? 0 : 1;
    }
}
