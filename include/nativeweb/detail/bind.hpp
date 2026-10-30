#ifndef NATIVEWEB_DETAIL_BIND_HPP_INCLUDED
#define NATIVEWEB_DETAIL_BIND_HPP_INCLUDED

#include "nativeweb/any.hpp"
#include "nativeweb/detail/callable.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace nativeweb {
namespace detail {

template <std::size_t... Indices>
struct IndexSequence
{
};

template <std::size_t N, std::size_t... Indices>
struct MakeIndexSequence
    : MakeIndexSequence<N - 1, N - 1, Indices...>
{
};

template <std::size_t... Indices>
struct MakeIndexSequence<0, Indices...>
{
    typedef IndexSequence<Indices...> Type;
};

template <typename Signature>
struct FunctionTraits;

template <typename Result, typename... Args>
struct FunctionTraits<Result(Args...)>
{
    typedef Result ResultType;
    typedef std::tuple<Args...> ArgsTuple;

    enum
    {
        Arity = sizeof...(Args)
    };

    template <std::size_t Index>
    struct Arg
    {
        typedef typename std::tuple_element<Index, ArgsTuple>::type Type;
    };
};

template <typename Result, typename... Args>
struct FunctionTraits<Result (*)(Args...)>
    : FunctionTraits<Result(Args...)>
{
};

template <typename Class, typename Result, typename... Args>
struct FunctionTraits<Result (Class::*)(Args...)>
    : FunctionTraits<Result(Args...)>
{
};

template <typename Class, typename Result, typename... Args>
struct FunctionTraits<Result (Class::*)(Args...) const>
    : FunctionTraits<Result(Args...)>
{
};

template <typename Result, typename... Args>
struct FunctionTraits<std::function<Result(Args...)> >
    : FunctionTraits<Result(Args...)>
{
};

template <typename Callable>
struct FunctionTraits
    : FunctionTraits<decltype(&Callable::operator())>
{
};

template <typename Arg>
typename std::decay<Arg>::type readArgument(const Any& value)
{
    typedef typename std::decay<Arg>::type ValueType;
    return AnyCast<ValueType>(value);
}

template <typename Callable, typename Traits, std::size_t... Indices>
typename std::enable_if<
    !std::is_void<typename Traits::ResultType>::value,
    Any
>::type
invokeCallable(
    Callable& callable,
    const VariantList& args,
    IndexSequence<Indices...>)
{
    return Any(callable(
        readArgument<typename Traits::template Arg<Indices>::Type>(
            args[Indices])...));
}

template <typename Callable, typename Traits, std::size_t... Indices>
typename std::enable_if<
    std::is_void<typename Traits::ResultType>::value,
    Any
>::type
invokeCallable(
    Callable& callable,
    const VariantList& args,
    IndexSequence<Indices...>)
{
    callable(
        readArgument<typename Traits::template Arg<Indices>::Type>(
            args[Indices])...);

    return Any();
}

template <typename Class, typename Result, typename... Args>
class BoundMemberFunction
{
public:
    typedef Result (Class::*Method)(Args...);

    BoundMemberFunction(
        const std::shared_ptr<Class>& object,
        Method method)
        : object_(object),
          method_(method)
    {
    }

    Result operator()(Args... args)
    {
        return ((*object_).*method_)(args...);
    }

private:
    std::shared_ptr<Class> object_;
    Method method_;
};

template <typename Class, typename Result, typename... Args>
class BoundConstMemberFunction
{
public:
    typedef Result (Class::*Method)(Args...) const;

    BoundConstMemberFunction(
        const std::shared_ptr<Class>& object,
        Method method)
        : object_(object),
          method_(method)
    {
    }

    Result operator()(Args... args) const
    {
        return ((*object_).*method_)(args...);
    }

private:
    std::shared_ptr<Class> object_;
    Method method_;
};

template <typename Class, typename Result, typename... Args>
BoundMemberFunction<Class, Result, Args...>
bindMember(
    const std::shared_ptr<Class>& object,
    Result (Class::*method)(Args...))
{
    return BoundMemberFunction<Class, Result, Args...>(
        object,
        method);
}

template <typename Class, typename Result, typename... Args>
BoundConstMemberFunction<Class, Result, Args...>
bindMember(
    const std::shared_ptr<Class>& object,
    Result (Class::*method)(Args...) const)
{
    return BoundConstMemberFunction<Class, Result, Args...>(
        object,
        method);
}

template <typename Traits, std::size_t... Indices>
void appendCallableArgumentTypes(
    CallableSignature& signature,
    IndexSequence<Indices...>)
{
    const CallableType values[] = {
        callableType<
            typename Traits::template Arg<Indices>::Type>()...
    };

    signature.arguments.assign(
        values,
        values + sizeof...(Indices));
}

template <typename Traits>
void appendCallableArgumentTypes(
    CallableSignature&,
    IndexSequence<>)
{
}

template <typename CallableValue>
Callable makeCallable(CallableValue callable)
{
    typedef typename std::decay<CallableValue>::type StoredCallable;
    typedef FunctionTraits<StoredCallable> Traits;
    typedef typename MakeIndexSequence<Traits::Arity>::Type Indices;

    const DynamicFunction dynamic =
        [callable](const VariantList& args) mutable -> Any
        {
            if (args.size() != static_cast<std::size_t>(Traits::Arity))
            {
                throw std::runtime_error(
                    "NativeWeb bind argument count mismatch");
            }

            return invokeCallable<CallableValue, Traits>(
                callable,
                args,
                Indices());
        };

    CallableSignature signature;
    signature.result =
        callableType<typename Traits::ResultType>();

    appendCallableArgumentTypes<Traits>(
        signature,
        Indices());

    return Callable(
        dynamic,
        signature);
}

template <typename CallableValue>
DynamicFunction makeDynamicFunction(CallableValue callable)
{
    return makeCallable(callable).dynamicFunction();
}

} // namespace detail
} // namespace nativeweb

#endif
