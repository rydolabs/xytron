#ifndef NATIVEWEB_TRUSTED_DOCUMENT_HPP_INCLUDED
#define NATIVEWEB_TRUSTED_DOCUMENT_HPP_INCLUDED
#include <string>
#include "nativeweb/app_resources.hpp"

namespace nativeweb {
namespace detail {

inline int hexNibble(char ch) {
    if(ch >= '0' && ch <= '9') return ch-'0';
    if(ch >= 'a' && ch <= 'f') return ch-'a'+10;
    if(ch >= 'A' && ch <= 'F') return ch-'A'+10;
    return -1;
}
// File URL trust is for an EXACT configured entry document. Canonicalize
// lexically before trusting it, so encoded separators and dot segments never
// become aliases after the engine normalizes the navigation.
inline bool canonicalLocalFileEntry(const std::string& entry) {
    if(entry.size() <= 8 || entry.compare(0,8,"file:///") != 0)
        return false;
    const std::string path=entry.substr(8);
    if(path.empty() || path[0]=='/' || path[path.size()-1]=='/')
        return false;
    std::size_t segment=0;
    for(std::size_t i=0;i<path.size();++i) {
        const unsigned char ch=static_cast<unsigned char>(path[i]);
        if(ch<0x20 || ch==0x7f || ch=='\\' || ch=='?' || ch=='#')
            return false;
        if(ch=='%') {
            if(i+2>=path.size()) return false;
            const int hi=hexNibble(path[i+1]);
            const int lo=hexNibble(path[i+2]);
            if(hi<0 || lo<0) return false;
            const int decoded=(hi<<4)|lo;
            if(decoded<0x20 || decoded==0x7f || decoded=='.' ||
               decoded=='/' || decoded=='\\' || decoded=='%' ||
               decoded=='?' || decoded=='#')
                return false;
            i+=2;
        } else if(ch=='/') {
            const std::string part=path.substr(segment,i-segment);
            if(part.empty() || part=="." || part=="..") return false;
            segment=i+1;
        }
    }
    const std::string last=path.substr(segment);
    return !last.empty() && last!="." && last!="..";
}

// First safe-by-default bridge boundary. The host entry must be a local
// file document, not a remote site, data URL, or arbitrary file navigation.
// This is deliberately exact: root-based grants will require a separate
// explicit, canonicalized trusted-app-root policy.
inline bool trustedEntryDocument(const std::string& entry,
                                 const std::string& actual,
                                 bool mainFrame)
{
    if (!mainFrame || entry != actual)
        return false;
    if (AppResourceBundle::isCanonicalUrl(entry))
        return true;
    // Legacy local file entries remain supported, but not aliases.
    return canonicalLocalFileEntry(entry);
}

} // namespace detail
} // namespace nativeweb
#endif
