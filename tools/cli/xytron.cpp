// Native C++11 bootstrap CLI: project/plugin scaffolding, not runtime.
#include "core/binding_registry.hpp"
#include "plugin/plugin_module.hpp"
#include <cstdlib>
#include <fstream>
#include <regex>
#include "zip_store.hpp"
#include "json_validate.hpp"
#include "json_fields.hpp"
#include "file_tree.hpp"
#include "existing_output_guard.hpp"
#include "app_workflow.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif
static bool exists(const std::string& p) {
  struct stat x; return stat(p.c_str(),&x)==0;
}
static void dir(const std::string& p,bool mustBeNew) {
  if(exists(p)) {
    if(mustBeNew) throw std::runtime_error("Already exists: "+p);
    struct stat x;
    if(stat(p.c_str(),&x)!=0) throw std::runtime_error("Cannot stat "+p);
#ifdef _WIN32
    if(!(x.st_mode & _S_IFDIR)) throw std::runtime_error("Not directory: "+p);
#else
    if(!S_ISDIR(x.st_mode)) throw std::runtime_error("Not directory: "+p);
#endif
    return;
  }
#ifdef _WIN32
  if(_mkdir(p.c_str())!=0)
#else
  if(mkdir(p.c_str(),0755)!=0)
#endif
    throw std::runtime_error("Cannot create directory: "+p);
}
static void file(const std::string& p,const std::string& body) {
  if(exists(p)) throw std::runtime_error("Refusing overwrite: "+p);
  std::ofstream o(p.c_str(),std::ios::binary);
  if(!o) throw std::runtime_error("Cannot create: "+p);
  o<<body; o.close();
  if(!o) throw std::runtime_error("Write failed: "+p);
}
static bool valid(const std::string& id,bool module) {
  if(id.empty()||id.size()>64||id[0]<'a'||id[0]>'z') return false;
  for(std::size_t i=1;i<id.size();++i) {
    const char c=id[i];
    if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||
         c=='_'||(!module&&c=='-'))) return false;
  }
  return id!="xytron"&&id!="native"&&id!="invoke"&&
         id!="then"&&id!="constructor";
}
static std::string q(const std::string& s){return "\""+s+"\"";}
static void desktop(const std::string& id,const std::string& lang) {
  if(!valid(id,false)) throw std::runtime_error("Invalid project id");
  if(lang!="cpp"&&lang!="python"&&lang!="csharp")
    throw std::runtime_error("Backend must be cpp, python or csharp");
  const std::string entry=lang=="cpp"?"backend/main.cpp":
    lang=="python"?"backend/main.py":"backend/Program.cs";
  dir(id,true); dir(id+"/ui",true); dir(id+"/backend",true);
  file(id+"/xytron.json","{\n \"schemaVersion\": 1,\n \"app\": {\"id\": "+
    q(id)+", \"mode\":\"desktop\", \"engine\":\"auto\"},\n \"ui\": {\"entry\":\"app://desktop/index.html\", \"resources\":\"ui\"},\n \"main\": "+
    "{\"id\":\"app\", \"language\": "+q(lang)+", \"entry\": "+
    q(entry)+"},\n \"modules\": []\n}\n");
  if(lang=="cpp")
    file(id+"/ui/index.html","<!doctype html><html><body><h1>Xytron</h1>"
      "<p>Native desktop C++ bridge demo (requires explicit engine host).</p>"
      "<button id=\"xytron-ping\">Call user C++ backend</button>"
      "<output id=\"xytron-result\">idle</output>"
      "<button id=\"xytron-feature\">Call native feature plugin</button>"
      "<output id=\"xytron-feature-result\">idle</output>"
      "<script>document.getElementById('xytron-ping').onclick=async function(){"
      "const output=document.getElementById('xytron-result');"
      "try{output.textContent=await xytron.app.ping();}"
      "catch(error){output.textContent='error:'+String(error);}"
      "};document.getElementById('xytron-feature').onclick=async function(){"
      "const output=document.getElementById('xytron-feature-result');"
      "try{output.textContent=await xytron.feature.hello();}"
      "catch(error){output.textContent='error:'+String(error);}"
      "};</script></body></html>\n");
  else
    file(id+"/ui/index.html","<!doctype html><html><body><h1>Xytron</h1>"
      "<p>Managed Main runs through private JSONL worker and trusted app:// bridge.</p>"
      "<button id=\"xytron-managed\">Call managed Main</button>"
      "<output id=\"xytron-managed-result\">idle</output>"
      "<script>document.getElementById('xytron-managed').onclick=async function(){"
      "const output=document.getElementById('xytron-managed-result');"
      "try{const h=await xytron.app.Main.create();"
      "try{output.textContent=await xytron.app.Main.method.ping(h);}"
      "finally{await xytron.app.Main.dispose(h);}}"
      "catch(error){output.textContent='error:'+String(error);}"
      "};</script></body></html>\n");
  if(lang=="cpp")
    file(id+"/"+entry,
      "// A single user-owned C++ source backs both the CLI smoke executable\n"
      "// and the NativeWeb ABI v1 plugin loaded by the real browser host.\n"
      "#ifdef XYTRON_APP_BACKEND_PLUGIN\n"
      "#include <xytron/plugin.hpp>\n"
      "#include <string>\n"
      "static std::string ping() { return \"pong-from-user-backend\"; }\n"
      "XYTRON_MODULE(m) { m.function(\"ping\", &ping); }\n"
      "#else\n"
      "#include <iostream>\n"
      "int main() { std::cout << \"cpp backend smoke\\n\"; return 0; }\n"
      "#endif\n");
  else if(lang=="python")
    file(id+"/"+entry,
      "from xytron import export\n"
      "@export\n"
      "class Main:\n"
      "    @export\n"
      "    def ping(self):\n"
      "        return 'pong-from-user-python'\n");
  else {
    file(id+"/"+entry,
      "using System;\n"
      "using Xytron;\n"
      "[Export]\n"
      "public sealed class Main\n"
      "{\n"
      "    [Export] public string ping() => \"pong-from-user-csharp\";\n"
      "}\n"
      "internal static class Program\n"
      "{\n"
      "    static int Main()\n"
      "    {\n"
      "        var host = new ObjectHost(\"app\", new[] { typeof(Main) },\n"
      "            (owner, operation) => owner == \"app_session\" &&\n"
      "                operation.StartsWith(\"app.Main.\", StringComparison.Ordinal));\n"
      "        JsonLineHost.Run(Console.In, Console.Out, host, \"app_session\");\n"
      "        return 0;\n"
      "    }\n"
      "}\n");
    file(id+"/backend/MainBackend.csproj",
      "<Project Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup>"
      "<OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework>"
      "<Nullable>enable</Nullable></PropertyGroup><ItemGroup>"
      "<ProjectReference Include=\"$(XytronSdkPrefix)/share/xytron/dotnet/"
      "Xytron.Binding/Xytron.Binding.csproj\" /></ItemGroup></Project>\n");
  }
  std::cout<<"Created scaffold: "<<id<<" (Main "<<lang<<")\n";
}
static void pluginCpp(const std::string& id) {
  if(!valid(id,true)) throw std::runtime_error("Invalid module id");
  dir("plugins",false);
  const std::string p="plugins/"+id;
  dir(p,true);
  file(p+"/plugin.json",
    "{\n  \"schemaVersion\": 1,\n  \"name\": "+q(id)+
    ",\n  \"version\": \"1.0.0\",\n  \"runtime\": \"native\",\n"
    "  \"entry\": {\n"
    "    \"win-x64\": \"bin/win-x64/"+id+".dll\",\n"
    "    \"linux-x64\": \"bin/linux-x64/lib"+id+".so\"\n"
    "  }\n}\n");
  file(p+"/plugin.cpp","#include \"xytron/plugin.hpp\"\n"
    "#include <string>\n"
    "static std::string hello() { return std::string(\"hello\"); }\n"
    "XYTRON_MODULE(m) {\n"
    "  m.function(\"hello\", &hello);\n"
    "  const auto publisher=m.events();\n"
    "  m.function(\"fire\", [publisher]() {\n"
    "    publisher.emit(\"changed\", \"from-native-feature\");\n"
    "    return std::string(\"published\");\n"
    "  });\n"
    "}\n");
  file(p+"/README.md", "# "+id+" (C++ plugin)\n\n"
    "Edit plugin.cpp, then run `xytron plugin build "+id+" --sdk SDK_PREFIX` "
    "from the parent project. This is a native ABI v1 plugin; "
    "JS bridge and publish are separate integration steps.\n");
  file(p+"/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.19)\n"
    "project(xytron_native_plugin LANGUAGES CXX)\n"
    "file(READ \"${CMAKE_CURRENT_SOURCE_DIR}/plugin.json\" _manifest)\n"
    "string(JSON _id ERROR_VARIABLE _id_err GET \"${_manifest}\" name)\n"
    "string(JSON _ver ERROR_VARIABLE _ver_err GET \"${_manifest}\" version)\n"
    "string(JSON _runtime ERROR_VARIABLE _rt_err GET \"${_manifest}\" runtime)\n"
    "if(_id_err OR _ver_err OR _rt_err OR NOT _runtime STREQUAL \"native\")\n"
    "  message(FATAL_ERROR \"Invalid plugin.json\")\n"
    "endif()\n"
    "if(NOT _id MATCHES \"^[a-z][a-z0-9_]*$\" OR\n"
    "   NOT _ver MATCHES \"^[0-9]+[.][0-9]+[.][0-9]+$\")\n"
    "  message(FATAL_ERROR \"Invalid plugin ID/version\")\n"
    "endif()\n"
    "find_package(NativeWeb CONFIG REQUIRED)\n"
    "get_target_property(_inc NativeWeb::Core INTERFACE_INCLUDE_DIRECTORIES)\n"
    "add_library(${_id} SHARED plugin.cpp)\n"
    "target_include_directories(${_id} PRIVATE ${_inc})\n"
    "target_compile_definitions(${_id} PRIVATE\n"
    "  XYTRON_PLUGIN_ID=\\\"${_id}\\\"\n"
    "  XYTRON_PLUGIN_VERSION=\\\"${_ver}\\\")\n"
    "set_target_properties(${_id} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)\n"
    "# Stable C ABI v1; never link Core into plugin DLL/SO.\n");
  std::cout<<"Created C++ plugin scaffold: "<<p<<"\n";
}


