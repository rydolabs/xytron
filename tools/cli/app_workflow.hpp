#ifndef XYTRON_CLI_APP_WORKFLOW_HPP
#define XYTRON_CLI_APP_WORKFLOW_HPP
// Offline, explicit local Core-SDK restore and backend-only build/test.
// No browser-engine download or standalone app publication is implied.
#include "project_manifest.hpp"
#include "managed_export_discovery.hpp"
#include "offline_file_safety.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif
namespace xytron_cli {
inline bool appExists(const std::string& path) {
  struct stat st; return stat(path.c_str(),&st)==0;
}
inline void appDir(const std::string& path) {
  struct stat st;
  if(stat(path.c_str(),&st)==0) {
#ifdef _WIN32
    if(!(st.st_mode & _S_IFDIR)) throw std::runtime_error("Not a directory: "+path);
#else
    if(!S_ISDIR(st.st_mode))throw std::runtime_error("Not a directory: "+path);
#endif
    projectPath(path,true);
    return;
  }
#ifdef _WIN32
  if(_mkdir(path.c_str())!=0)
#else
  if(mkdir(path.c_str(),0755)!=0)
#endif
    throw std::runtime_error("Cannot create "+path);
}
inline bool sdkPathChars(const std::string& path) {
  if(path.empty()||path.size()>700)return false;
  for(std::size_t i=0;i<path.size();++i) {
    const char c=path[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
         (c>='0'&&c<='9')||c=='_'||c=='-'||c=='/'||
         c=='\\'||c=='.'||c==':'||c==' '||c=='~'))return false;
  }
  return true;
}
inline std::string normalizeSdk(const std::string& input) {
  if(!sdkPathChars(input))throw std::runtime_error("Unsafe SDK prefix");
#ifdef _WIN32
  char path[MAX_PATH*3];
  if(!_fullpath(path,input.c_str(),sizeof(path)))
    throw std::runtime_error("Cannot resolve SDK prefix");
#else
  char path[PATH_MAX];
  if(!realpath(input.c_str(),path))
    throw std::runtime_error("Cannot resolve SDK prefix");
#endif
  const std::string full(path);
  if(!sdkPathChars(full))throw std::runtime_error("Unsafe resolved SDK prefix");
  return full;
}
inline std::string sdkConfig(const std::string& prefix) {
  const std::string name="/lib/cmake/NativeWeb/NativeWebConfig.cmake";
  const std::string alt="/lib64/cmake/NativeWeb/NativeWebConfig.cmake";
  const std::string header=prefix+"/include/nativeweb/webview.hpp";
  if(!appExists(header))throw std::runtime_error("Core SDK header missing: "+header);
  if(appExists(prefix+name))return prefix+name;
  if(appExists(prefix+alt))return prefix+alt;
  throw std::runtime_error("Installed NativeWeb CMake config missing: "+prefix);
}
// FNV-1a is a stable change detector, NOT a cryptographic distribution hash.
inline std::string sdkFingerprint(const std::string& path) {
  std::ifstream in(path.c_str(),std::ios::binary);
  if(!in)throw std::runtime_error("Cannot open SDK config");
  unsigned long long value=14695981039346656037ULL;
  char data[4096];std::size_t bytes=0;
  while(in.read(data,sizeof(data))||in.gcount()) {
    const std::size_t n=static_cast<std::size_t>(in.gcount());
    bytes+=n;
    if(bytes>1024*1024)throw std::runtime_error("SDK config too large");
    for(std::size_t i=0;i<n;++i) {
      value^=static_cast<unsigned char>(data[i]);
      value*=1099511628211ULL;
    }
  }
  if(in.bad())throw std::runtime_error("SDK config read failed");
  std::ostringstream out;
  out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;
  return out.str();
}
inline std::string appMainLanguage() {
  validateProjectManifest();
  const JsonObjectFields fields(readProjectFile("xytron.json"));
  return fields.object("main").string("language");
}
inline std::string lockPayload(const std::string& prefix) {
  return "xytron-core-lock-v1\n"+prefix+"\n"+
    sdkFingerprint(sdkConfig(prefix))+"\n";
}
inline std::string appReadAll(const std::string& path) {
  projectPath(path,false);
  std::ifstream in(path.c_str(),std::ios::binary);
  if(!in)throw std::runtime_error("Missing "+path+"; run xytron restore --sdk PREFIX");
  std::ostringstream out; out<<in.rdbuf();
  if(in.bad()||out.str().size()>4096)throw std::runtime_error("Invalid SDK lock");
  return out.str();
}
inline std::string restoredSdk() {
  const std::string lock=appReadAll(".xytron/core.lock");
  const std::string pre="xytron-core-lock-v1\n";
  if(lock.compare(0,pre.size(),pre)!=0)throw std::runtime_error("Invalid lock format");
  const std::size_t sep=lock.find('\n',pre.size());
  if(sep==std::string::npos)throw std::runtime_error("Invalid lock path");
  const std::string path=lock.substr(pre.size(),sep-pre.size());
  if(!sdkPathChars(path)||normalizeSdk(path)!=path)
    throw std::runtime_error("Invalid restored SDK path");
  if(lock!=lockPayload(path))
    throw std::runtime_error("Core SDK changed since restore; remove lock and re-restore");
  return path;
}
inline void appRestore(const std::string& sdk) {
  appMainLanguage();
  const std::string canonical=normalizeSdk(sdk);
  const std::string payload=lockPayload(canonical);
  appDir(".xytron");
  if(appExists(".xytron/core.lock")) {
    projectPath(".xytron/core.lock",false);
    if(appReadAll(".xytron/core.lock")!=payload)
      throw std::runtime_error("Existing Core lock differs; explicit removal required");
  } else {
    // Refuse dangling symlinks and stale staging files before writing.
    checkedProjectOutput(".xytron/core.lock",true);
    checkedProjectOutput(".xytron/core.lock.tmp",true);
    std::ofstream out(".xytron/core.lock.tmp",std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("Cannot stage Core SDK lock");
    out<<payload;out.close();
    if(!out||std::rename(".xytron/core.lock.tmp",".xytron/core.lock")!=0) {
      std::remove(".xytron/core.lock.tmp");
      throw std::runtime_error("Cannot commit Core SDK lock");
    }
  }
  std::cout<<"Core SDK restored from locally installed prefix: "<<canonical<<"\n"
           <<"Browser engines and managed dependencies NOT restored; app run/publish blocked.\n";
}
inline void appCommand(const char* command,const char* action) {
  if(std::system(command)!=0)
    throw std::runtime_error(std::string(action)+" failed: "+command);
}
inline void appDoctor() {
  const std::string lang=appMainLanguage();
  const std::string prefix=restoredSdk();
  appCommand("cmake --version","CMake diagnostic");
  if(lang=="python") appCommand("python --version","Python runtime diagnostic");
  else if(lang=="csharp") appCommand("dotnet --version",".NET SDK diagnostic");
  std::cout<<"Core SDK and backend tools verified: "<<prefix<<" ("<<lang<<")\n"
           <<"Browser runtime, engine SDK, menu APIs and distribution NOT verified.\n";
}
inline void appBuild() {
  const std::string lang=appMainLanguage();
  const std::string coreSdk=restoredSdk();
  appDir(".xytron-build");
  if(lang=="cpp") {
    appDir(".xytron-build/source");
    appDir(".xytron-build/out");
    checkedProjectOutput(".xytron-build/source/CMakeLists.txt",false);
    const std::string makefile=
      "cmake_minimum_required(VERSION 3.16)\n"
      "project(xytron_user_backend LANGUAGES CXX)\n"
      "find_package(NativeWeb CONFIG REQUIRED)\n"
      "get_target_property(_xytron_inc NativeWeb::Core INTERFACE_INCLUDE_DIRECTORIES)\n"
      "add_executable(xytron_user_backend ../../backend/main.cpp)\n"
      "add_library(xytron_app_backend SHARED ../../backend/main.cpp)\n"
      "target_include_directories(xytron_app_backend PRIVATE ${_xytron_inc})\n"
      "target_compile_definitions(xytron_app_backend PRIVATE\n"
      "  XYTRON_APP_BACKEND_PLUGIN=1\n"
      "  XYTRON_PLUGIN_ID=\\\"app\\\"\n"
      "  XYTRON_PLUGIN_VERSION=\\\"1.0.0\\\")\n"
      "set_target_properties(xytron_user_backend xytron_app_backend PROPERTIES\n"
      "  CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)\n";
    std::ofstream out(".xytron-build/source/CMakeLists.txt",std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("Cannot create backend build manifest");
    out<<makefile;out.close();
    if(!out)throw std::runtime_error("Cannot save backend build manifest");
    const std::string configure="cmake -S .xytron-build/source -B .xytron-build/out \"-DCMAKE_PREFIX_PATH="+coreSdk+"\"";
    appCommand(configure.c_str(),"C++ backend configure");
    appCommand("cmake --build .xytron-build/out --config Release","C++ backend build");
  } else if(lang=="python") {
    appCommand("python -m py_compile backend/main.py","Python backend syntax check");
  } else {
    appDir(".xytron-build/dotnet");
    const std::string build="dotnet build backend/MainBackend.csproj --configuration Release --output .xytron-build/dotnet --nologo \"-p:XytronSdkPrefix="+coreSdk+"\"";
    appCommand(build.c_str(),"C# backend build");
  }
  if(lang=="python" || lang=="csharp") {
    const std::vector<std::string> exports=discoverManagedExports(
      lang,lang=="python"?"backend/main.py":"backend/Program.cs");
    const std::string manifest=".xytron-build/managed_operations.txt";
    checkedProjectOutput(manifest,false);
    std::ofstream file(manifest.c_str(),std::ios::binary|std::ios::trunc);
    if(!file)throw std::runtime_error("Cannot create managed operation grant");
    for(std::size_t i=0;i<exports.size();++i)
      file<<exports[i]<<"\n";
    file.close();
    if(!file)throw std::runtime_error("Cannot save managed operation grant");
    std::cout<<"Discovered "<<exports.size()
             <<" explicit Main operations (including create/dispose).\n";
  }
  std::cout<<"Backend build PASS ("<<lang<<"); C++ app ABI plugin built, browser host is external.\n";
}
inline void appTest() {
  const std::string lang=appMainLanguage();
  restoredSdk();
  if(lang=="cpp") {
    const std::string exe=
#ifdef _WIN32
      appExists(".xytron-build/out/Release/xytron_user_backend.exe")?
      ".xytron-build/out/Release/xytron_user_backend.exe":
      ".xytron-build/out/xytron_user_backend.exe";
#else
      ".xytron-build/out/xytron_user_backend";
#endif
    if(!appExists(exe))throw std::runtime_error("Backend missing; run xytron build");
    projectPath(exe,false); // No executable/path symlink substitution.
    // Execute by absolute executable path rather than shelling through
    // cmd.exe, which splits forward slashes and quotes in relative paths.
#ifdef _WIN32
    char absolute[MAX_PATH*3]={};
    if(!_fullpath(absolute,exe.c_str(),sizeof(absolute)))
      throw std::runtime_error("Cannot resolve backend executable");
    STARTUPINFOA startup={}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process={};
    if(!CreateProcessA(absolute,nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,
                       &startup,&process))
      throw std::runtime_error("CreateProcess failed for backend executable");
    const DWORD wait=WaitForSingleObject(process.hProcess,60000);
    if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);
    DWORD code=1;
    if(wait==WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if(wait!=WAIT_OBJECT_0||code!=0)
      throw std::runtime_error("C++ backend smoke process failed");
#else
    appCommand((std::string("\"")+exe+"\"").c_str(),"C++ backend smoke test");
#endif
  } else if(lang=="python") {
    appCommand("python -m py_compile backend/main.py","Python backend syntax test");
  } else {
    if(!appExists(".xytron-build/dotnet/MainBackend.dll"))
      throw std::runtime_error("C# backend missing; run xytron build");
    appCommand("dotnet .xytron-build/dotnet/MainBackend.dll","C# backend smoke test");
  }
  std::cout<<"Backend-only smoke test PASS; JS/browser E2E NOT exercised.\n";
}

inline std::string appBackendPlugin() {
#ifdef _WIN32
  const std::string path=
    appExists(".xytron-build/out/Release/xytron_app_backend.dll")?
    ".xytron-build/out/Release/xytron_app_backend.dll":
    ".xytron-build/out/xytron_app_backend.dll";
#elif defined(__APPLE__)
  const std::string path=".xytron-build/out/libxytron_app_backend.dylib";
#else
  const std::string path=".xytron-build/out/libxytron_app_backend.so";
#endif
  projectPath(path,false);
  return path;
}
inline void appRunHost(const std::string& path, bool smoke,
                       const std::string& featurePath) {
  const std::string language=appMainLanguage();
  const std::string sdk=restoredSdk();
  // Require a built, valid backend before launching the UI. Running this
  // executable here is still a backend smoke preflight, not JS backend wiring.
  appTest();
  projectPath("ui/index.html",false);
  const std::string backendPath=language=="cpp"?appBackendPlugin():std::string();
  if(language!="cpp") projectPath(".xytron-build/managed_operations.txt",false);
  if(!featurePath.empty()) {
    if(language!="cpp") throw std::runtime_error("Native feature plugin run is C++-only in this slice");
    projectPath(featurePath,false);
  }
  if(language=="python") {
    projectPath("backend/main.py",false);
  } else if(language=="csharp") {
    projectPath(".xytron-build/dotnet/MainBackend.dll",false);
    projectPath(".xytron-build/dotnet/MainBackend.runtimeconfig.json",false);
  }
  const std::string host=normalizeSdk(path);
  struct stat st;
  if(stat(host.c_str(),&st)!=0)
    throw std::runtime_error("Explicit native browser host is missing");
#ifdef _WIN32
  if(!(st.st_mode&_S_IFREG))
    throw std::runtime_error("Browser host is not a regular executable");
  char project[MAX_PATH*3]={};
  if(!GetCurrentDirectoryA(sizeof(project),project))
    throw std::runtime_error("Cannot resolve app project directory");
#else
  if(!S_ISREG(st.st_mode))
    throw std::runtime_error("Browser host is not a regular executable");
  char project[PATH_MAX]={};
  if(!getcwd(project,sizeof(project)))
    throw std::runtime_error("Cannot resolve app project directory");
#endif
  const std::string ui=std::string("--ui=")+project+"/ui/index.html";
  const std::string backendArg=language=="cpp"?
    std::string("--backend-plugin=")+project+"/"+backendPath:
    std::string("--backend-language=")+language;
  const std::string managedArg=language=="python"?
    std::string("--python-runner=")+sdk+"/share/xytron/python/app_worker.py":
    language=="csharp"?
    std::string("--dotnet-assembly=")+project+"/.xytron-build/dotnet/MainBackend.dll":
    std::string();
  const std::string managedDir=language=="python"?
    std::string("--python-project=")+project+"/backend":std::string();
  const std::string operationsArg=language=="cpp"?std::string():
    std::string("--managed-operations=")+project+"/.xytron-build/managed_operations.txt";
  const std::string featureArg=featurePath.empty()?std::string():
    std::string("--plugin-path=")+project+"/"+featurePath;
  const std::size_t slash=host.find_last_of("/\\");
  if(slash==std::string::npos)
    throw std::runtime_error("Native browser host path must be absolute");
  const std::string hostDir=host.substr(0,slash);
  std::cout<<"Launching real browser app:// with trusted "<<language<<" Main backend.\n";
#ifdef _WIN32
  // Paths are canonical, contain no command-line quote characters, and are
  // passed directly to CreateProcess (never via cmd.exe).
  std::string command="\""+host+"\" \""+ui+"\" \""+backendArg+"\"";
  if(!featureArg.empty()) command+=" \""+featureArg+"\"";
  if(!managedArg.empty()) command+=" \""+managedArg+"\"";
  if(!managedDir.empty()) command+=" \""+managedDir+"\"";
  if(!operationsArg.empty()) command+=" \""+operationsArg+"\"";
  if(smoke) command+=" --smoke";
  std::vector<char> mutableCmd(command.begin(),command.end());
  mutableCmd.push_back('\0');
  STARTUPINFOA startup={};startup.cb=sizeof(startup);
  PROCESS_INFORMATION process={};
  if(!CreateProcessA(host.c_str(),mutableCmd.data(),nullptr,nullptr,FALSE,0,
                     nullptr,hostDir.c_str(),&startup,&process))
    throw std::runtime_error("Cannot launch real WebView2/CEF browser host");
  const DWORD wait=WaitForSingleObject(process.hProcess,
      smoke?60000:INFINITE);
  DWORD code=1;
  if(wait==WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess,&code);
  if(wait!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1);
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
  if(wait!=WAIT_OBJECT_0||code!=0)
    throw std::runtime_error("Real desktop browser host failed");
#else
  pid_t child=fork();
  if(child<0) throw std::runtime_error("Cannot fork desktop browser host");
  if(child==0) {
    if(chdir(hostDir.c_str())!=0) _exit(127);
    std::vector<std::string> args;
    args.push_back(host);
    args.push_back("--no-sandbox");
    args.push_back(ui);
    args.push_back(backendArg);
    if(!featureArg.empty())args.push_back(featureArg);
    if(!managedArg.empty())args.push_back(managedArg);
    if(!managedDir.empty())args.push_back(managedDir);
    if(!operationsArg.empty())args.push_back(operationsArg);
    if(smoke)args.push_back("--smoke");
    std::vector<char*> native;
    for(std::size_t i=0;i<args.size();++i)
      native.push_back(const_cast<char*>(args[i].c_str()));
    native.push_back(nullptr);
    execv(host.c_str(),native.data());
    _exit(127);
  }
  int status=0;
  if(waitpid(child,&status,0)!=child ||
     !WIFEXITED(status)||WEXITSTATUS(status)!=0)
    throw std::runtime_error("Real desktop browser host failed");
#endif
  std::cout<<"Real browser app:// host completed successfully.\n";
}
} // namespace xytron_cli
#endif
