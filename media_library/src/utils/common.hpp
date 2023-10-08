#pragma once
#include <memory>

// this macro is used to create a static initializer for a function that should be called before main program starts
// https://stackoverflow.com/questions/1113409/attribute-constructor-equivalent-in-vc
#define COMPAT__INITIALIZER(f) \
    static void f(void);       \
    struct f##_t_              \
    {                          \
        f##_t_(void) { f(); }  \
    };                         \
    static f##_t_ f##_;        \
    static void f(void)

// From https://stackoverflow.com/questions/57092289/do-stdmake-shared-and-stdmake-unique-have-a-nothrow-version
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
