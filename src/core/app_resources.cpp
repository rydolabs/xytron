#include "nativeweb/app_resources.hpp"
#include "nativeweb/error.hpp"

namespace nativeweb {
namespace {
bool alpha(char c) { return c >= 'a' && c <= 'z'; }
bool number(char c) { return c >= '0' && c <= '9'; }
bool hostChar(char c) { return alpha(c) || number(c) || c == '-'; }
bool pathChar(char c)
{
    return alpha(c) || (c >= 'A' && c <= 'Z') || number(c) ||
           c == '-' || c == '_' || c == '.' || c == '/';
}
bool safeMime(const std::string& m)
{
    if (m.empty() || m.size() > 100) return false;
    const std::size_t slash = m.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash == m.size() - 1 || m.find('/', slash + 1) != std::string::npos)
        return false;
    for (std::size_t i=0; i<m.size(); ++i) {
        const char c=m[i];
        if (c == '/') continue;
        if (!(alpha(c) || number(c) || c == '-' || c == '+' || c == '.'))
            return false;
    }
    return true;
}
} // namespace

bool AppResourceBundle::isCanonicalUrl(const std::string& url)
{
    const std::string scheme = "app://";
    if (url.compare(0, scheme.size(), scheme) != 0 || url.size() > 2048)
        return false;
    const std::size_t firstSlash = url.find('/', scheme.size());
    if (firstSlash == std::string::npos) return false;
    const std::string host =
        url.substr(scheme.size(), firstSlash - scheme.size());
    if (host.empty() || host.size() > 63 || !alpha(host[0]) ||
        !hostChar(host[host.size()-1])) return false;
    for (std::size_t i=1; i<host.size(); ++i)
        if (!hostChar(host[i])) return false;
    const std::string path = url.substr(firstSlash);
    if (path.size() < 2 || path[1] == '/' ||
        path[path.size()-1] == '/') return false;
    std::size_t start=1;
    while (start < path.size()) {
        const std::size_t end=path.find('/', start);
        const std::string segment=path.substr(
            start, end == std::string::npos ? std::string::npos : end-start);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        for (std::size_t i=0; i<segment.size(); ++i)
            if (!pathChar(segment[i])) return false;
        if (end == std::string::npos) break;
        start=end+1;
    }
    return true;
}

void AppResourceBundle::add(const std::string& url,
                            const std::string& mimeType,
                            const std::string& bytes)
{
    if (!isCanonicalUrl(url))
        throw Error("invalid_app_resource_url", "Invalid app resource URL: " + url);
    if (!safeMime(mimeType))
        throw Error("invalid_app_resource_mime", "Invalid app resource MIME type");
    if (bytes.size() > 16u*1024u*1024u)
        throw Error("app_resource_too_large", "App resource exceeds 16 MiB");
    const std::size_t maxBundleSize = 64u*1024u*1024u;
    if (bytes.size() > maxBundleSize-totalBytes_)
        throw Error("app_bundle_too_large", "App bundle exceeds 64 MiB");
    if (resources_.size() >= 4096u)
        throw Error("app_resource_limit", "Too many app resources");
    if (resources_.count(url))
        throw Error("duplicate_app_resource", "Duplicate app resource: " + url);
    AppResource resource;
    resource.mimeType=mimeType;
    resource.bytes=bytes;
    resources_[url]=resource;
    totalBytes_ += bytes.size();
}

bool AppResourceBundle::find(
    const std::string& url, AppResource* result) const
{
    if (!result || !isCanonicalUrl(url)) return false;
    const std::map<std::string,AppResource>::const_iterator it =
        resources_.find(url);
    if (it == resources_.end()) return false;
    *result=it->second;
    return true;
}

bool AppResourceBundle::contains(const std::string& url) const
{
    return isCanonicalUrl(url) && resources_.count(url) != 0;
}

std::size_t AppResourceBundle::size() const { return resources_.size(); }

} // namespace nativeweb
