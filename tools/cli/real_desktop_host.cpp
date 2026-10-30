// Minimal real-engine app:// desktop host for an explicitly trusted generated UI.
// This proves UI -> Native C++ bridge, NOT arbitrary user backend binding,
// native menus, plugin callbacks or redistributable desktop packaging.
#include "nativeweb/webview.hpp"
#include "nativeweb/app_resources.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#ifndef _WIN32
#include <unistd.h>
#include <limits.h>
#endif
#include <vector>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef XYTRON_APP_HOST_CEF
#include "browser/cef/cef_backend.hpp"
#include "browser/cef/cef_renderer_app.hpp"
#include "include/cef_app.h"
#ifdef _WIN32
#include <windows.h>
#endif
#else
#include "browser/webview2/webview2_backend.hpp"
#include <windows.h>
#endif

namespace {
bool smoke = false;
std::string problem;
bool verified = false;

std::string flag(int argc, char** argv, const std::string& prefix) {
  for (int i=1;i<argc;++i) {
    const std::string arg(argv[i]);
    if (arg.compare(0,prefix.size(),prefix)==0)
      return arg.substr(prefix.size());
  }
  return "";
}

// Resolve installed runtimes to real absolute executables BEFORE handing
// control to ProcessBroker. It intentionally rejects a PATH-only program name;
// the native managed-worker security contract must not be weakened.
std::string trustedRuntime(const char* name) {
#ifdef _WIN32
  char found[32768] = {};
  const DWORD count=SearchPathA(nullptr,name,nullptr,sizeof(found),found,nullptr);
  if(count==0 || count>=sizeof(found))
    throw std::runtime_error(std::string("Missing installed worker runtime: ")+name);
  return std::string(found);
#else
  const char* path=std::getenv("PATH");
  if(!path) throw std::runtime_error("Missing trusted runtime PATH");
  const std::string search(path);
  for(std::size_t from=0;from<=search.size();) {
    const std::size_t until=search.find(':',from);
    const std::string dir=search.substr(from,
      until==std::string::npos?std::string::npos:until-from);
    // Skip empty relative PATH segments to keep the subprocess executable
    // independent of its working directory.
    if(!dir.empty() && dir[0]=='/') {
      const std::string candidate=dir+"/"+name;
      if(access(candidate.c_str(),X_OK)==0) {
        char resolved[PATH_MAX]={};
        if(realpath(candidate.c_str(),resolved))
          return std::string(resolved);
      }
    }
    if(until==std::string::npos)break;
    from=until+1;
  }
  throw std::runtime_error(std::string("Missing installed worker runtime: ")+name);
#endif
}

// CLI-generated grants are an upper bound; ObjectHost also requires
// actual @export / [Export] on each callable and property.
std::vector<std::string> readManagedGrant(const std::string& path) {
  if(path.empty()) throw std::runtime_error("Missing managed exports grant");
  std::ifstream input(path.c_str(),std::ios::binary);
  if(!input)throw std::runtime_error("Cannot open managed exports grant");
  const std::regex allowed(
    R"(^app\.Main\.(create|dispose|(method|get|set)\.[A-Za-z][A-Za-z0-9_]*)$)");
  std::vector<std::string> operations;
  std::set<std::string> unique;
  std::string line;
  std::size_t bytes=0;
  while(std::getline(input,line)) {
    bytes+=line.size()+1;
    if(bytes>4096||line.size()>90 || operations.size()>=34 ||
       !std::regex_match(line,allowed) || !unique.insert(line).second)
      throw std::runtime_error("Unsafe or duplicate managed operation grant");
    operations.push_back(line);
  }
  if(!input.eof() && input.fail())
    throw std::runtime_error("Cannot read managed operation grant");
  if(operations.size()<3 || unique.count("app.Main.create")!=1 ||
     unique.count("app.Main.dispose")!=1)
    throw std::runtime_error("Incomplete exported Main operation grant");
  return operations;
}

std::shared_ptr<nativeweb::AppResourceBundle>
loadUi(const std::string& name, const std::string& lang) {
  if (name.empty()) throw std::runtime_error("Explicit --ui path is required");
  std::ifstream in(name.c_str(),std::ios::binary);
  if (!in) throw std::runtime_error("UI index not found");
  std::ostringstream bytes;
  char part[4096];
  std::size_t length=0;
  while (in.read(part,sizeof(part)) || in.gcount()) {
    const std::size_t count=static_cast<std::size_t>(in.gcount());
    length+=count;
    if (length>1024*1024) throw std::runtime_error("UI index exceeds 1 MiB");
    bytes.write(part,static_cast<std::streamsize>(count));
  }
  if (in.bad() || length==0) throw std::runtime_error("Invalid UI index");
  std::string html=bytes.str();
  if (html.find('\0')!=std::string::npos)
    throw std::runtime_error("UI HTML contains invalid NUL");
  if (smoke && lang=="cpp") {
    // Exercise real engine JS->native Promise. This probe does NOT claim
    // that the scaffold placeholder is the user's C++ backend.
    html+=R"HTML(
<script>
(async function () {
  try {
    const button=document.getElementById("xytron-ping");
    const output=document.getElementById("xytron-result");
    if (!button || !output) throw new Error("Generated UI button missing");
    button.click();
    for (let attempt=0; attempt<100 && output.textContent!=="pong-edited-in-project"; ++attempt)
      await new Promise(resolve=>setTimeout(resolve,50));
    if (output.textContent!=="pong-edited-in-project")
      throw new Error("User C++ backend did not return edited result: "+
                      String(output.textContent));
    const featureButton=document.getElementById("xytron-feature");
    const featureOutput=document.getElementById("xytron-feature-result");
    if (!featureButton || !featureOutput)
      throw new Error("Generated plugin UI button missing");
    featureButton.click();
    for (let attempt=0;attempt<100 && featureOutput.textContent!=="hello";++attempt)
      await new Promise(resolve=>setTimeout(resolve,50));
    if (featureOutput.textContent!=="hello")
      throw new Error("Native feature plugin method not invoked");
    // Plugin emits from its C ABI v1 callable; no JS-side proxy event.
    let count=0;
    let subscription=null;
    const received=new Promise((resolve,reject) => {
      const timer=setTimeout(()=>reject(new Error("Native plugin event timeout")),5000);
      subscription=xytron.onMessage("feature.changed",payload => {
        ++count;
        clearTimeout(timer);
        resolve(payload);
      });
    });
    if (await xytron.feature.fire()!=="published")
      throw new Error("Native feature publisher did not complete");
    if (await received!=="from-native-feature" || count!==1)
      throw new Error("Native feature event payload or delivery count mismatch");
    if (!xytron.offMessage(subscription))
      throw new Error("Native event subscription could not be removed");
    // A new plugin notification must not reach a removed JS listener.
    await xytron.feature.fire();
    await new Promise(resolve=>setTimeout(resolve,150));
    if (count!==1)
      throw new Error("Event fired after JS unsubscribed");
    if (await xytron.app.unloadFeature()!==true)
      throw new Error("Feature plugin unload refused");
    let rejected=false;
    try { await xytron.feature.hello(); }
    catch (_) { rejected=true; }
    if (!rejected)
      throw new Error("Unloaded plugin callable was still exposed to JS");
    await xytron.app.complete();
  } catch (error) {
    await xytron.app.fail(String(error));
  }
})();
</script>
)HTML";
  }
  if (smoke && (lang=="python" || lang=="csharp")) {
    html+=R"HTML(
<script>
(async function() {
  try {
    let ready=false;
    for (let i=0;i<200;i++) {
      if (await xytron.app.managedReady()) { ready=true; break; }
      await new Promise(r=>setTimeout(r,50));
    }
    if (!ready) throw new Error("Trusted managed worker was not registered");
    const button=document.getElementById("xytron-managed");
    const result=document.getElementById("xytron-managed-result");
    if (!button || !result) throw new Error("Generated managed UI is missing");
    button.click();
    for(let i=0;i<200 && result.textContent!=="pong-edited-in-project";i++)
      await new Promise(r=>setTimeout(r,50));
    if (result.textContent!=="pong-edited-in-project")
      throw new Error("Edited user managed Main did not respond: "+result.textContent);
    // All of these APIs were authored AFTER scaffold generation and were
    // discovered by the CLI build grant. No manually hardcoded native
    // allowlist is permitted here.
    const dynamic=await xytron.app.Main.create();
    try {
      if(await xytron.app.Main.method.add(dynamic,7,5)!==12)
        throw new Error("User Main.add numeric result mismatch");
      if(await xytron.app.Main.method.invert(dynamic,true)!==false)
        throw new Error("User Main.invert bool result mismatch");
      if(await xytron.app.Main.method.echo(dynamic,"hello-user")!=="hello-user")
        throw new Error("User Main.echo string result mismatch");
      if(await xytron.app.Main.method.nothing(dynamic)!==null)
        throw new Error("User Main.nothing null result mismatch");
      if(await xytron.app.Main.get.count(dynamic)!==0)
        throw new Error("Exported count property initial value mismatch");
      await xytron.app.Main.set.count(dynamic,33);
      if(await xytron.app.Main.get.count(dynamic)!==33)
        throw new Error("Exported count property setter failed");
      let typeDenied=false;
      try { await xytron.app.Main.method.add(dynamic,"seven",5); }
      catch(e) { typeDenied=true; }
      if(!typeDenied)throw new Error("Managed argument type validation missing");
      let arityDenied=false;
      try { await xytron.app.Main.method.add(dynamic,7); }
      catch(e) { arityDenied=true; }
      if(!arityDenied)throw new Error("Managed argument count validation missing");
    } finally {
      await xytron.app.Main.dispose(dynamic);
    }
    let denied=false;
    try { await xytron.app.Main.method.secret("no-handle"); }
    catch(e) { denied=true; }
    if(!denied)throw new Error("Unexported managed method reached worker");
    const handle=await xytron.app.Main.create();
    if(typeof handle!=="string" || !handle.startsWith("xy_"))
      throw new Error("Opaque managed handle missing");
    await xytron.app.Main.dispose(handle);
    denied=false;
    try { await xytron.app.Main.method.ping(handle); }
    catch(e) { denied=true; }
    if(!denied) throw new Error("Disposed managed handle remained accessible");
    await xytron.app.complete();
  } catch (error) {
    await xytron.app.fail(String(error));
  }
})();
</script>
)HTML";
  }
  std::shared_ptr<nativeweb::AppResourceBundle> bundle(
    new nativeweb::AppResourceBundle());
  bundle->add("app://desktop/index.html","text/html",html);
  return bundle;
}

