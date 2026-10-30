#ifndef XYTRON_CLI_PROJECT_MANIFEST_HPP
#define XYTRON_CLI_PROJECT_MANIFEST_HPP
// Standalone, C++11 validator for Scope A1 generated project manifests.
// No commands are executed and no manifest-provided path is opened before
// identifier/relative-path checks. This is NOT a runtime/engine verification.
#include "json_fields.hpp"
#include "json_validate.hpp"
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace xytron_cli {
inline void requireProject(bool valid, const std::string& message) {
  if(!valid) throw std::runtime_error("Invalid project: " + message);
}
inline bool projectIdentifier(const std::string& id) {
  if(id.empty()||id.size()>64||id[0]<'a'||id[0]>'z')return false;
  for(std::size_t i=1;i<id.size();++i) {
    const char c=id[i];
    if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
  }
  return id!="xytron"&&id!="native"&&id!="invoke"&&id!="then"&&id!="constructor";
}
inline bool moduleIdentifier(const std::string& id) {
  if(!projectIdentifier(id))return false;
  return id.find('-')==std::string::npos;
}
inline bool projectLanguage(const std::string& lang) {
  return lang=="cpp"||lang=="python"||lang=="csharp";
}
inline std::string readProjectFile(const std::string& filename) {
  std::ifstream in(filename.c_str(),std::ios::binary);
  requireProject(!!in,"missing " + filename);
  std::string text;
  char buf[4096];
  while(in.read(buf,sizeof(buf)) || in.gcount()) {
    text.append(buf,static_cast<std::size_t>(in.gcount()));
    requireProject(text.size()<=1024*1024,"oversized " + filename);
  }
  requireProject(!in.bad(),"unable to read " + filename);
  JsonSyntax(text).validate();
  return text;
}
// Reject symlinks/reparse points at every path component: project validation
// should not inspect unexpected files outside its working directory.
inline void projectPath(const std::string& path, bool directory) {
  requireProject(!path.empty()&&path.size()<512,"invalid project path");
  std::string prefix;
  std::size_t start=0;
  while(start<path.size()) {
    const std::size_t slash=path.find('/',start);
    const std::string part=path.substr(start,slash==std::string::npos?
                                        std::string::npos:slash-start);
    requireProject(!part.empty()&&part!="."&&part!="..","unsafe path component");
    for(std::size_t k=0;k<part.size();++k) {
      const char c=part[k];
      requireProject((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
                     (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.',
                     "unsafe path byte");
    }
    if(!prefix.empty())prefix+='/';
    prefix+=part;
    const bool expectDir=slash!=std::string::npos||directory;
#ifdef _WIN32
    const DWORD attrs=GetFileAttributesA(prefix.c_str());
    requireProject(attrs!=INVALID_FILE_ATTRIBUTES,"missing " + prefix);
    requireProject(!(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"reparse point " + prefix);
    requireProject(((attrs&FILE_ATTRIBUTE_DIRECTORY)!=0)==expectDir,
                   "wrong path type " + prefix);
#else
    struct stat st;
    requireProject(lstat(prefix.c_str(),&st)==0,"missing " + prefix);
    requireProject(!S_ISLNK(st.st_mode),"symlink " + prefix);
    requireProject(expectDir?S_ISDIR(st.st_mode):S_ISREG(st.st_mode),
                   "wrong path type " + prefix);
#endif
    if(slash==std::string::npos)break;
    start=slash+1;
  }
}
inline std::vector<std::string> projectArrayObjects(const std::string& raw) {
  JsonSyntax(raw).validate();
  requireProject(raw.size()>=2&&raw[0]=='['&&raw[raw.size()-1]==']',
                 "modules is not an array");
  std::vector<std::string> objects;
  std::size_t i=1;
  while(i<raw.size()-1) {
    while(i<raw.size()-1&&(raw[i]==' '||raw[i]=='\n'||raw[i]=='\r'||raw[i]=='\t'))++i;
    if(i==raw.size()-1)break;
    requireProject(raw[i]=='{',"module must be an object");
    const std::size_t start=i;
    int depth=0;bool quoted=false,escaped=false;
    do {
      const char ch=raw[i++];
      if(quoted) {
        if(escaped)escaped=false;
        else if(ch=='\\')escaped=true;
        else if(ch=='"')quoted=false;
      } else if(ch=='"')quoted=true;
      else if(ch=='{'||ch=='[')++depth;
      else if(ch=='}'||ch==']')--depth;
    } while(depth!=0&&i<raw.size()-1);
    requireProject(depth==0,"unclosed module object");
    objects.push_back(raw.substr(start,i-start));
    requireProject(objects.size()<=128,"too many modules");
    while(i<raw.size()-1&&(raw[i]==' '||raw[i]=='\n'||raw[i]=='\r'||raw[i]=='\t'))++i;
    if(i<raw.size()-1) {
      requireProject(raw[i]==',',"missing module separator");
      ++i;
    }
  }
  return objects;
}
inline void validateProjectManifest() {
  projectPath("xytron.json",false);
  const JsonObjectFields root(readProjectFile("xytron.json"));
  requireProject(root.raw("schemaVersion")=="1","unsupported schemaVersion");
  const JsonObjectFields app=root.object("app");
  const std::string appId=app.string("id");
  requireProject(projectIdentifier(appId),"invalid app id");
  requireProject(app.string("mode")=="desktop","unsupported app mode");
  const std::string engine=app.string("engine");
  requireProject(engine=="auto"||engine=="cef"||engine=="webview2",
                 "unsupported engine");
  const JsonObjectFields ui=root.object("ui");
  requireProject(ui.string("entry")=="app://desktop/index.html",
                 "unsupported UI entry");
  requireProject(ui.string("resources")=="ui","unsupported resource root");
  projectPath("ui",true);
  projectPath("ui/index.html",false);
  const JsonObjectFields main=root.object("main");
  requireProject(main.string("id")=="app","unexpected Main id");
  const std::string lang=main.string("language");
  requireProject(projectLanguage(lang),"unsupported Main language");
  const std::string entry=lang=="cpp"?"backend/main.cpp":
    lang=="python"?"backend/main.py":"backend/Program.cs";
  requireProject(main.string("entry")==entry,"Main source path mismatch");
  projectPath(entry,false);
  if(lang=="csharp")projectPath("backend/MainBackend.csproj",false);
  const std::vector<std::string> modules=projectArrayObjects(root.raw("modules"));
  std::set<std::string> ids;
  ids.insert("app");
  for(std::size_t i=0;i<modules.size();++i) {
    const JsonObjectFields spec(modules[i]);
    const std::string id=spec.string("id");
    requireProject(moduleIdentifier(id),"invalid Module id");
    requireProject(ids.insert(id).second,"duplicate Module id");
    const std::string language=spec.string("language");
    requireProject(projectLanguage(language),"unsupported Module language");
    const std::string path="modules/"+id;
    requireProject(spec.string("delivery")=="embedded"&&spec.string("path")==path,
                   "unsupported Module delivery/path");
    projectPath(path,true);
    projectPath(path+"/module.json",false);
    const JsonObjectFields meta(readProjectFile(path+"/module.json"));
    requireProject(meta.raw("schemaVersion")=="1"&&meta.string("id")==id&&
                   meta.string("role")=="feature"&&meta.string("language")==language&&
                   meta.string("delivery")=="embedded", "Module metadata mismatch");
    const std::string source=language=="cpp"?"module.cpp":
      language=="python"?"module.py":"Module.cs";
    projectPath(path+"/"+source,false);
    if(language=="csharp")projectPath(path+"/Module.csproj",false);
  }
}
} // namespace xytron_cli
#endif