// The project templates use normal language-native build descriptions.
// Python and .NET export declarations are metadata-only until their
// managed hosts are connected to the Xytron runtime.
static void pluginManaged(const std::string& id,const std::string& lang) {
  if(!valid(id,true)) throw std::runtime_error("Invalid module id");
  if(lang!="python"&&lang!="csharp")
    throw std::runtime_error("Unsupported managed plugin language");
  dir("plugins",false);
  const std::string p="plugins/"+id;
  dir(p,true);
  if(lang=="python") {
    dir(p+"/src",true); dir(p+"/src/"+id,true);
    dir(p+"/tests",true);
    file(p+"/plugin.json",
      "{\n  \"schemaVersion\": 1,\n  \"name\": "+q(id)+
      ",\n  \"version\": \"1.0.0\",\n  \"runtime\": \"python\",\n"
      "  \"entry\": \""+id+".plugin\"\n}\n");
    file(p+"/pyproject.toml",
      "[build-system]\nrequires = [\"setuptools>=61\", \"wheel\"]\n"
      "build-backend = \"setuptools.build_meta\"\n\n"
      "[project]\nname = \""+id+"\"\nversion = \"1.0.0\"\n"
      "requires-python = \">=3.9\"\n\n"
      "[tool.setuptools.packages.find]\nwhere = [\"src\"]\n");
    file(p+"/src/"+id+"/__init__.py", "from .plugin import hello\n");
    file(p+"/src/"+id+"/plugin.py",
      "# Requires Xytron's Python export SDK at runtime.\n"
      "from xytron import export\n\n"
      "@export\ndef hello(name: str) -> str:\n"
      "    return f\"Hello, {name}!\"\n");
    file(p+"/tests/test_plugin.py",
      "# With the Xytron Python SDK on PYTHONPATH:\n"
      "from "+id+".plugin import hello\n\n"
      "def test_hello():\n    assert hello(\"Xytron\") == \"Hello, Xytron!\"\n");
  } else {
    dir(p+"/src",true); dir(p+"/tests",true);
    file(p+"/plugin.json",
      "{\n  \"schemaVersion\": 1,\n  \"name\": "+q(id)+
      ",\n  \"version\": \"1.0.0\",\n  \"runtime\": \"dotnet\",\n"
      "  \"entry\": \"bin/"+id+".dll\"\n}\n");
    file(p+"/Plugin.csproj",
      "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
      "  <PropertyGroup>\n    <TargetFramework>net8.0</TargetFramework>\n"
      "    <Nullable>enable</Nullable>\n"
      "    <AssemblyName>"+id+"</AssemblyName>\n"
      "  </PropertyGroup>\n</Project>\n");
    file(p+"/src/Plugin.cs",
      "using Xytron;\n\npublic static class Plugin\n{\n"
      "    [Export]\n"
      "    public static string Hello(string name) => $\"Hello, {name}!\";\n}\n");
    file(p+"/src/ExportAttribute.cs",
      "// Temporary metadata-only contract copied into standalone templates.\n"
      "// Replace with the official Xytron.Binding SDK reference when available.\n"
      "using System;\nnamespace Xytron\n{\n"
      "    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Method | AttributeTargets.Constructor | AttributeTargets.Property)]\n"
      "    public sealed class ExportAttribute : Attribute { }\n}\n");
    file(p+"/tests/README.md",
      "Test the Hello method with your preferred .NET test runner.\n"
      "The language host and JS bridge are not implemented yet.\n");
  }
  file(p+"/README.md","# "+id+" ("+lang+" plugin)\n\n"
    "Edit the source, use native language tooling to build or test, "
    "and keep plugin.json in sync with the package output.\n\n"
    "The runtime field selects the future loader, not the authoring language. "
    "Use xytron build, xytron pack and xytron publish --output DIR. "
    "Use `xytron bindings generate --out bindings` for Python; "
    "add `--trusted` after building a local C# assembly.\n"
    "Live JS dispatch and signed distribution remain TODO.\n");
  std::cout<<"Created "<<lang<<" plugin scaffold: "<<p<<"\n";
}
static void newPlugin(const std::string& id,const std::string& lang) {
  if(lang=="cpp") pluginCpp(id);
  else pluginManaged(id,lang);
}

