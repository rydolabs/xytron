#ifndef NATIVEWEB_DETAIL_CALLABLE_HPP_INCLUDED
#define NATIVEWEB_DETAIL_CALLABLE_HPP_INCLUDED

#include "nativeweb/any.hpp"
#include "nativeweb/types.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace nativeweb {

typedef std::function<Any(const VariantList&)> DynamicFunction;

namespace detail {

enum class CallableTypeKind
{
    Unknown,
    Void,
    Boolean,
    Integer,
    Floating,
    String,
    List,
    Dictionary,
    Binary,
    NativeObject
};

struct CallableType
{
    CallableType()
        : kind(CallableTypeKind::Unknown),
          name("unknown")
    {
    }

    CallableType(
        CallableTypeKind valueKind,
        const std::string& valueName)
        : kind(valueKind),
          name(valueName)
    {
    }

    CallableTypeKind kind;
    std::string name;
};

struct CallableSignature
{
    CallableType result;
    std::vector<CallableType> arguments;

    std::size_t arity() const
    {
        return arguments.size();
    }
};

template <typename T>
struct CallableTypeResolver
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Unknown,
            "opaque");
    }
};

template <>
struct CallableTypeResolver<void>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Void,
            "void");
    }
};

template <>
struct CallableTypeResolver<bool>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Boolean,
            "boolean");
    }
};

#define NATIVEWEB_INTEGER_TYPE(Type)            \
template <>                                     \
struct CallableTypeResolver<Type>               \
{                                               \
    static CallableType get()                   \
    {                                           \
        return CallableType(                    \
            CallableTypeKind::Integer,          \
            "integer");                         \
    }                                           \
}

NATIVEWEB_INTEGER_TYPE(char);
NATIVEWEB_INTEGER_TYPE(signed char);
NATIVEWEB_INTEGER_TYPE(unsigned char);
NATIVEWEB_INTEGER_TYPE(short);
NATIVEWEB_INTEGER_TYPE(unsigned short);
NATIVEWEB_INTEGER_TYPE(int);
NATIVEWEB_INTEGER_TYPE(unsigned int);
NATIVEWEB_INTEGER_TYPE(long);
NATIVEWEB_INTEGER_TYPE(unsigned long);
NATIVEWEB_INTEGER_TYPE(long long);
NATIVEWEB_INTEGER_TYPE(unsigned long long);

#undef NATIVEWEB_INTEGER_TYPE

template <>
struct CallableTypeResolver<float>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Floating,
            "number");
    }
};

template <>
struct CallableTypeResolver<double>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Floating,
            "number");
    }
};

template <>
struct CallableTypeResolver<std::string>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::String,
            "string");
    }
};

template <>
struct CallableTypeResolver<VariantList>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::List,
            "array");
    }
};

template <>
struct CallableTypeResolver<VariantDict>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Dictionary,
            "object");
    }
};

template <>
struct CallableTypeResolver<Binary>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::Binary,
            "binary");
    }
};

template <>
struct CallableTypeResolver<NativeObjectHandle>
{
    static CallableType get()
    {
        return CallableType(
            CallableTypeKind::NativeObject,
            "native_object");
    }
};

template <typename T>
CallableType callableType()
{
    typedef typename std::decay<T>::type ValueType;
    return CallableTypeResolver<ValueType>::get();
}

class Callable
{
public:
    Callable()
    {
    }

    explicit Callable(
        const DynamicFunction& function)
        : function_(function)
    {
    }

    Callable(
        const DynamicFunction& function,
        const CallableSignature& signature)
        : function_(function),
          signature_(signature)
    {
    }

    Callable(
        const DynamicFunction& function,
        const CallableSignature& signature,
        const std::shared_ptr<void>& lifetime)
        : function_(function),
          signature_(signature),
          lifetime_(lifetime)
    {
    }

    Any invoke(const VariantList& args) const
    {
        return function_(args);
    }

    const CallableSignature& signature() const
    {
        return signature_;
    }

    bool valid() const
    {
        return static_cast<bool>(function_);
    }

    operator bool() const
    {
        return valid();
    }

    const DynamicFunction& dynamicFunction() const
    {
        return function_;
    }

    const std::shared_ptr<void>& lifetime() const
    {
        return lifetime_;
    }

private:
    DynamicFunction function_;
    CallableSignature signature_;
    std::shared_ptr<void> lifetime_;
};

} // namespace detail
} // namespace nativeweb

#endif
