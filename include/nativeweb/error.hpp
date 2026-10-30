#ifndef NATIVEWEB_ERROR_HPP_INCLUDED
#define NATIVEWEB_ERROR_HPP_INCLUDED

#include <stdexcept>
#include <string>

namespace nativeweb {

class Error : public std::runtime_error
{
public:
    Error(const std::string& code, const std::string& message)
        : std::runtime_error(message),
          code_(code)
    {
    }

    const std::string& code() const
    {
        return code_;
    }

private:
    std::string code_;
};

} // namespace nativeweb

#endif
