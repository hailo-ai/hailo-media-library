/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include "media_library_types.hpp"
#include <memory>
#include <cstdlib>
#include <string>
#include <sstream>
#include <tl/expected.hpp>
#include <stdexcept> // For std::invalid_argument, std::out_of_range

// this macro is used to create a static initializer for a function that should
// be called before main program starts
// https://stackoverflow.com/questions/1113409/attribute-constructor-equivalent-in-vc
#define COMPAT__INITIALIZER(f)                                                                                         \
    static void f(void);                                                                                               \
    struct f##_t_                                                                                                      \
    {                                                                                                                  \
        f##_t_(void)                                                                                                   \
        {                                                                                                              \
            f();                                                                                                       \
        }                                                                                                              \
    };                                                                                                                 \
    static f##_t_ f##_;                                                                                                \
    static void f(void)

// From
// https://stackoverflow.com/questions/57092289/do-stdmake-shared-and-stdmake-unique-have-a-nothrow-version
template <class T, class... Args>
static inline std::unique_ptr<T> make_unique_nothrow(Args &&...args) noexcept(noexcept(T(std::forward<Args>(args)...)))
{
    auto ptr = std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
    if (nullptr == ptr)
    {
        LOGGER__ERROR("make_unique failed, pointer is null!");
    }
    return ptr;
}

template <class T, class... Args>
static inline std::shared_ptr<T> make_shared_nothrow(Args &&...args) noexcept(noexcept(T(std::forward<Args>(args)...)))
{
    auto ptr = std::shared_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
    if (nullptr == ptr)
    {
        LOGGER__ERROR("make_shared failed, pointer is null!");
    }
    return ptr;
}

static inline bool is_env_variable_on(const std::string &env_var_name, const std::string &required_value = "1")
{
    auto env_var = std::getenv(env_var_name.c_str());
    return ((nullptr != env_var) && (required_value == env_var));
}

template <typename T> static inline tl::expected<T, media_library_return> get_env_variable(const std::string &var_name)
{
    const char *val = std::getenv(var_name.c_str());
    if (!val)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    std::string str_val(val);
    T result;
    if constexpr (std::is_same_v<T, std::string>)
    {
        return T(str_val); // Direct return for strings
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        // Interpret common boolean values
        if (str_val == "1" || str_val == "true" || str_val == "yes")
            return true;
        if (str_val == "0" || str_val == "false" || str_val == "no")
            return false;
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }
    else
    {
        std::istringstream iss(str_val);
        if (iss >> result)
            return T(result); // Convert for int, double, etc.
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }
}
