#pragma once

#ifndef TOMLKIT11_CONFIG_HPP
#define TOMLKIT11_CONFIG_HPP

#include <cstddef>
#include <cstdlib>

#if defined(_MSVC_LANG) && (_MSVC_LANG > __cplusplus)
#    define TOMLKIT11_CPLUSPLUS _MSVC_LANG
#else
#    define TOMLKIT11_CPLUSPLUS __cplusplus
#endif

#if TOMLKIT11_CPLUSPLUS >= 201402L
#    define TOMLKIT11_CPP14 1
#else
#    define TOMLKIT11_CPP14 0
#endif

#if TOMLKIT11_CPLUSPLUS >= 201703L
#    define TOMLKIT11_CPP17 1
#else
#    define TOMLKIT11_CPP17 0
#endif

#if TOMLKIT11_CPLUSPLUS >= 202002L
#    define TOMLKIT11_CPP20 1
#else
#    define TOMLKIT11_CPP20 0
#endif

#if TOMLKIT11_CPLUSPLUS >= 202302L
#    define TOMLKIT11_CPP23 1
#else
#    define TOMLKIT11_CPP23 0
#endif

#ifndef TOMLKIT11_NAMESPACE_BEGIN
#    define TOMLKIT11_NAMESPACE_BEGIN namespace tomlkit {
#endif

#ifndef TOMLKIT11_NAMESPACE_END
#    define TOMLKIT11_NAMESPACE_END }
#endif

#ifndef TOMLKIT11_HAS_EXCEPTIONS
#    if defined(TOMLKIT11_NO_EXCEPTIONS)
#        define TOMLKIT11_HAS_EXCEPTIONS 0
#    elif defined(_HAS_EXCEPTIONS) && (_HAS_EXCEPTIONS == 0)
#        define TOMLKIT11_HAS_EXCEPTIONS 0
#    elif defined(__cpp_exceptions)
#        if __cpp_exceptions
#            define TOMLKIT11_HAS_EXCEPTIONS 1
#        else
#            define TOMLKIT11_HAS_EXCEPTIONS 0
#        endif
#    elif defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#        define TOMLKIT11_HAS_EXCEPTIONS 1
#    else
#        define TOMLKIT11_HAS_EXCEPTIONS 0
#    endif
#endif

#ifndef TOMLKIT11_ABORT
#    define TOMLKIT11_ABORT() std::abort()
#endif

#ifndef TOMLKIT11_HAS_STRING_VIEW
#    if TOMLKIT11_CPP17
#        if defined(__has_include)
#            if __has_include(<string_view>)
#                define TOMLKIT11_HAS_STRING_VIEW 1
#            else
#                define TOMLKIT11_HAS_STRING_VIEW 0
#            endif
#        else
#            define TOMLKIT11_HAS_STRING_VIEW 1
#        endif
#    else
#        define TOMLKIT11_HAS_STRING_VIEW 0
#    endif
#endif

#ifndef TOMLKIT11_HAS_OPTIONAL
#    if TOMLKIT11_CPP17
#        if defined(__has_include)
#            if __has_include(<optional>)
#                define TOMLKIT11_HAS_OPTIONAL 1
#            else
#                define TOMLKIT11_HAS_OPTIONAL 0
#            endif
#        else
#            define TOMLKIT11_HAS_OPTIONAL 1
#        endif
#    else
#        define TOMLKIT11_HAS_OPTIONAL 0
#    endif
#endif

#ifndef TOMLKIT11_HAS_FILESYSTEM
#    if TOMLKIT11_CPP17 && (!defined(_GLIBCXX_RELEASE) || (_GLIBCXX_RELEASE >= 9))
#        if defined(__has_include)
#            if __has_include(<filesystem>)
#                define TOMLKIT11_HAS_FILESYSTEM 1
#            else
#                define TOMLKIT11_HAS_FILESYSTEM 0
#            endif
#        else
#            define TOMLKIT11_HAS_FILESYSTEM 1
#        endif
#    else
#        define TOMLKIT11_HAS_FILESYSTEM 0
#    endif
#endif

#ifndef TOMLKIT11_HAS_CHAR8_T
#    if TOMLKIT11_CPP20 && defined(__cpp_char8_t)
#        if __cpp_char8_t >= 201811L
#            define TOMLKIT11_HAS_CHAR8_T 1
#        else
#            define TOMLKIT11_HAS_CHAR8_T 0
#        endif
#    else
#        define TOMLKIT11_HAS_CHAR8_T 0
#    endif
#endif

#ifndef TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
#    if TOMLKIT11_CPP23 && defined(__cpp_multidimensional_subscript)
#        if __cpp_multidimensional_subscript >= 202110L
#            define TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT 1
#        else
#            define TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT 0
#        endif
#    else
#        define TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT 0
#    endif
#endif

#ifndef TOMLKIT11_NODISCARD
#    if TOMLKIT11_CPP17
#        define TOMLKIT11_NODISCARD [[nodiscard]]
#    else
#        define TOMLKIT11_NODISCARD
#    endif
#endif

#if defined(TOMLKIT11_COMPILED_LIB)
#    define TOMLKIT11_INLINE
#else
#    define TOMLKIT11_INLINE inline
#endif

#if !defined(TOMLKIT11_COMPILED_LIB) || defined(TOMLKIT11_IMPLEMENTATION)
#    define TOMLKIT11_SHOULD_INCLUDE_IMPL 1
#else
#    define TOMLKIT11_SHOULD_INCLUDE_IMPL 0
#endif

#endif // TOMLKIT11_CONFIG_HPP
