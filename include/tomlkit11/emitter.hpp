#pragma once

#ifndef TOMLKIT11_EMITTER_HPP
#define TOMLKIT11_EMITTER_HPP

#include "decor.hpp"
#include "error.hpp"
#include "item.hpp"
#include "spec.hpp"
#include "types.hpp"

#include <string>
#include <type_traits>

#if TOMLKIT11_HAS_FILESYSTEM
#    include <filesystem>
#endif

#if TOMLKIT11_HAS_CHAR8_T && TOMLKIT11_HAS_STRING_VIEW
#    include <string_view>
#endif

TOMLKIT11_NAMESPACE_BEGIN

// Serializes a document or any item node into TOML text without throwing exceptions.
// Unmodified nodes replay their original raw formatting, comments, and whitespace.
TOMLKIT11_NODISCARD TOMLKIT11_INLINE Result<std::string, ParseError> tryFormat(
    const Item& item,
    FormatOptions options = FormatOptions()
);

// Serializes a document or item node into TOML text.
// On formatting failure, throws TypeError when exceptions are enabled or calls TOMLKIT11_ABORT().
TOMLKIT11_INLINE std::string format(
    const Item& item,
    FormatOptions options = FormatOptions()
);

#if TOMLKIT11_HAS_FILESYSTEM
namespace detail
{
    TOMLKIT11_INLINE Result<void, ParseError> trySavePathAtomicImpl(
        const std::filesystem::path& filePath,
        const Item& doc,
        FormatOptions options
    );

#if TOMLKIT11_HAS_EXCEPTIONS
    TOMLKIT11_INLINE void savePathAtomicImpl(
        const std::filesystem::path& filePath,
        const Item& doc,
        FormatOptions options
    );
#endif
} // namespace detail

// Atomically writes serialized TOML to a temporary binary file and replaces target path.
template<typename PathT, detail::enable_if_t<
    std::is_same<detail::remove_cvref_t<PathT>, std::filesystem::path>::value, int> = 0>
TOMLKIT11_NODISCARD inline Result<void, ParseError> trySaveFileAtomic(
    const PathT& filePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
)
{
    return detail::trySavePathAtomicImpl(filePath, doc, options);
}
#endif // TOMLKIT11_HAS_FILESYSTEM

TOMLKIT11_NODISCARD TOMLKIT11_INLINE Result<void, ParseError> trySaveFileAtomic(
    StringView utf8FilePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
);

TOMLKIT11_NODISCARD TOMLKIT11_INLINE Result<void, ParseError> trySaveFileAtomic(
    WideStringView wideFilePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
);

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_NODISCARD TOMLKIT11_INLINE Result<void, ParseError> trySaveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
);
#endif

#if TOMLKIT11_HAS_EXCEPTIONS

#if TOMLKIT11_HAS_FILESYSTEM
template<typename PathT, detail::enable_if_t<
    std::is_same<detail::remove_cvref_t<PathT>, std::filesystem::path>::value, int> = 0>
inline void saveFileAtomic(
    const PathT& filePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
)
{
    detail::savePathAtomicImpl(filePath, doc, options);
}
#endif // TOMLKIT11_HAS_FILESYSTEM

TOMLKIT11_INLINE void saveFileAtomic(
    StringView utf8FilePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
);

TOMLKIT11_INLINE void saveFileAtomic(
    WideStringView wideFilePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
);

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE void saveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options = FormatOptions()
);
#endif

#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_EMITTER_HPP
