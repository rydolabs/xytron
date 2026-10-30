#ifndef XYTRON_CLI_FILE_TREE_HPP
#define XYTRON_CLI_FILE_TREE_HPP
// Enumerate files in the generated plugin's source/build subtree. Reject
// symlinks/reparse points so packaging cannot escape the project directory.
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
namespace xytron_cli {
inline bool skipBuildArtifact(const std::string& name) {
  return name=="__pycache__" || name==".pytest_cache" ||
    (name.size()>=4 && name.substr(name.size()-4)==".pyc");
}
inline void sourceFiles(const std::string& base,const std::string& relative,
                        std::vector<std::string>& files,int depth=0) {
  if(depth>16 || files.size()>1024) throw std::runtime_error("Too many plugin files");
  const std::string folder=base+(relative.empty()?"":"/"+relative);
#ifdef _WIN32
  WIN32_FIND_DATAA item;
  HANDLE handle=FindFirstFileA((folder+"/*").c_str(),&item);
  if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot list: "+folder);
  do {
    const std::string name=item.cFileName;
    if(name=="."||name==".."||skipBuildArtifact(name))continue;
    if(item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      throw std::runtime_error("Symlink/reparse point not allowed: "+name);
    const std::string path=relative.empty()?name:relative+"/"+name;
    if(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      sourceFiles(base,path,files,depth+1);
    else files.push_back(path);
  } while(FindNextFileA(handle,&item));
  FindClose(handle);
#else
  DIR* dir=opendir(folder.c_str());
  if(!dir) throw std::runtime_error("Cannot list: "+folder);
  struct dirent* entry;
  while((entry=readdir(dir))!=NULL) {
    const std::string name=entry->d_name;
    if(name=="."||name==".."||skipBuildArtifact(name))continue;
    const std::string path=relative.empty()?name:relative+"/"+name;
    struct stat st;
    if(lstat((base+"/"+path).c_str(),&st)!=0){closedir(dir);throw std::runtime_error("Cannot stat: "+path);}
    if(S_ISLNK(st.st_mode)){closedir(dir);throw std::runtime_error("Symlink forbidden: "+path);}
    if(S_ISDIR(st.st_mode))sourceFiles(base,path,files,depth+1);
    else if(S_ISREG(st.st_mode))files.push_back(path);
  }
  closedir(dir);
#endif
  if(files.size()>1024) throw std::runtime_error("Too many plugin files");
  std::sort(files.begin(),files.end());
}
} // namespace xytron_cli
#endif
