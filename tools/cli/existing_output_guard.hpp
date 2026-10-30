#ifndef XYTRON_CLI_EXISTING_OUTPUT_GUARD_HPP
#define XYTRON_CLI_EXISTING_OUTPUT_GUARD_HPP

// Refuse writing through symlinks/reparse points and hard links. This protects
// the legacy CLI module-add manifest rewrite; it is not an atomic filesystem
// transaction and does not address concurrent local filesystem attackers.
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace xytron_cli {
inline void checkedExistingOutput(const std::string& path) {
  if(path.empty() || path.size()>127 || path=="." || path=="..")
    throw std::runtime_error("Invalid CLI output name");
  for(std::size_t i=0;i<path.size();++i) {
    const char c=path[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
         (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))
      throw std::runtime_error("Unsafe CLI output name");
  }
#ifdef _WIN32
  const DWORD attrs=GetFileAttributesA(path.c_str());
  if(attrs==INVALID_FILE_ATTRIBUTES ||
     (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    throw std::runtime_error("Unsafe or missing CLI output: "+path);
  HANDLE handle=CreateFileA(path.c_str(),FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
      OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(handle==INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot inspect CLI output: "+path);
  BY_HANDLE_FILE_INFORMATION info={};
  const BOOL inspected=GetFileInformationByHandle(handle,&info);
  CloseHandle(handle);
  if(!inspected || info.nNumberOfLinks!=1)
    throw std::runtime_error("Hard-linked or unreadable CLI output: "+path);
#else
  struct stat st;
  if(lstat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_nlink!=1)
    throw std::runtime_error("Unsafe, hard-linked or missing CLI output: "+path);
#endif
}
} // namespace xytron_cli
#endif
