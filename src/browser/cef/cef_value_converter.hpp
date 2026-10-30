#ifndef NATIVEWEB_CEF_VALUE_CONVERTER_HPP_INCLUDED
#define NATIVEWEB_CEF_VALUE_CONVERTER_HPP_INCLUDED

#include "include/cef_values.h"
#include "include/cef_v8.h"
#include "nativeweb/any.hpp"

namespace nativeweb {
namespace detail {

Any cefValueToAny(CefRefPtr<CefValue> value);
CefRefPtr<CefValue> anyToCefValue(const Any& value);

CefRefPtr<CefValue> v8ToCefValue(CefRefPtr<CefV8Value> value);
CefRefPtr<CefV8Value> cefValueToV8(CefRefPtr<CefValue> value);

} // namespace detail
} // namespace nativeweb

#endif
