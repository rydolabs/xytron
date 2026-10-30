#ifndef NATIVEWEB_TRUSTED_DOCUMENT_HPP_INCLUDED
#define NATIVEWEB_TRUSTED_DOCUMENT_HPP_INCLUDED
#include <string>
#include "nativeweb/app_resources.hpp"

namespace nativeweb {
namespace detail {

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
    // Backward-compatible exact local file entry, NOT arbitrary file URLs.
    return entry.size() > 8 &&
           entry.compare(0, 8, "file:///") == 0 &&
           entry.find('#') == std::string::npos &&
           entry.find('?') == std::string::npos &&
           entry.find('\0') == std::string::npos;
}

} // namespace detail
} // namespace nativeweb
#endif
