#ifndef XYTRON_CLI_MANAGED_EXPORT_DISCOVERY_HPP
#define XYTRON_CLI_MANAGED_EXPORT_DISCOVERY_HPP
// Conservative build-time declaration discovery, never a JS-authorized grant.
// Worker ObjectHost additionally enforces @export / [Export] by reflection.
// This scanner deliberately handles explicit Main declarations in generated
// projects; unsupported/ambiguous syntax must fail closed, not grant '*'.
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace xytron_cli {
inline std::string managedTrim(const std::string& line) {
  const std::string::size_type start=line.find_first_not_of(" \t\r");
  if(start==std::string::npos)return std::string();
  const std::string::size_type end=line.find_last_not_of(" \t\r");
  return line.substr(start,end-start+1);
}
inline std::vector<std::string> discoverManagedExports(
    const std::string& language,const std::string& source) {
  if(language!="python" && language!="csharp")
    throw std::runtime_error("Unsupported managed export language");
  std::ifstream input(source.c_str(),std::ios::binary);
  if(!input)throw std::runtime_error("Cannot inspect Main exports");
  std::vector<std::string> operations;
  operations.push_back("app.Main.create");
  operations.push_back("app.Main.dispose");
  std::set<std::string> unique(operations.begin(),operations.end());
  const std::regex name("^[A-Za-z][A-Za-z0-9_]*$");
  const std::regex pythonDef("^def[ \t]+([A-Za-z][A-Za-z0-9_]*)[ \t]*\\(");
  const std::regex csharpMethod("\\bpublic[ \t]+(?:[A-Za-z_][A-Za-z0-9_?<>,]*[ \t]+)+([A-Za-z][A-Za-z0-9_]*)[ \t]*\\(");
  const std::regex csharpProperty("\\bpublic[ \t]+[A-Za-z_][A-Za-z0-9_?<>]*[ \t]+([A-Za-z][A-Za-z0-9_]*)[ \t]*\\{([^}]*)\\}");
  const std::regex csharpMain("\\bclass[ \t]+Main\\b");
  const std::regex pythonMain("^class[ \t]+Main[ \t]*[:(]");
  bool classDecorated=false,inside=false,pending=false,property=false,mainFound=false;
  std::set<std::string> properties;
  std::string line;
  std::size_t bytes=0;
  auto add=[&](const std::string& operation) {
    if(!unique.insert(operation).second)
      throw std::runtime_error("Duplicate managed export: "+operation);
    if(unique.size()>34)
      throw std::runtime_error("Too many exported Main operations");
    operations.push_back(operation);
  };
  while(std::getline(input,line)) {
    bytes+=line.size()+1;
    if(bytes>256*1024)throw std::runtime_error("Managed Main source is too large");
    const std::string trimmed=managedTrim(line);
    if(trimmed.empty()||trimmed.compare(0,1,"#")==0||
       trimmed.compare(0,2,"//")==0)continue;
    if(language=="python") {
      if(!inside) {
        if(trimmed=="@export")classDecorated=true;
        else if(std::regex_search(trimmed,pythonMain)) {
          if(!classDecorated || mainFound)
            throw std::runtime_error("Main must be a unique @export class");
          mainFound=true;inside=true;classDecorated=false;
        } else if(trimmed[0]!='@')classDecorated=false;
        continue;
      }
      // Only declarations indented within Main are eligible. At a
      // top-level declaration leave the class; do not scan other classes.
      if(line[0]!=' '&&line[0]!='\t')break;
      if(trimmed=="@export"){pending=true;continue;}
      if(trimmed=="@property"){property=true;continue;}
      if(trimmed.size()>7 && trimmed[0]=='@') {
        const std::string suffix=".setter";
        const std::size_t pos=trimmed.find(suffix);
        if(pos!=std::string::npos) {
          const std::string prop=trimmed.substr(1,pos-1);
          if(properties.count(prop))add("app.Main.set."+prop);
          pending=false;property=false;continue;
        }
        continue;
      }
      std::smatch match;
      if(std::regex_search(trimmed,match,pythonDef)) {
        const std::string method=match[1];
        if(pending) {
          if(property){
            properties.insert(method);add("app.Main.get."+method);
          } else add("app.Main.method."+method);
        }
        pending=false;property=false;
      } else if(trimmed[0]!='@'){
        pending=false;property=false;
      }
    } else {
      // Inline or preceding [Export] attribute, never a free-standing
      // public method. The actual ObjectHost reflection is final authority.
      if(trimmed.find("[Export]")!=std::string::npos)pending=true;
      if(!inside){
        if(std::regex_search(trimmed,csharpMain)) {
          if(!pending||mainFound)
            throw std::runtime_error("Main must be a unique [Export] class");
          inside=true;mainFound=true;pending=false;
        } else if(trimmed.find("class ")!=std::string::npos)pending=false;
        continue;
      }
      if(trimmed.find("class ")!=std::string::npos &&
         trimmed.find("class Main")==std::string::npos)break;
      std::smatch match;
      if(pending && std::regex_search(trimmed,match,csharpProperty)) {
        const std::string member=match[1];
        const std::string access=match[2];
        if(access.find("get;")!=std::string::npos)add("app.Main.get."+member);
        if(access.find("set;")!=std::string::npos)add("app.Main.set."+member);
        pending=false;
      } else if(pending&&std::regex_search(trimmed,match,csharpMethod)) {
        add("app.Main.method."+match[1].str());pending=false;
      } else if(trimmed.find("public ")!=std::string::npos && pending) {
        throw std::runtime_error("Unsupported [Export] declaration in Main");
      }
    }
  }
  if(!input.eof()&&input.fail())
    throw std::runtime_error("Cannot read managed Main declarations");
  if(!mainFound||operations.size()<=2)
    throw std::runtime_error("No discoverable exported Main methods");
  std::sort(operations.begin()+2,operations.end());
  return operations;
}
} // namespace xytron_cli
#endif