// The CLI invokes CMake for a user-authored, locally trusted plugin tree.
// Validate the *entire* SDK prefix before shelling out so no command syntax
// can be injected through a raw CLI argument.
static bool safeSdkPrefix(const std::string& path) {
  if(path.empty() || path.size()>700) return false;
  for(std::size_t i=0;i<path.size();++i) {
    const char c=path[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
         (c>='0'&&c<='9')||c=='_'||c=='-'||
         c=='/'||c=='\\'||c=='.'||c==':'||c==' '||c=='~'))
      return false;
  }
  return true;
}
static void doctorSdk(const std::string& sdk) {
  if(!safeSdkPrefix(sdk))
    throw std::runtime_error("Unsafe/invalid SDK prefix path");
  const std::string header=sdk+"/include/nativeweb/plugin.hpp";
  const std::string config=sdk+"/lib/cmake/NativeWeb/NativeWebConfig.cmake";
  const std::string alternative=sdk+"/lib64/cmake/NativeWeb/NativeWebConfig.cmake";
  if(!exists(header))
    throw std::runtime_error("NativeWeb plugin.hpp is missing: "+header);
  if(!exists(config) && !exists(alternative))
    throw std::runtime_error("NativeWeb CMake package is missing under: "+sdk);
  std::cout<<"xytron doctor: NativeWeb headers and CMake SDK package FOUND\n";
  if(std::system("cmake --version")!=0)
    throw std::runtime_error("CMake is not available on PATH");
  std::cout<<"xytron doctor: installed Core SDK + CMake PASS\n";
  std::cout<<"Note: browser engines, Python/.NET hosts and app packaging are NOT checked\n";
}

