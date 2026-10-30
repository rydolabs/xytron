#ifndef NATIVEWEB_JSON_CODEC_HPP_INCLUDED
#define NATIVEWEB_JSON_CODEC_HPP_INCLUDED

#include "nativeweb/any.hpp"

#include <string>

namespace nativeweb {
namespace detail {

std::string anyToJson(const Any& value);
// WebView2-only wire framing: base64 instead of numeric-byte JSON arrays.
// Existing anyToJson remains byte-for-byte compatible with broker clients.
std::string anyToJsonCompactBinary(const Any& value);
Any jsonToAny(const std::string& json);

} // namespace detail
} // namespace nativeweb

#endif
