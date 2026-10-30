#ifndef NATIVEWEB_PENDING_RESULT_HPP_INCLUDED
#define NATIVEWEB_PENDING_RESULT_HPP_INCLUDED

#include "nativeweb/any.hpp"
#include "nativeweb/error.hpp"

#include <exception>
#include <future>
#include <string>

namespace nativeweb {
namespace detail {

class PendingResultBase
{
public:
    virtual ~PendingResultBase()
    {
    }

    virtual void resolve(const Any& value) = 0;
    virtual void reject(const Error& error) = 0;
};

template <typename Result>
class PendingResult : public PendingResultBase
{
public:
    std::future<Result> future()
    {
        return promise_.get_future();
    }

    void resolve(const Any& value) override
    {
        try
        {
            promise_.set_value(
                AnyCast<Result>(value));
        }
        catch (const std::exception& error)
        {
            promise_.set_exception(
                std::make_exception_ptr(
                    Error(
                        "result_type_mismatch",
                        std::string(
                            "NativeWeb result conversion failed: ") +
                            error.what())));
        }
        catch (...)
        {
            promise_.set_exception(
                std::make_exception_ptr(
                    Error(
                        "result_type_mismatch",
                        "NativeWeb result conversion failed")));
        }
    }

    void reject(const Error& error) override
    {
        promise_.set_exception(
            std::make_exception_ptr(error));
    }

private:
    std::promise<Result> promise_;
};

template <>
class PendingResult<Any> : public PendingResultBase
{
public:
    std::future<Any> future()
    {
        return promise_.get_future();
    }

    void resolve(const Any& value) override
    {
        promise_.set_value(value);
    }

    void reject(const Error& error) override
    {
        promise_.set_exception(
            std::make_exception_ptr(error));
    }

private:
    std::promise<Any> promise_;
};

template <>
class PendingResult<void> : public PendingResultBase
{
public:
    std::future<void> future()
    {
        return promise_.get_future();
    }

    void resolve(const Any&) override
    {
        promise_.set_value();
    }

    void reject(const Error& error) override
    {
        promise_.set_exception(
            std::make_exception_ptr(error));
    }

private:
    std::promise<void> promise_;
};

} // namespace detail
} // namespace nativeweb

#endif