static void pluginBuild(const std::string& id,const std::string& sdk) {
  if(!valid(id,true)) throw std::runtime_error("Invalid plugin id");
  if(!safeSdkPrefix(sdk))
    throw std::runtime_error("Unsafe/invalid SDK prefix path");
  const std::string source="plugins/"+id;
  const std::string cmake=source+"/CMakeLists.txt";
  if(!exists(cmake))
    throw std::runtime_error("Missing plugin CMakeLists.txt: "+cmake);
  const std::string output=source+"/build";
  const std::string configure="cmake -S \""+source+
    "\" -B \""+output+"\" \"-DCMAKE_PREFIX_PATH="+sdk+"\"";
  if(std::system(configure.c_str())!=0)
    throw std::runtime_error("CMake plugin configuration failed");
  const std::string compile="cmake --build \""+output+"\" --config Release";
  if(std::system(compile.c_str())!=0)
    throw std::runtime_error("CMake plugin build failed");
  std::cout<<"Xytron plugin build: PASS ("<<id<<")\n";
}

static void moduleAdd(const std::string& id, const std::string& lang) {
  if(!valid(id,true)) throw std::runtime_error("Invalid feature Module id");
  if(lang!="cpp"&&lang!="python"&&lang!="csharp")
    throw std::runtime_error("Module must be cpp, python or csharp");
  xytron_cli::checkedExistingOutput("xytron.json");
  std::ifstream in("xytron.json", std::ios::binary);
  if(!in) throw std::runtime_error("Run module add inside a xytron project");
  std::ostringstream ss;ss<<in.rdbuf();
  const std::string doc=ss.str();
  if(doc.size()>1024*1024) throw std::runtime_error("Manifest too large");
  const std::string key="\"modules\": [";
  const std::size_t begin=doc.find(key);
  if(begin==std::string::npos ||
     doc.find(key,begin+1)!=std::string::npos ||
     doc.find("\"schemaVersion\": 1")==std::string::npos ||
     doc.find("\"main\": {")==std::string::npos)
    throw std::runtime_error("Unsupported xytron.json format");
  const std::size_t open=begin+key.size()-1;
  const std::size_t close=doc.find(']',open+1);
  if(close==std::string::npos || doc.find("\n}",close)==std::string::npos)
    throw std::runtime_error("Malformed Modules manifest array");
  const std::string items=doc.substr(open+1,close-open-1);
  // Accept exactly the canonical inline records emitted by this CLI.
  // Hand-authored manifests must use a future schema-aware JSON editor.
  std::size_t cursor=0;
  while(cursor<items.size()) {
    if(items[cursor]==' ' || items[cursor]=='\n' || items[cursor]==',')
      {++cursor;continue;}
    const std::string prefix="{\"id\":\"";
    if(items.compare(cursor,prefix.size(),prefix)!=0)
      throw std::runtime_error("Unsupported Module record structure");
    const std::size_t startId=cursor+prefix.size();
    const std::size_t endId=items.find('"',startId);
    if(endId==std::string::npos) throw std::runtime_error("Invalid Module record");
    const std::string existing=items.substr(startId,endId-startId);
    if(!valid(existing,true)) throw std::runtime_error("Invalid existing Module id");
    if(existing==id) throw std::runtime_error("Module id already exists: "+id);
    const std::size_t end=items.find('}',endId);
    if(end==std::string::npos) throw std::runtime_error("Invalid Module object");
    const std::string item=items.substr(cursor,end+1-cursor);
    if(item.find("\"delivery\":\"embedded\",\"path\":\"modules/"+existing+"\"")==std::string::npos ||
       item.find("\"language\":\"")==std::string::npos)
      throw std::runtime_error("Unrecognized Module item");
    cursor=end+1;
  }
  const std::string path="modules/"+id;
  if(exists(path)) throw std::runtime_error("Module already exists: "+id);
  dir("modules",false);
  dir(path,true);
  file(path+"/module.json", "{\"schemaVersion\":1,\"id\":"+q(id)+
    ",\"role\":\"feature\",\"language\":"+q(lang)+
    ",\"delivery\":\"embedded\"}\n");
  if(lang=="cpp")
    file(path+"/module.cpp","// Module scaffold only; JS dispatch TODO.\n"
      "#include <string>\nstd::string hello(){return \"hello\";}\n");
  else if(lang=="python")
    file(path+"/module.py","# Module scaffold only; JS dispatch TODO.\n"
      "async def hello():\n    return 'hello'\n");
  else {
    file(path+"/Module.cs","// Module scaffold only; JS dispatch TODO.\n"
      "public static class Module { public static string Hello() => \"hello\"; }\n");
    file(path+"/Module.csproj","<Project Sdk=\"Microsoft.NET.Sdk\">"
      "<PropertyGroup><TargetFramework>net8.0</TargetFramework>"
      "</PropertyGroup></Project>\n");
  }
  const std::string item="{\"id\":"+q(id)+",\"language\":"+q(lang)+
    ",\"delivery\":\"embedded\",\"path\":"+q(path)+"}";
  const std::string updated=doc.substr(0,close)+
    (items.empty()?"":",")+item+doc.substr(close);
  // Validate and create generated source before writing project metadata.
  // Transactional writes / generalized JSON migration remain follow-up.
  xytron_cli::checkedExistingOutput("xytron.json");
  std::ofstream out("xytron.json",std::ios::binary|std::ios::trunc);
  if(!out) throw std::runtime_error("Cannot update xytron.json");
  out<<updated;out.close();
  if(!out) throw std::runtime_error("Failed to save xytron.json");
  std::cout<<"Added "<<lang<<" feature Module "<<id
           <<" (scaffold only, not loaded by runtime)\n";
}


