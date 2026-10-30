#ifndef XYTRON_CLI_OFFLINE_FILE_SAFETY_HPP
#define XYTRON_CLI_OFFLINE_FILE_SAFETY_HPP
// Offline output-path guard; does not protect against concurrent local
// filesystem attackers (not a transactional sandbox).
#include "project_manifest.hpp"
#include <cerrno>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace xytron_cli {
inline void checkedProjectOutput(const std::string& path, bool requireAbsent) {
  const std::size_t slash=path.rfind('/');
  requireProject(slash!=std::string::npos&&slash>0&&slash+1<path.size(),
                 "output requires a project-local directory");
  const std::string parent=path.substr(0,slash);
  const std::string leaf=path.substr(slash+1);
  requireProject(leaf!="."&&leaf!=".."&&leaf.size()<128,"invalid output name");
  for(std::size_t i=0;i<leaf.size();++i) {
    const char c=leaf[i];
    requireProject((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
                   (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.',
                   "unsafe output name");
  }
  projectPath(parent,true);
#ifdef _WIN32
  const DWORD attrs=GetFileAttributesA(path.c_str());
  if(attrs==INVALID_FILE_ATTRIBUTES) {
    WIN32_FIND_DATAA found={};
    HANDLE handle=FindFirstFileA(path.c_str(),&found);
    if(handle!=INVALID_HANDLE_VALUE) {
      FindClose(handle);
      throw std::runtime_error("Invalid output path or reparse point: "+path);
    }
    const DWORD error=GetLastError();
    requireProject(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,
                   "cannot inspect output path");
  } else {
    requireProject(!requireAbsent,"output already exists: "+path);
    requireProject(!(attrs&FILE_ATTRIBUTE_REPARSE_POINT)&&
                   !(attrs&FILE_ATTRIBUTE_DIRECTORY),"unsafe output object: "+path);
    // A hard link may overwrite another file when the output is truncated.
    HANDLE handle=CreateFileA(path.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    requireProject(handle!=INVALID_HANDLE_VALUE,"cannot inspect output links: "+path);
    BY_HANDLE_FILE_INFORMATION info={};
    const BOOL inspected=GetFileInformationByHandle(handle,&info);
    CloseHandle(handle);
    requireProject(inspected!=0&&info.nNumberOfLinks==1,
                   "hard-linked or unreadable output: "+path);
  }
#else
  struct stat st;
  if(lstat(path.c_str(),&st)==0) {
    requireProject(!requireAbsent,"output already exists: "+path);
    requireProject(S_ISREG(st.st_mode),"unsafe output object: "+path);
    requireProject(st.st_nlink==1,"hard-linked output: "+path);
  } else {
    requireProject(errno==ENOENT,"cannot inspect output path: "+path);
  }
#endif
}
} // namespace xytron_cli
#endif
