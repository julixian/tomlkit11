#pragma once

#ifndef TOMLKIT11_PARSER_HPP
#define TOMLKIT11_PARSER_HPP

#include "decor.hpp"
#include "error.hpp"
#include "item.hpp"
#include "spec.hpp"
#include "types.hpp"

#include <string>

#if TOMLKIT11_HAS_FILESYSTEM
#    include <filesystem>
#endif

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
#    include <string_view>
#endif

TOMLKIT11_NAMESPACE_BEGIN

// Parses UTF-8 TOML text (TOML v1.1 by default), returning ParseError with exact diagnostics on failure.
TOMLKIT11_INLINE Result<Item, ParseError> tryParse(
    StringView utf8Source,
    ParseOptions options = ParseOptions(),
    StringView sourcePath = ""
);

// Parses a UTF-8 TOML file from UTF-8 or wide path (uses native wide paths on Windows).
TOMLKIT11_INLINE Result<Item, ParseError> tryParseFile(
    StringView utf8FilePath,
    ParseOptions options = ParseOptions()
);

TOMLKIT11_INLINE Result<Item, ParseError> tryParseFile(
    WideStringView wideFilePath,
    ParseOptions options = ParseOptions()
);

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE Result<Item, ParseError> tryParseFile(
    std::u8string_view u8FilePath,
    ParseOptions options = ParseOptions()
);
#endif

#if TOMLKIT11_HAS_FILESYSTEM
namespace detail
{
    TOMLKIT11_INLINE Result<Item, ParseError> tryParsePathImpl(
        const std::filesystem::path& filePath,
        ParseOptions options
    );
} // namespace detail

template<typename PathT, detail::enable_if_t<
    std::is_same<detail::remove_cvref_t<PathT>, std::filesystem::path>::value, int> = 0>
inline Result<Item, ParseError> tryParseFile(
    const PathT& filePath,
    ParseOptions options = ParseOptions()
)
{
    return detail::tryParsePathImpl(filePath, options);
}
#endif // TOMLKIT11_HAS_FILESYSTEM

#if TOMLKIT11_HAS_EXCEPTIONS

// Parses UTF-8 TOML text, throwing SyntaxError on failure.
TOMLKIT11_INLINE Item parse(
    StringView utf8Source,
    ParseOptions options = ParseOptions(),
    StringView sourcePath = ""
);

TOMLKIT11_INLINE Item parseFile(
    StringView utf8FilePath,
    ParseOptions options = ParseOptions()
);

TOMLKIT11_INLINE Item parseFile(
    WideStringView wideFilePath,
    ParseOptions options = ParseOptions()
);

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE Item parseFile(
    std::u8string_view u8FilePath,
    ParseOptions options = ParseOptions()
);
#endif

#if TOMLKIT11_HAS_FILESYSTEM
namespace detail
{
    TOMLKIT11_INLINE Item parsePathImpl(
        const std::filesystem::path& filePath,
        ParseOptions options
    );
} // namespace detail

template<typename PathT, detail::enable_if_t<
    std::is_same<detail::remove_cvref_t<PathT>, std::filesystem::path>::value, int> = 0>
inline Item parseFile(
    const PathT& filePath,
    ParseOptions options = ParseOptions()
)
{
    return detail::parsePathImpl(filePath, options);
}
#endif // TOMLKIT11_HAS_FILESYSTEM

#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_PARSER_HPP
