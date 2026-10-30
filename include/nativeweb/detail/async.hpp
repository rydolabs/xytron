#ifndef NATIVEWEB_DETAIL_ASYNC_HPP_INCLUDED
#define NATIVEWEB_DETAIL_ASYNC_HPP_INCLUDED

#include "nativeweb/any.hpp"

#include <cstddef>

namespace nativeweb {
namespace detail {

inline Any toAny(const char* value)
{
    return Any(value);
}

template <std::size_t N>
Any toAny(const char (&value)[N])
{
    return Any(static_cast<const char*>(value));
}

template <typename T>
Any toAny(const T& value)
{
    return Any(value);
}

inline void appendArguments(VariantList&)
{
}

template <typename First, typename... Rest>
void appendArguments(
    VariantList& output,
    const First& first,
    const Rest&... rest)
{
    output.push_back(toAny(first));
    appendArguments(output, rest...);
}

} // namespace detail
} // namespace nativeweb

#endif