class Listener : public nativeweb::WebViewListener {
public:
  Listener(nativeweb::WebView& webview,const std::string& lang,
           const std::string& pythonRunner,const std::string& pythonProject,
           const std::string& dotnetAssembly,
           const std::vector<std::string>& managedOps)
      : webview_(webview), language_(lang), pythonRunner_(pythonRunner),
        pythonProject_(pythonProject), dotnetAssembly_(dotnetAssembly),
        managedOps_(managedOps), ready_(false) {
    if(language_!="cpp")
      webview_.bind("app.managedReady",[this]() { return ready_.load(); });
    // app.ping now comes exclusively from user-authored backend/main.cpp
    // registered through ABI v1; never silently fall back to a host stub.
    if (smoke) {
      webview_.bind("app.complete",[this]() {
        verified=true;
        std::cout << "checkpoint: generated-app-js-native-pong" << std::endl;
        std::cout << "checkpoint: user-cpp-backend-and-feature-plugin" << std::endl;
        if(language_=="cpp") {
          std::cout << "checkpoint: plugin-native-js-event-unsubscribe" << std::endl;
        } else {
          std::cout << "checkpoint: user-managed-main-js-verified-" << language_ << std::endl;
          webview_.revokeManagedWorker("app");
        }
        webview_.destroy();
        return true;
      });
      webview_.bind("app.unloadFeature",[this]() {
        webview_.unloadPlugin("feature");
        std::cout << "checkpoint: feature-plugin-unloaded" << std::endl;
        return true;
      });
      webview_.bind("app.fail",[this](const std::string& msg) {
        problem=msg;
        webview_.destroy();
        return false;
      });
    }
  }
  void onCreated() override {
    std::cout << "checkpoint: native-app-window-created" << std::endl;
  }
  void onLoadFinished(const std::string& url) override {
    std::cout << "checkpoint: native-app-ui-loaded " << url << std::endl;
    if(language_=="cpp" || url!="app://desktop/index.html" || ready_.load()) return;
    try {
      if(managedOps_.empty())
        throw std::runtime_error("No trusted managed Main grant");
      if(language_=="python") {
        webview_.bindManagedWorker("app",managedOps_,
          {trustedRuntime(
#ifdef _WIN32
             "python.exe"
#else
             "python"
#endif
           ),pythonRunner_,pythonProject_},5000);
      } else if(language_=="csharp") {
        webview_.bindManagedWorker("app",managedOps_,
          {trustedRuntime(
#ifdef _WIN32
             "dotnet.exe"
#else
             "dotnet"
#endif
           ),dotnetAssembly_},5000);
      } else {
        throw std::runtime_error("Unsupported managed Main language");
      }
      ready_.store(true);
      std::cout << "checkpoint: managed-main-worker-granted-" << language_ << std::endl;
    } catch(const std::exception& error) {
      problem=error.what();
      webview_.destroy();
    }
  }
  void onClosed() override {
#ifdef XYTRON_APP_HOST_CEF
    CefQuitMessageLoop();
#else
    PostQuitMessage(0);
#endif
  }
private:
  nativeweb::WebView& webview_;
  std::string language_,pythonRunner_,pythonProject_,dotnetAssembly_;
  std::vector<std::string> managedOps_;
  std::atomic<bool> ready_;
};

