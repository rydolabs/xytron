// Minimal real-engine app:// desktop host for an explicitly trusted generated UI.
// This proves UI -> Native C++ bridge, NOT arbitrary user backend binding,
// complete cross-platform native menus or redistributable desktop packaging.
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
#include <windowsx.h>
#endif

namespace {
bool smoke = false;
std::string problem;
bool verified = false;
#ifndef XYTRON_APP_HOST_CEF
// UI-thread WM_COMMAND dispatch is the only source of the menu E2E counter.
// Posting a command from the JS-facing method uses the same path as an
// actual application-menu selection (without GUI automation or mouse races).
enum { XYTRON_MENU_ACTION=0x7401, XYTRON_MENU_EXIT=0x7402,
       XYTRON_MENU_CONTEXT_ACTION=0x7403, XYTRON_MENU_EXIT_TIMER=0x7404 };
HWND menuProbeWindow=nullptr;
std::atomic<unsigned> nativeMenuActions(0);
std::atomic<unsigned> nativeContextActions(0);
std::atomic<unsigned> nativeExitCommands(0);
HMENU makeNativeContextMenu();
HACCEL makeNativeAccelerators();
#endif

#ifndef XYTRON_APP_HOST_CEF
// For a *locally staged*, already-built Windows WebView2 C++ app, the real
// native host executable is itself the entry point. Explicit --ui remains
// mandatory in ordinary SDK/E2E host invocations without this marker.
std::string localPortableRoot() {
  char executable[MAX_PATH*4]={};
  const DWORD length=GetModuleFileNameA(nullptr,executable,sizeof(executable));
  if(!length || length>=sizeof(executable))
    throw std::runtime_error("Cannot resolve local app executable directory");
  const std::string full(executable,length);
  const std::size_t slash=full.find_last_of("/\\");
  if(slash==std::string::npos)
    throw std::runtime_error("Cannot find local app executable directory");
  const std::string root=full.substr(0,slash);
  std::ifstream in((root+"/xytron.portable").c_str(),std::ios::binary);
  if(!in)return std::string();
  std::string marker,extra;
  if(!std::getline(in,marker) ||
     marker!="xytron-webview2-cpp-local-v1" ||
     static_cast<bool>(std::getline(in,extra)))
    throw std::runtime_error("Invalid local WebView2 app marker");
  return root;
}
#endif

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
    // Only WebView2 has the Win32 app shell. Verify that a *real* window
    // WM_COMMAND travels through native WebView::emit to the JS subscriber.
    if (window.chrome && window.chrome.webview) {
      let menuEvents=0;
      let token=null;
      const menuReceived=new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(new Error("Native menu event timed out")),8000);
        token=xytron.onMessage("app.menu",payload=>{
          ++menuEvents;
          clearTimeout(timer);
          resolve(payload);
        });
      });
      if(await xytron.app.menuProbeTrigger()!==true)
        throw new Error("Unable to request native Win32 menu selection");
      if(await menuReceived!=="action" || menuEvents!==1)
        throw new Error("Real native menu event was not delivered to JS");
      if(await xytron.app.menuProbeCount()!==1)
        throw new Error("JS callback was not preceded by native WM_COMMAND");
      if(!xytron.offMessage(token))
        throw new Error("Native menu JS subscription removal failed");
      if(await xytron.app.menuProbeTrigger()!==true)
        throw new Error("Second native menu command not posted");
      for(let i=0;i<50 && await xytron.app.menuProbeCount()!==2;i++)
        await new Promise(resolve=>setTimeout(resolve,20));
      await new Promise(resolve=>setTimeout(resolve,200));
      if(menuEvents!==1 || await xytron.app.menuProbeCount()!==2)
        throw new Error("Unsubscribed JS callback still received native menu event");
      if(await xytron.app.menuProbeVerified()!==true)
        throw new Error("Native menu action verification failed");

      // A context action has a *distinct* Win32 ID and an independent JS
      // event subscription. The popup's actual item is checked by the host.
      let contextEvents=0;
      let contextToken=null;
      const contextReceived=new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(new Error("Native context event timed out")),8000);
        contextToken=xytron.onMessage("app.menu",payload=>{
          ++contextEvents;
          clearTimeout(timer);
          resolve(payload);
        });
      });
      if(await xytron.app.contextProbeTrigger()!==true)
        throw new Error("Unable to post native context action");
      if(await contextReceived!=="context-action" || contextEvents!==1)
        throw new Error("Native context action not delivered to JS");
      if(await xytron.app.contextProbeCount()!==1)
        throw new Error("Context JS event lacked a native WM_COMMAND");
      if(!xytron.offMessage(contextToken))
        throw new Error("Context event listener could not unsubscribe");
      if(await xytron.app.contextProbeTrigger()!==true)
        throw new Error("Cannot post second native context action");
      for(let i=0;i<50 && await xytron.app.contextProbeCount()!==2;i++)
        await new Promise(resolve=>setTimeout(resolve,20));
      await new Promise(resolve=>setTimeout(resolve,200));
      if(contextEvents!==1 || await xytron.app.contextProbeCount()!==2)
        throw new Error("Unsubscribed context JS listener still invoked");
      if(await xytron.app.contextProbeVerified()!==true)
        throw new Error("Native context action verification failed");

      // Exit is intentionally delivered before teardown; on a real user
      // click an OS timer enforces eventual close even without a subscriber.
      let exitToken=null;
      const exitReceived=new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(new Error("Native Exit event timed out")),8000);
        exitToken=xytron.onMessage("app.menu",payload=>{
          if(payload==="exit") {
            clearTimeout(timer);
            resolve(payload);
          }
        });
      });
      if(await xytron.app.exitProbeTrigger()!==true)
        throw new Error("Unable to post native Exit command");
      if(await exitReceived!=="exit")
        throw new Error("Native Exit event did not reach JS");
      if(!xytron.offMessage(exitToken))
        throw new Error("Exit JS listener could not unsubscribe");
      if(await xytron.app.exitProbeVerified()!==true)
        throw new Error("Native Exit command not confirmed");
    }
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
#ifndef XYTRON_APP_HOST_CEF
      // This call is made only AFTER JS subscribes. It posts a real Win32
      // command to the actual owner HWND, where hostProc invokes WebView::emit.
      webview_.bind("app.menuProbeTrigger",[]() {
        const HWND hwnd=menuProbeWindow;
        if(!hwnd || !IsWindow(hwnd))
          throw nativeweb::Error("menu_unavailable","Native window not available");
        HMENU bar=GetMenu(hwnd);
        HMENU menu=bar?GetSubMenu(bar,0):nullptr;
        if(!menu || GetMenuItemID(menu,0)!=XYTRON_MENU_ACTION)
          throw nativeweb::Error("menu_unavailable","Native App menu item missing");
        if(!PostMessageW(hwnd,WM_COMMAND,MAKEWPARAM(XYTRON_MENU_ACTION,0),0))
          throw nativeweb::Error("menu_unavailable","Cannot post native App menu command");
        return true;
      });
      webview_.bind("app.menuProbeCount",[]() {
        return static_cast<int>(nativeMenuActions.load());
      });
      webview_.bind("app.menuProbeVerified",[]() {
        if(nativeMenuActions.load()!=2)
          throw nativeweb::Error("menu_unverified","Native WM_COMMAND count mismatch");
        std::cout<<"checkpoint: win32-menu-js-subscriber-action-unsubscribe"<<std::endl;
        return true;
      });
      webview_.bind("app.contextProbeTrigger",[]() {
        const HWND hwnd=menuProbeWindow;
        if(!hwnd || !IsWindow(hwnd))
          throw nativeweb::Error("menu_unavailable","Native window not available");
        HMENU popup=makeNativeContextMenu();
        if(!popup)
          throw nativeweb::Error("menu_unavailable","Native context menu unavailable");
        const UINT id=GetMenuItemID(popup,0);
        DestroyMenu(popup);
        if(id!=XYTRON_MENU_CONTEXT_ACTION)
          throw nativeweb::Error("menu_unavailable","Native context menu item missing");
        if(!PostMessageW(hwnd,WM_COMMAND,MAKEWPARAM(id,0),0))
          throw nativeweb::Error("menu_unavailable","Cannot post native context action");
        return true;
      });
      webview_.bind("app.contextProbeCount",[]() {
        return static_cast<int>(nativeContextActions.load());
      });
      webview_.bind("app.contextProbeVerified",[]() {
        if(nativeContextActions.load()!=2)
          throw nativeweb::Error("menu_unverified","Context WM_COMMAND count mismatch");
        std::cout<<"checkpoint: win32-context-js-subscriber-action-unsubscribe"<<std::endl;
        return true;
      });
      webview_.bind("app.exitProbeTrigger",[]() {
        const HWND hwnd=menuProbeWindow;
        if(!hwnd || !IsWindow(hwnd))
          throw nativeweb::Error("menu_unavailable","Native window not available");
        HMENU bar=GetMenu(hwnd);
        HMENU app=bar?GetSubMenu(bar,0):nullptr;
        if(!app || GetMenuItemID(app,2)!=XYTRON_MENU_EXIT)
          throw nativeweb::Error("menu_unavailable","Native Exit menu item missing");
        if(!PostMessageW(hwnd,WM_COMMAND,MAKEWPARAM(XYTRON_MENU_EXIT,0),0))
          throw nativeweb::Error("menu_unavailable","Cannot post native Exit command");
        return true;
      });
      webview_.bind("app.exitProbeVerified",[]() {
        if(nativeExitCommands.load()!=1)
          throw nativeweb::Error("menu_unverified","Native Exit WM_COMMAND count mismatch");
        std::cout<<"checkpoint: win32-exit-js-subscriber"<<std::endl;
        return true;
      });
