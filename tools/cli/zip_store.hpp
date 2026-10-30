#ifndef XYTRON_CLI_ZIP_STORE_HPP
#define XYTRON_CLI_ZIP_STORE_HPP

// Minimal portable C++11 ZIP writer (stored entries only; no Zip64).
// Archives are generated from an explicit allowlist, never by recursively
// zipping an untrusted directory. ZIP timestamps are fixed for reproducibility.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace xytron_cli {
struct ZipEntry {
  std::string name;
  std::string body;
  std::uint32_t crc;
  std::uint32_t offset;
};
inline void put16(std::ostream& o, std::uint16_t n) {
  o.put(static_cast<char>(n & 0xff)); o.put(static_cast<char>(n >> 8));
}
inline void put32(std::ostream& o, std::uint32_t n) {
  put16(o, static_cast<std::uint16_t>(n & 0xffff));
  put16(o, static_cast<std::uint16_t>(n >> 16));
}
inline std::uint32_t crc32(const std::string& body) {
  std::uint32_t crc = 0xffffffffu;
  for(std::size_t i=0;i<body.size();++i) {
    crc ^= static_cast<unsigned char>(body[i]);
    for(int j=0;j<8;++j)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}
class ZipStore {
public:
  void add(const std::string& name,const std::string& source) {
    if(name.empty() || name.size()>65535 || name[0]=='/' ||
       name.find("..")!=std::string::npos || name.find('\\')!=std::string::npos ||
       entries_.size()>=1024)
      throw std::runtime_error("Invalid ZIP entry name");
    for(std::size_t i=0;i<entries_.size();++i)
      if(entries_[i].name==name) throw std::runtime_error("Duplicate ZIP entry");
    // Explicit manifest / binary paths must also reject links; recursive source
    // discovery alone does not protect files passed directly to ZipStore::add.
#ifdef _WIN32
    const DWORD attrs=GetFileAttributesA(source.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
      throw std::runtime_error("Unsafe package input: "+source);
#else
    struct stat mode;
    if(lstat(source.c_str(),&mode)!=0 || !S_ISREG(mode.st_mode))
      throw std::runtime_error("Unsafe package input: "+source);
#endif
    std::ifstream in(source.c_str(),std::ios::binary);
    if(!in) throw std::runtime_error("Missing package input: "+source);
    in.seekg(0,std::ios::end);
    const std::streamoff size=in.tellg();
    if(size<0 || size>32*1024*1024)
      throw std::runtime_error("Invalid or oversized package input: "+source);
    in.seekg(0,std::ios::beg);
    ZipEntry e; e.name=name;
    e.body.resize(static_cast<std::size_t>(size));
    if(size && !in.read(&e.body[0],size))
      throw std::runtime_error("Failed to read package input: "+source);
    e.crc=crc32(e.body);e.offset=0;entries_.push_back(e);
  }
  void write(const std::string& path) {
    std::uint64_t bytes=0;
    for(std::size_t i=0;i<entries_.size();++i)
      bytes+=30+entries_[i].name.size()+entries_[i].body.size()+46+entries_[i].name.size();
    if(bytes>64*1024*1024 || entries_.empty())
      throw std::runtime_error("Empty or oversized package");
    const std::string tmp=path+".tmp";
    std::ofstream o(tmp.c_str(),std::ios::binary|std::ios::trunc);
    if(!o) throw std::runtime_error("Cannot create package: "+tmp);
    for(std::size_t i=0;i<entries_.size();++i) {
      ZipEntry& e=entries_[i];
      e.offset=static_cast<std::uint32_t>(o.tellp());
      put32(o,0x04034b50u);put16(o,20);put16(o,2048);put16(o,0);
      put16(o,0);put16(o,0);put32(o,e.crc);
      put32(o,static_cast<std::uint32_t>(e.body.size()));
      put32(o,static_cast<std::uint32_t>(e.body.size()));
      put16(o,static_cast<std::uint16_t>(e.name.size()));put16(o,0);
      o.write(e.name.data(),e.name.size());o.write(e.body.data(),e.body.size());
    }
    const std::uint32_t start=static_cast<std::uint32_t>(o.tellp());
    for(std::size_t i=0;i<entries_.size();++i) {
      const ZipEntry& e=entries_[i];
      put32(o,0x02014b50u);put16(o,20);put16(o,20);
      put16(o,2048);put16(o,0);put16(o,0);put16(o,0);
      put32(o,e.crc);
      put32(o,static_cast<std::uint32_t>(e.body.size()));
      put32(o,static_cast<std::uint32_t>(e.body.size()));
      put16(o,static_cast<std::uint16_t>(e.name.size()));
      put16(o,0);put16(o,0);put16(o,0);put16(o,0);put32(o,0);put32(o,e.offset);
      o.write(e.name.data(),e.name.size());
    }
    const std::uint32_t end=static_cast<std::uint32_t>(o.tellp());
    put32(o,0x06054b50u);put16(o,0);put16(o,0);
    put16(o,static_cast<std::uint16_t>(entries_.size()));
    put16(o,static_cast<std::uint16_t>(entries_.size()));
    put32(o,end-start);put32(o,start);put16(o,0);
    o.close();
    if(!o) throw std::runtime_error("Package write failed: "+tmp);
    // Fail closed if a previous output exists; never silently overwrite a release.
    std::ifstream previous(path.c_str(),std::ios::binary);
    if(previous) { std::remove(tmp.c_str());throw std::runtime_error("Package already exists: "+path); }
    if(std::rename(tmp.c_str(),path.c_str())!=0) {
      std::remove(tmp.c_str());throw std::runtime_error("Cannot finalize package: "+path);
    }
  }
private:
  std::vector<ZipEntry> entries_;
};
} // namespace xytron_cli
#endif