struct PluginSpec {
  std::string id,version,runtime;
};
static PluginSpec readPluginSpec(const std::string& directory) {
  std::ifstream in((directory+"/plugin.json").c_str(),std::ios::binary);
  if(!in) throw std::runtime_error("Missing plugin.json under "+directory);
  std::ostringstream stream;stream<<in.rdbuf();
  const std::string doc=stream.str();
  if(doc.size()>65536) throw std::runtime_error("Oversized plugin manifest");
  xytron_cli::JsonSyntax(doc).validate();
  const xytron_cli::JsonObjectFields manifest(doc);
  if(manifest.raw("schemaVersion")!="1")
    throw std::runtime_error("Unsupported plugin schemaVersion");
  PluginSpec p;
  p.id=manifest.string("name");
  p.version=manifest.string("version");
  p.runtime=manifest.string("runtime");
  if(!valid(p.id,true)) throw std::runtime_error("Invalid plugin name");
  if(!std::regex_match(p.version,std::regex("[0-9]+\\.[0-9]+\\.[0-9]+")))
    throw std::runtime_error("Invalid plugin version");
  if(p.runtime!="native"&&p.runtime!="python"&&p.runtime!="dotnet")
    throw std::runtime_error("Unsupported plugin runtime");
  if(p.runtime=="python" && manifest.string("entry")!=p.id+".plugin")
    throw std::runtime_error("Python plugin entry does not match generated package");
  if(p.runtime=="dotnet" && manifest.string("entry")!="bin/"+p.id+".dll")
    throw std::runtime_error(".NET plugin entry does not match generated package");
  if(p.runtime=="native") {
    const xytron_cli::JsonObjectFields entry=manifest.object("entry");
    if(entry.string("win-x64")!="bin/win-x64/"+p.id+".dll" ||
       entry.string("linux-x64")!="bin/linux-x64/lib"+p.id+".so")
      throw std::runtime_error("Native plugin platform entries differ from package layout");
  }
  return p;
}
static std::string nativeRid() {
#ifdef _WIN32
  return "win-x64";
#elif defined(__linux__) && defined(__x86_64__)
  return "linux-x64";
#else
  throw std::runtime_error("Native plugin pack is supported on win-x64/linux-x64 only");
#endif
}
static std::string nativeLibrary(const std::string& id) {
#ifdef _WIN32
  return id+".dll";
#else
  return "lib"+id+".so";
#endif
}
// This command loads and executes registration code from a *trusted local*
// native library. Never invoke it on a downloaded/unverified plugin.
static std::string tsType(nativeweb::detail::CallableTypeKind kind) {
  using nativeweb::detail::CallableTypeKind;
  switch(kind) {
  case CallableTypeKind::Void: return "void";
  case CallableTypeKind::Boolean: return "boolean";
  case CallableTypeKind::Integer:
  case CallableTypeKind::Floating: return "number";
  case CallableTypeKind::String: return "string";
  case CallableTypeKind::Binary: return "Uint8Array";
  default: throw std::runtime_error("Cannot emit an unknown native type");
  }
}
static void generateBindings(const std::string& library,
                             const std::string& output) {
  if(library.empty() || output.empty() || output=="." ||
     output.find("..")!=std::string::npos)
    throw std::runtime_error("Invalid bindings path");
  const PluginSpec manifest=readPluginSpec(".");
  if(manifest.runtime!="native")
    throw std::runtime_error("Native bindgen currently supports C++ only");
  // Loading runs plugin code. Caller must explicitly select --trusted.
  nativeweb::detail::BindingRegistry registry;
  nativeweb::detail::PluginModule module(library,registry);
  if(module.id()!=manifest.id || module.version()!=manifest.version)
    throw std::runtime_error("Plugin binary does not match plugin.json");
  const std::vector<std::string> methods=module.registeredFunctions();
  if(methods.empty()) throw std::runtime_error("No native functions registered");
  std::ostringstream meta,decl,js;
  meta << "{\n  \"schemaVersion\": 1,\n  \"runtime\": \"native\",\n"
       << "  \"pluginId\": " << q(manifest.id) << ",\n  \"functions\": [\n";
  decl << "// Generated by Xytron; edit native source instead.\n";
  js << "// Generated by Xytron; requires the Xytron window bridge.\n";
  for(std::size_t i=0;i<methods.size();++i) {
    const std::string& full=methods[i];
    const std::string name=full.substr(manifest.id.size()+1);
    if(full.compare(0,manifest.id.size()+1,manifest.id+".")!=0 ||
       !valid(name,true)) throw std::runtime_error("Invalid JS export name: "+full);
    nativeweb::detail::CallableSignature sig;
    if(!registry.signature(full,&sig) ||
       sig.result.kind==nativeweb::detail::CallableTypeKind::Unknown)
      throw std::runtime_error("Legacy/untyped function cannot generate bindings: "+full);
    meta << (i?",\n":"") << "    {\"name\": " << q(name)
         << ", \"arguments\": [";
    decl << "export declare function " << name << "(";
    for(std::size_t j=0;j<sig.arguments.size();++j) {
      const std::string type=tsType(sig.arguments[j].kind);
      if(j){meta << ", ";decl << ", ";}
      meta << q(type);
      decl << "arg" << j << ": " << type;
    }
    const std::string result=tsType(sig.result.kind);
    meta << "], \"returns\": " << q(result) << "}";
    decl << "): Promise<" << result << ">;\n";
    js << "export function " << name << "(...args) {\n"
          "  const bridge = globalThis.xytron;\n"
          "  if (!bridge || typeof bridge.invoke !== 'function')\n"
          "    throw new Error('Xytron bridge unavailable');\n"
          "  return bridge.invoke(" << q(full) << ", ...args);\n}\n";
  }
  meta << "\n  ]\n}\n";
  dir(output,false);
  // Refuse overwrite, so a failed invocation cannot silently replace files.
  file(output+"/exports.json",meta.str());
  file(output+"/index.d.ts",decl.str());
  file(output+"/index.mjs",js.str());
  std::cout << "Generated typed native bindings for " << methods.size()
            << " function(s): " << output << "\n";
}
// Managed bindgen is a separate installed tool: Python is parsed without
// executing user code; .NET requires explicit --trusted for reflection.
// Native plugin bindgen retains the existing --library --trusted workflow.
static std::string cliSupportDir(const std::string& program) {
  // Use the actual binary path even when invoked via PATH as plain "xytron".
  std::string executable=program;
#ifdef _WIN32
  char fullPath[4096];
  const DWORD length=GetModuleFileNameA(NULL,fullPath,sizeof(fullPath));
  if(length>0 && length<sizeof(fullPath))executable.assign(fullPath,length);
#else
  char fullPath[4096];
  const ssize_t length=readlink("/proc/self/exe",fullPath,sizeof(fullPath)-1);
  if(length>0)executable.assign(fullPath,static_cast<std::size_t>(length));
#endif
  const std::size_t slash=executable.find_last_of("/\\");
  if(slash!=std::string::npos) {
    const std::string bindir=executable.substr(0,slash);
    const std::string installed=bindir+"/../share/xytron/cli";
    if(exists(installed+"/generate_managed_bindings.py"))return installed;
  }
#ifdef XYTRON_CLI_DEV_TOOLS
  const std::string source=XYTRON_CLI_DEV_TOOLS;
  if(exists(source+"/generate_managed_bindings.py"))return source;
#endif
  throw std::runtime_error("Managed bindings support files missing; reinstall Xytron CLI");
}
static void generateManagedBindings(const std::string& out,bool trusted,
                                    const std::string& executable) {
  if(out.empty() || out=="." || out.find("..")!=std::string::npos ||
     !safeSdkPrefix(out))
    throw std::runtime_error("Invalid output directory for managed bindings");
  const PluginSpec spec=readPluginSpec(".");
  if(spec.runtime=="native")
    throw std::runtime_error("Native bindings require --library PATH --trusted");
  if(spec.runtime=="dotnet" && !trusted)
    throw std::runtime_error(".NET reflection executes trusted local assembly; supply --trusted");
  const std::string support=cliSupportDir(executable);
  if(!safeSdkPrefix(support))throw std::runtime_error("Unsupported CLI tool location");
#ifdef _WIN32
  const std::string python="python";
#else
  const std::string python="python3";
#endif
  std::string command;
  if(spec.runtime=="dotnet") {
    char cwd[4096];
#ifdef _WIN32
    if(!_getcwd(cwd,sizeof(cwd)))throw std::runtime_error("Cannot resolve plugin directory");
#else
    if(!getcwd(cwd,sizeof(cwd)))throw std::runtime_error("Cannot resolve plugin directory");
#endif
    const std::string root=cwd;
    if(!safeSdkPrefix(root)) {
      for(std::size_t i=0;i<root.size();++i)
        if(!safeSdkPrefix(root.substr(i,1)))
          throw std::runtime_error("Unsupported plugin directory byte: "+
              std::to_string(static_cast<unsigned char>(root[i])));
      throw std::runtime_error("Plugin directory path is too long");
    }
    const std::string assembly=root+"/build/"+spec.id+".dll";
    const std::string output=root+"/"+out;
    command="dotnet run --project \""+support+
      "/dotnet_reflector/Xytron.Metadata.csproj\" --configuration Release -- \""+
      assembly+"\" \""+output+"\" \""+spec.id+"\"";
  } else {
    command=python+" \""+support+
      "/generate_managed_bindings.py\" --project . --out \""+out+"\"";
  }

  if(std::system(command.c_str())!=0)
    throw std::runtime_error("Managed bindings generation failed");
}
static void pluginPack(const std::string& source) {
  const PluginSpec spec=readPluginSpec(source);
  const std::string base=source+"/";
  xytron_cli::ZipStore archive;
  archive.add("plugin.json",base+"plugin.json");
  std::string rid="any";
  if(spec.runtime=="native") {
    rid=nativeRid();
    const std::string library=nativeLibrary(spec.id);
    std::string built=base+"build/"+library;
#ifdef _WIN32
    built=base+"build/Release/"+library;
#endif
    archive.add("bin/"+rid+"/"+library,built);
  } else if(spec.runtime=="dotnet") {
    // Include referenced DLLs and metadata copied by dotnet build. External
    // runtime installation/restore remains a host deployment responsibility.
    const std::string outDir=base+"build";
    std::vector<std::string> built;xytron_cli::sourceFiles(outDir,"",built);
    bool assemblyFound=false;
    for(std::size_t i=0;i<built.size();++i) {
      if(built[i]==spec.id+".dll") assemblyFound=true;
      archive.add("bin/"+built[i],outDir+"/"+built[i]);
    }
    if(!assemblyFound) throw std::runtime_error("Missing .NET plugin assembly");
  } else {
    const std::string src=base+"src/"+spec.id;
    std::vector<std::string> files;xytron_cli::sourceFiles(src,"",files);
    bool entryFound=false;
    for(std::size_t i=0;i<files.size();++i) {
      if(files[i]=="plugin.py") entryFound=true;
      archive.add("python/"+spec.id+"/"+files[i],src+"/"+files[i]);
    }
    if(!entryFound) throw std::runtime_error("Missing Python plugin entry");
    archive.add("pyproject.toml",base+"pyproject.toml");
  }
  // Include generated typed bindings only as a complete checked set.
  const std::string gen=base+"bindings/";
  const bool any=exists(gen+"exports.json") || exists(gen+"index.d.ts") ||
                 exists(gen+"index.mjs");
  if(any) {
    if(!exists(gen+"exports.json") || !exists(gen+"index.d.ts") ||
       !exists(gen+"index.mjs"))
      throw std::runtime_error("Incomplete generated bindings; regenerate first");
    archive.add("bindings/exports.json",gen+"exports.json");
    archive.add("bindings/index.d.ts",gen+"index.d.ts");
    archive.add("bindings/index.mjs",gen+"index.mjs");
  }
  dir(base+"dist",false);
  const std::string out=base+"dist/"+spec.id+"-"+spec.version+"-"+rid+".xplugin";
  archive.write(out);
  std::cout<<"Created plugin archive: "<<out<<"\n"
           <<"Note: runtime dependency closure, signing and remote publishing are NOT implemented.\n";
}
static void pluginBuildCurrent(const std::string& sdk) {
  const PluginSpec spec=readPluginSpec(".");
  if(spec.runtime=="native") {
    if(sdk.empty()) throw std::runtime_error("Native build requires --sdk PREFIX");
    if(!safeSdkPrefix(sdk)) throw std::runtime_error("Invalid SDK prefix");
    // Reuse installed SDK and generated CMake project without copying Core.
    const std::string configure="cmake -S . -B build \"-DCMAKE_PREFIX_PATH="+sdk+"\"";
    if(std::system(configure.c_str())!=0) throw std::runtime_error("CMake configure failed");
    if(std::system("cmake --build build --config Release")!=0)
      throw std::runtime_error("CMake build failed");
  } else if(spec.runtime=="dotnet") {
    if(!sdk.empty()) throw std::runtime_error("--sdk is for native plugins only");
    if(std::system("dotnet build Plugin.csproj --configuration Release --output build")!=0)
      throw std::runtime_error("dotnet build failed or .NET SDK missing");
  } else {
    if(!sdk.empty()) throw std::runtime_error("--sdk is for native plugins only");
#ifdef _WIN32
    const std::string cmd="python -m compileall -f -q src/"+spec.id;
#else
    const std::string cmd="python3 -m compileall -f -q src/"+spec.id;
#endif
    if(std::system(cmd.c_str())!=0)
      throw std::runtime_error("Python syntax check failed or interpreter missing");
  }
  std::cout<<"Plugin build/syntax check PASS ("<<spec.id<<", "<<spec.runtime<<")\n";
}
// Local artifact publication only. Remote registries, authentication, signing
// and supply-chain verification will be a separate release milestone.
static void pluginPublish(const std::string& output) {
  if(output.empty() || output.size()>700)
    throw std::runtime_error("Invalid publish output directory");
  // No shell execution here; spaces are safe, but reject parent traversal.
  if(output.find("..")!=std::string::npos)
    throw std::runtime_error("Parent traversal in publish output");
  const PluginSpec spec=readPluginSpec(".");
  const std::string rid=spec.runtime=="native"?nativeRid():"any";
  const std::string name=spec.id+"-"+spec.version+"-"+rid+".xplugin";
  const std::string src="dist/"+name;
  std::ifstream in(src.c_str(),std::ios::binary);
  if(!in) throw std::runtime_error("Run xytron pack before publish: "+src);
  in.seekg(0,std::ios::end);
  const std::streamoff length=in.tellg();
  if(length<=0 || length>64*1024*1024) throw std::runtime_error("Invalid artifact size");
  in.seekg(0,std::ios::beg);
  dir(output,false);
  const std::string dest=output+"/"+name;
  if(exists(dest)) throw std::runtime_error("Refusing to overwrite published artifact: "+dest);
  std::ofstream out(dest.c_str(),std::ios::binary);
  if(!out) throw std::runtime_error("Cannot publish to "+dest);
  out<<in.rdbuf();out.close();
  if(!out) throw std::runtime_error("Local artifact publication failed");
  std::cout<<"Published local archive: "<<dest<<"\n"
           <<"Remote registry upload, signing and installation are not supported.\n";
}
static void help() {
  std::cout<<"xytron bootstrap CLI (partial)\n"
    "  new app NAME [--backend cpp|python|csharp] (default: cpp)\n"
    "  new desktop NAME [--backend cpp|python|csharp]\n"
    "  new plugin NAME [--lang cpp|python|csharp] (default: cpp)\n"
    "  plugin new NAME --language cpp|python|csharp\n"
    "  plugin build NAME --sdk INSTALLED_SDK_PREFIX (C++ only)\n"
    "  module add NAME --language cpp|python|csharp\n"
    "  doctor --sdk INSTALLED_SDK_PREFIX\n"
    "  bindings generate --library PATH --out DIR --trusted (C++ only)\n"
    "  bindings generate --out DIR [--trusted] (Python/.NET plugin cwd)\n"
    "  build [--sdk PREFIX] (inside plugin project)\n"
    "  pack (inside plugin project)\n"
    "  plugin pack NAME (from parent project)\n"
    "  publish [--output DIR] (local archive copy, inside plugin project)\n"
    "  --help / --version\n"
    "  validate (inside a generated desktop project)\n"
    "  restore --sdk INSTALLED_CORE_SDK (desktop project; Core only)\n"
    "  build / test / doctor (desktop project; backend-only)\n"
    "  run --host ABSOLUTE_ENGINE_HOST [--plugin PROJECT_PLUGIN_PATH] [--smoke] (C++/Python/C# app)\n"
    "  automatic browser host restore / desktop publish: NOT IMPLEMENTED\n";
}
int main(int argc,char** argv) {
  try {
    if(argc==1||(argc==2&&std::string(argv[1])=="--help")) {
      help(); return 0;
    }
    if(argc==2&&std::string(argv[1])=="--version") {
      std::cout<<"xytron bootstrap 0.2.0\n"; return 0;
    }
    if(argc>=4 && std::string(argv[1])=="new" &&
       (std::string(argv[2])=="desktop"||std::string(argv[2])=="app") &&
       (argc==4 || (argc==6 && std::string(argv[4])=="--backend"))) {
      desktop(argv[3],argc==4?"cpp":argv[5]);return 0;
    }
    if(argc>=4 && std::string(argv[1])=="new" &&
       std::string(argv[2])=="plugin" &&
       (argc==4 || (argc==6 && std::string(argv[4])=="--lang"))) {
      newPlugin(argv[3],argc==4?"cpp":argv[5]);return 0;
    }
    if(argc==2 && std::string(argv[1])=="publish") {
      if(exists("xytron.json"))
        throw std::runtime_error("Desktop publish unavailable: browser runtime and app shell not packaged");
      pluginPublish("releases");return 0;
    }
    if(argc==4 && std::string(argv[1])=="publish"&&
       std::string(argv[2])=="--output") {
      pluginPublish(argv[3]);return 0;
    }
    if(argc==8 && std::string(argv[1])=="bindings" &&
       std::string(argv[2])=="generate" &&
       std::string(argv[3])=="--library" &&
       std::string(argv[5])=="--out" &&
       std::string(argv[7])=="--trusted") {
      generateBindings(argv[4],argv[6]); return 0;
    }
    if((argc==5 || argc==6) && std::string(argv[1])=="bindings" &&
       std::string(argv[2])=="generate" &&
       std::string(argv[3])=="--out" &&
       (argc==5 || std::string(argv[5])=="--trusted")) {
      generateManagedBindings(argv[4],argc==6,argv[0]); return 0;
    }
    if(argc==2 && std::string(argv[1])=="pack") {
      pluginPack(".");return 0;
    }
    if(argc==2 && std::string(argv[1])=="build") {
      if(exists("xytron.json")) xytron_cli::appBuild();
      else pluginBuildCurrent("");
      return 0;
    }
    if(argc==4 && std::string(argv[1])=="build"&&
       std::string(argv[2])=="--sdk") {
      pluginBuildCurrent(argv[3]);return 0;
    }
    if(argc==4 && std::string(argv[1])=="plugin"&&
       std::string(argv[2])=="pack") {
      if(!valid(argv[3],true)) throw std::runtime_error("Invalid plugin id");
      pluginPack(std::string("plugins/")+argv[3]);return 0;
    }
    if(argc==6&&std::string(argv[1])=="module"&&
       std::string(argv[2])=="add"&&std::string(argv[4])=="--language") {
      moduleAdd(argv[3],argv[5]);return 0;
    }
    if(argc==2 && std::string(argv[1])=="validate") {
      xytron_cli::validateProjectManifest();
      std::cout<<"Generated desktop project manifest validated (not a runtime acceptance test)\n";
      return 0;
    }
    if(argc==4 && std::string(argv[1])=="restore"&&
       std::string(argv[2])=="--sdk") {
      xytron_cli::appRestore(argv[3]);return 0;
    }
    if(argc==2 && std::string(argv[1])=="test" && exists("xytron.json")) {
      xytron_cli::appTest();return 0;
    }
    if(argc==2 && std::string(argv[1])=="doctor" && exists("xytron.json")) {
      xytron_cli::appDoctor();return 0;
    }
    if((argc==4 || argc==5 || argc==6 || argc==7) &&
       std::string(argv[1])=="run" &&
       std::string(argv[2])=="--host" &&
       ((argc<=5 && (argc==4 || std::string(argv[4])=="--smoke")) ||
        (argc>=6 && std::string(argv[4])=="--plugin" &&
         (argc==6 || std::string(argv[6])=="--smoke")))) {
      xytron_cli::appRunHost(argv[3],
                             argc==5 || argc==7,
                             argc>=6?argv[5]:"");
      return 0;
    }
    if(argc==2 && std::string(argv[1])=="run" && exists("xytron.json")) {
      std::cerr<<"Desktop run unavailable: native shell and browser engine not integrated\n";
      return 2;
    }
    if(argc==4&&std::string(argv[1])=="doctor"&&
       std::string(argv[2])=="--sdk") {
      doctorSdk(argv[3]);return 0;
    }
    if(argc==6&&std::string(argv[1])=="plugin"&&
       std::string(argv[2])=="build"&&std::string(argv[4])=="--sdk") {
      pluginBuild(argv[3],argv[5]);return 0;
    }
    if(argc==6&&std::string(argv[1])=="plugin"&&
       std::string(argv[2])=="new"&&std::string(argv[4])=="--language") {
      newPlugin(argv[3],argv[5]);return 0;
    }
    help();std::cerr<<"Invalid or unimplemented xytron command\n";return 2;
  } catch(const std::exception& e) {
    std::cerr<<"xytron: "<<e.what()<<std::endl;return 1;
  }
}