#ifndef XYTRON_APP_HOST_CEF
LRESULT CALLBACK hostProc(HWND wnd,UINT message,WPARAM w,LPARAM p) {
  if (message==WM_CLOSE) {
    DestroyWindow(wnd);
    return 0;
  }
  if (message==WM_DESTROY) {
    PostQuitMessage(0); // Manual window close must terminate interactive run.
    return 0;
  }
  return DefWindowProcW(wnd,message,w,p);
}
HWND newWindow() {
  const wchar_t* className=L"Xytron.Public.StandaloneHost";
  WNDCLASSW wc={};
  wc.lpfnWndProc=&hostProc;
  wc.hInstance=GetModuleHandleW(nullptr);
  wc.lpszClassName=className;
  if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
    return nullptr;
  return CreateWindowExW(0,className,L"Xytron App",WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT,CW_USEDEFAULT,900,640,
                         nullptr,nullptr,wc.hInstance,nullptr);
}
#endif
} // namespace

int main(int argc,char** argv) {
  try {
#ifdef XYTRON_APP_HOST_CEF
#ifdef _WIN32
    CefMainArgs cefArgs(GetModuleHandleW(nullptr));
#else
    CefMainArgs cefArgs(argc,argv);
#endif
    CefRefPtr<nativeweb::detail::CefRendererApp> app(
        new nativeweb::detail::CefRendererApp());
    const int child=CefExecuteProcess(cefArgs,app,nullptr);
    if (child>=0) return child;
#endif
    for(int i=1;i<argc;++i) {
      if (std::string(argv[i])=="--smoke") smoke=true;
    }
    const std::string languageFlag=flag(argc,argv,"--backend-language=");
    const std::string language=languageFlag.empty()?"cpp":languageFlag;
    if(language!="cpp"&&language!="python"&&language!="csharp")
      throw std::runtime_error("Unsupported Main language");
    const std::shared_ptr<nativeweb::AppResourceBundle> assets=
        loadUi(flag(argc,argv,"--ui="),language);
    const std::string backendPlugin=flag(argc,argv,"--backend-plugin=");
    const std::string featurePlugin=flag(argc,argv,"--plugin-path=");
    const std::string pythonRunner=flag(argc,argv,"--python-runner=");
    const std::string pythonProject=flag(argc,argv,"--python-project=");
    const std::string dotnetAssembly=flag(argc,argv,"--dotnet-assembly=");
    const std::string managedGrant=flag(argc,argv,"--managed-operations=");
    const std::vector<std::string> managedOps=language=="cpp"?
      std::vector<std::string>():readManagedGrant(managedGrant);
    if(language=="cpp" && backendPlugin.empty())
      throw std::runtime_error("Explicit C++ backend ABI plugin is required");
    if(language=="python" && (pythonRunner.empty()||pythonProject.empty()))
      throw std::runtime_error("Missing trusted Python Main worker inputs");
    if(language=="csharp" && dotnetAssembly.empty())
      throw std::runtime_error("Missing trusted .NET Main assembly");

#ifdef XYTRON_APP_HOST_CEF
    CefSettings settings;
    settings.no_sandbox=true; // CI/runtime contract; secure sandbox is a release follow-up.
    settings.windowless_rendering_enabled=true;
    settings.log_severity=LOGSEVERITY_WARNING;
    nativeweb::detail::registerCefBackendFactory();
    nativeweb::WebView view;
    Listener listener(view,language,pythonRunner,pythonProject,dotnetAssembly,managedOps);
    view.setListener(&listener);
    if(language=="cpp" && view.loadPlugin(backendPlugin)!="app")
      throw std::runtime_error("C++ backend plugin ID mismatch");
    if (!featurePlugin.empty() && view.loadPlugin(featurePlugin)!="feature")
      throw std::runtime_error("Feature plugin ID mismatch");
    app->setBrowserReadyCallback([&view,assets]() {
      nativeweb::WebViewOptions options;
      options.engine=nativeweb::Engine::Cef;
      options.appResources=assets;
      view.create(0,"app://desktop/index.html",options);
    });
    if (!CefInitialize(cefArgs,settings,app,nullptr))
      throw std::runtime_error("CEF runtime initialization failed");
    CefRunMessageLoop();
    CefShutdown();
#else
    HWND window=newWindow();
    if (!window) throw std::runtime_error("Cannot create Win32 window");
    nativeweb::detail::registerWebView2BackendFactory();
    nativeweb::WebView view;
    Listener listener(view,language,pythonRunner,pythonProject,dotnetAssembly,managedOps);
    view.setListener(&listener);
    if(language=="cpp" && view.loadPlugin(backendPlugin)!="app")
      throw std::runtime_error("C++ backend plugin ID mismatch");
    if (!featurePlugin.empty() && view.loadPlugin(featurePlugin)!="feature")
      throw std::runtime_error("Feature plugin ID mismatch");
    nativeweb::WebViewOptions options;
    options.engine=nativeweb::Engine::WebView2;
    options.appResources=assets;
    view.create(reinterpret_cast<nativeweb::NativeWindowHandle>(window),
                "app://desktop/index.html",options);
    if (!smoke) ShowWindow(window,SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg,nullptr,0,0)>0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (IsWindow(window)) DestroyWindow(window);
#endif
    if (smoke && !verified) {
      std::cerr << "Generated-app bridge smoke incomplete: "<<problem<<std::endl;
      return 3;
    }
    std::cout << "xytron native desktop host: PASS" << std::endl;
    return 0;
  } catch(const std::exception& error) {
    std::cerr << "xytron native desktop host: "<<error.what()<<std::endl;
    return 2;
  }
}
