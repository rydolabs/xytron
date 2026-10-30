#ifndef XYTRON_TYPES_HPP_INCLUDED
#define XYTRON_TYPES_HPP_INCLUDED

// Public Xytron spellings are exact C++ type aliases, not new runtime types.
#include "nativeweb/any.hpp"
#include "nativeweb/types.hpp"

namespace xytron {
using ::nativeweb::Any;
using ::nativeweb::Variant;
using ::nativeweb::VariantDict;
using ::nativeweb::VariantList;
using ::nativeweb::Binary;
using ::nativeweb::ByteArray;
using ::nativeweb::NativeObjectHandle;
using ::nativeweb::NativeWindowHandle;
using ::nativeweb::Engine;
using ::nativeweb::Capability;
using ::nativeweb::Capabilities;
using ::nativeweb::WebViewOptions;
} // namespace xytron

#endif
