#pragma once

#include <stdexcept>
#include <string>
#include <typeinfo>

#if defined(__GNUG__)
#include <cxxabi.h>
inline std::string demangle(const char *name)
{
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = (status == 0 && demangled) ? demangled : name;
    free(demangled);
    return result;
}
#else
inline std::string demangle(const char *name)
{
    return name; // fallback (MSVC or no demangling available)
}
#endif

#define THROW_IF_MISSING(cond, field)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            throw std::runtime_error(demangle(typeid(*this).name()) + " " + field +                                    \
                                     " is required or unsupported value");                                             \
        }                                                                                                              \
    } while (0)
