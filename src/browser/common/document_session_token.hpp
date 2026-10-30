#ifndef NATIVEWEB_DOCUMENT_SESSION_TOKEN_HPP_INCLUDED
#define NATIVEWEB_DOCUMENT_SESSION_TOKEN_HPP_INCLUDED

#include <cstddef>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nativeweb {
namespace detail {

// An unpredictable per-navigation bearer token. Kept outside the JS-request
// parser and rotated on native main-frame navigation. Never use mt19937/time.
inline std::string createDocumentSessionToken()
{
    unsigned char bytes[24] = {};
#if defined(_WIN32)
    const NTSTATUS status = BCryptGenRandom(
        NULL, bytes, static_cast<ULONG>(sizeof(bytes)),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0)
        throw std::runtime_error("Cannot acquire document session entropy");
#else
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int fd = open("/dev/urandom", flags);
    if (fd < 0)
        throw std::runtime_error("Cannot open document session entropy");
    std::size_t total = 0;
    while (total < sizeof(bytes)) {
        const ssize_t n = read(fd, bytes + total, sizeof(bytes) - total);
        if (n > 0) total += static_cast<std::size_t>(n);
        else if (n < 0 && errno == EINTR) continue;
        else { close(fd); throw std::runtime_error("Document entropy read failed"); }
    }
    close(fd);
#endif
    static const char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(sizeof(bytes) * 2);
    for (std::size_t i = 0; i < sizeof(bytes); ++i) {
        token += hex[bytes[i] >> 4];
        token += hex[bytes[i] & 15];
    }
    return token;
}

// Reject missing and different tokens without short-circuiting by prefix.
inline bool matchesDocumentSessionToken(const std::string& received,
                                        const std::string& active)
{
    if (active.size() != 48 || received.size() != 48)
        return false;
    unsigned char difference = 0;
    for (std::size_t i=0; i<48; ++i)
        difference |= static_cast<unsigned char>(active[i] ^ received[i]);
    return difference == 0;
}

} // namespace detail
} // namespace nativeweb
#endif