#endif
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
           ),dotnetAssembly_},12000);
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
// Real Win32 application/context menus; native action callbacks are sent to
// the app:// document through the existing native-to-JS message channel.
struct NativeMenuState {
  nativeweb::WebView* view;
  unsigned actions;
  unsigned exits;
  unsigned contextActions;
  NativeMenuState():view(nullptr),actions(0),exits(0),contextActions(0){}
};
// Always create the popup from the same function as the selftest/JS probe.
// This avoids accidentally proving a synthetic command for a missing item.
HMENU makeNativeContextMenu() {
  HMENU popup=CreatePopupMenu();
  if(popup && !AppendMenuW(popup,MF_STRING,
                           XYTRON_MENU_CONTEXT_ACTION,L"Action")) {
    DestroyMenu(popup);
    return nullptr;
  }
  return popup;
}
// Standard Win32 accelerator translation, handled by the standalone
// window's normal message loop. No new NativeWeb public ABI types.
HACCEL makeNativeAccelerators() {
  ACCEL shortcut={};
  shortcut.fVirt=FVIRTKEY|FCONTROL;
  shortcut.key='M';
  shortcut.cmd=XYTRON_MENU_ACTION;
  return CreateAcceleratorTableW(&shortcut,1);
}
LRESULT CALLBACK hostProc(HWND wnd,UINT message,WPARAM w,LPARAM p) {
  NativeMenuState* state=reinterpret_cast<NativeMenuState*>(
      GetWindowLongPtrW(wnd,GWLP_USERDATA));
  if(message==WM_COMMAND && HIWORD(w)==0) {
    const unsigned command=LOWORD(w);
    if(command==XYTRON_MENU_ACTION || command==XYTRON_MENU_EXIT ||
       command==XYTRON_MENU_CONTEXT_ACTION) {
      if(state) {
        if(command==XYTRON_MENU_ACTION) {
          ++state->actions;
          ++nativeMenuActions;
          if(smoke)std::cout<<"checkpoint: native-win32-menu-command-action"<<std::endl;
        } else if(command==XYTRON_MENU_CONTEXT_ACTION) {
          ++state->contextActions;
          ++nativeContextActions;
          if(smoke)std::cout<<"checkpoint: native-win32-context-command-action"<<std::endl;
        } else {
          ++state->exits;
          ++nativeExitCommands;
          if(smoke)std::cout<<"checkpoint: native-win32-exit-command"<<std::endl;
        }
      }
      const bool active=state && state->view && state->view->isCreated();
      if(active) {
        const std::string action=command==XYTRON_MENU_ACTION?"action":
          command==XYTRON_MENU_CONTEXT_ACTION?"context-action":"exit";
        state->view->emit("app.menu",action);
      }
      if(command==XYTRON_MENU_EXIT) {
        // Synchronous DestroyWindow would revoke the document before JS
        // could observe the native event. The timer guarantees an eventual
        // exit if the app never acknowledges or has no JS listener.
        if(!active || !SetTimer(wnd,XYTRON_MENU_EXIT_TIMER,
                                smoke?10000U:1000U,nullptr))
          DestroyWindow(wnd);
      }
      return 0;
    }
  }
  if(message==WM_TIMER && w==XYTRON_MENU_EXIT_TIMER) {
    KillTimer(wnd,XYTRON_MENU_EXIT_TIMER);
    DestroyWindow(wnd);
    return 0;
  }
  const HWND source=reinterpret_cast<HWND>(w);
  if(message==WM_CONTEXTMENU && (source==wnd || (source && IsChild(wnd,source)))) {
    HMENU popup=makeNativeContextMenu();
    if(!popup)return 0;
    POINT point={GET_X_LPARAM(p),GET_Y_LPARAM(p)};
    if(point.x==-1 && point.y==-1) {
      RECT rect={};GetWindowRect(wnd,&rect);
      point.x=(rect.left+rect.right)/2;point.y=(rect.top+rect.bottom)/2;
    }
    const UINT chosen=TrackPopupMenu(popup,
        TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,wnd,nullptr);
    DestroyMenu(popup);
    if(chosen)SendMessageW(wnd,WM_COMMAND,MAKEWPARAM(chosen,0),0);
    return 0;
  }
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
  HMENU bar=CreateMenu();
  HMENU appMenu=CreatePopupMenu();
  if(!bar || !appMenu) {
    if(appMenu)DestroyMenu(appMenu);
    if(bar)DestroyMenu(bar);
    return nullptr;
  }
  AppendMenuW(appMenu,MF_STRING,XYTRON_MENU_ACTION,L"Action\tCtrl+M");
  AppendMenuW(appMenu,MF_SEPARATOR,0,nullptr);
  AppendMenuW(appMenu,MF_STRING,XYTRON_MENU_EXIT,L"Exit");
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(appMenu),L"App");
  HWND wnd=CreateWindowExW(0,className,L"Xytron App",WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,CW_USEDEFAULT,900,640,
                          nullptr,bar,wc.hInstance,nullptr);
  if(!wnd)DestroyMenu(bar);
  return wnd;
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
#ifndef XYTRON_APP_HOST_CEF
    // Win32-only runner check: menu resource and WM_COMMAND dispatch,
    // without requiring WebView2 installation or a display screenshot.
    if(argc==2 && std::string(argv[1])=="--menu-selftest") {
      HWND testWindow=newWindow();
      if(!testWindow)throw std::runtime_error("Win32 menu selftest: window");
      HMENU bar=GetMenu(testWindow);
      if(!bar || GetMenuItemCount(bar)!=1 ||
         !GetSubMenu(bar,0) || GetMenuItemCount(GetSubMenu(bar,0))!=3)
        throw std::runtime_error("Win32 menu selftest: menu structure");
      NativeMenuState testState;
      SetWindowLongPtrW(testWindow,GWLP_USERDATA,
          reinterpret_cast<LONG_PTR>(&testState));
      SendMessageW(testWindow,WM_COMMAND,MAKEWPARAM(XYTRON_MENU_ACTION,0),0);
      if(testState.actions!=1 || testState.exits!=0 || !IsWindow(testWindow))
        throw std::runtime_error("Win32 menu selftest: action dispatch");
      HMENU contextPopup=makeNativeContextMenu();
      if(!contextPopup || GetMenuItemCount(contextPopup)!=1 ||
         GetMenuItemID(contextPopup,0)!=XYTRON_MENU_CONTEXT_ACTION)
        throw std::runtime_error("Win32 menu selftest: context popup layout");
      DestroyMenu(contextPopup);
      HACCEL shortcuts=makeNativeAccelerators();
      ACCEL installedShortcut={};
      const int count=shortcuts?CopyAcceleratorTableW(shortcuts,&installedShortcut,1):0;
      if(shortcuts)DestroyAcceleratorTable(shortcuts);
      if(count!=1 || installedShortcut.cmd!=XYTRON_MENU_ACTION ||
         installedShortcut.key!='M' ||
         installedShortcut.fVirt!=(FVIRTKEY|FCONTROL))
        throw std::runtime_error("Win32 menu selftest: Ctrl+M accelerator");
      SendMessageW(testWindow,WM_COMMAND,
                   MAKEWPARAM(XYTRON_MENU_CONTEXT_ACTION,0),0);
      if(testState.contextActions!=1 || !IsWindow(testWindow))
        throw std::runtime_error("Win32 menu selftest: context callback");
      SendMessageW(testWindow,WM_COMMAND,MAKEWPARAM(XYTRON_MENU_EXIT,0),0);
      if(testState.exits!=1 || IsWindow(testWindow))
        throw std::runtime_error("Win32 menu selftest: exit dispatch");
      std::cout<<"Win32 native application menu selftest: PASS"<<std::endl;
      return 0;
    }
#endif
    const std::string explicitUi=flag(argc,argv,"--ui=");
    std::string portableRoot;
#ifndef XYTRON_APP_HOST_CEF
    portableRoot=localPortableRoot();
#endif
    const std::string languageFlag=flag(argc,argv,"--backend-language=");
    if(!portableRoot.empty() &&
       (!explicitUi.empty() || !languageFlag.empty() ||
        !flag(argc,argv,"--backend-plugin=").empty() ||
        !flag(argc,argv,"--plugin-path=").empty()))
      throw std::runtime_error("Local app forbids overriding its packaged backend/plugins");
    const std::string language=portableRoot.empty()?
        (languageFlag.empty()?"cpp":languageFlag):"cpp";
    if(language!="cpp"&&language!="python"&&language!="csharp")
      throw std::runtime_error("Unsupported Main language");
    const std::string ui=portableRoot.empty()?explicitUi:
        portableRoot+"/ui/index.html";
    const std::shared_ptr<nativeweb::AppResourceBundle> assets=
        loadUi(ui,language);
    const std::string backendPlugin=portableRoot.empty()?
        flag(argc,argv,"--backend-plugin="):
        portableRoot+"/bin/xytron_app_backend.dll";
    std::string featurePlugin=flag(argc,argv,"--plugin-path=");
    if(!portableRoot.empty()) {
      std::ifstream feature((portableRoot+"/bin/feature.dll").c_str(),std::ios::binary);
      if(feature)featurePlugin=portableRoot+"/bin/feature.dll";
    }
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
    NativeMenuState menuState;
    menuProbeWindow=window;
    nativeMenuActions.store(0);
    nativeContextActions.store(0);
    nativeExitCommands.store(0);
    nativeweb::detail::registerWebView2BackendFactory();
    nativeweb::WebView view;
    menuState.view=&view;
    SetWindowLongPtrW(window,GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(&menuState));
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
    HACCEL shortcuts=makeNativeAccelerators();
    if(!shortcuts)throw std::runtime_error("Cannot create native accelerator table");
    MSG msg;
    while (GetMessageW(&msg,nullptr,0,0)>0) {
      if(!TranslateAcceleratorW(window,shortcuts,&msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    DestroyAcceleratorTable(shortcuts);
    if (IsWindow(window)) {
      SetWindowLongPtrW(window,GWLP_USERDATA,0);
      DestroyWindow(window);
    }
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
