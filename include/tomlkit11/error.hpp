#pragma once

#ifndef TOMLKIT11_ERROR_HPP
#define TOMLKIT11_ERROR_HPP

#include "spec.hpp"

#include <string>
#include <utility>

#if TOMLKIT11_HAS_EXCEPTIONS
#    include <stdexcept>
#endif

TOMLKIT11_NAMESPACE_BEGIN

struct SourceLocation
{
    size_t offset = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    uint32_t length = 1;

    constexpr SourceLocation(
        size_t off = 0,
        uint32_t ln = 1,
        uint32_t col = 1,
        uint32_t len = 1
    ) noexcept
        : offset(off)
        , line(ln)
        , column(col)
        , length(len)
    { }
};

inline bool operator==(const SourceLocation& lhs, const SourceLocation& rhs) noexcept
{
    return lhs.offset == rhs.offset
        && lhs.line == rhs.line
        && lhs.column == rhs.column
        && lhs.length == rhs.length;
}

inline bool operator!=(const SourceLocation& lhs, const SourceLocation& rhs) noexcept
{
    return !(lhs == rhs);
}

enum class ErrorCategory : uint8_t
{
    InvalidUtf8,
    ControlCharacter,
    UnexpectedCharacter,
    UnexpectedEof,
    InvalidEscape,
    InvalidNumber,
    InvalidDateTime,
    InvalidKey,
    DuplicateKey,
    TableRedefinition,
    DottedKeyConflict,
    InlineTableImmutable,
    StaticArrayConflict,
    SpecRestriction,
    NestingLimitExceeded,
    TypeMismatch,
    IoError
};

struct ParseError
{
    ErrorCategory category = ErrorCategory::UnexpectedCharacter;
    SourceLocation location{};
    std::string message;
    std::string sourcePath;
    std::string formattedDiagnostic;

    ParseError() = default;

    ParseError(
        ErrorCategory cat,
        SourceLocation loc,
        std::string msg,
        std::string path = std::string(),
        std::string diag = std::string()
    )
        : category(cat)
        , location(loc)
        , message(std::move(msg))
        , sourcePath(std::move(path))
        , formattedDiagnostic(std::move(diag))
    { }

    const std::string& format() const noexcept { return formattedDiagnostic; }
    const std::string& formatDiagnostic() const noexcept { return formattedDiagnostic; }
};

inline bool operator==(const ParseError& lhs, const ParseError& rhs) noexcept
{
    return lhs.category == rhs.category
        && lhs.location == rhs.location
        && lhs.message == rhs.message
        && lhs.sourcePath == rhs.sourcePath
        && lhs.formattedDiagnostic == rhs.formattedDiagnostic;
}

inline bool operator!=(const ParseError& lhs, const ParseError& rhs) noexcept
{
    return !(lhs == rhs);
}

// Computes 1-based line number and UTF-8 codepoint column from raw source and byte offset,
// and generates a diagnostic snippet with caret (`^`) indicators.
TOMLKIT11_INLINE ParseError makeParseError(
    ErrorCategory category,
    StringView source,
    size_t byteOffset,
    uint32_t byteLength,
    std::string message,
    StringView sourcePath = ""
);

#if TOMLKIT11_HAS_EXCEPTIONS

class Exception : public std::runtime_error
{
public:
    explicit Exception(const std::string& msg)
        : std::runtime_error(msg)
    { }

    explicit Exception(const char* pMsg)
        : std::runtime_error(pMsg)
    { }
};

class SyntaxError : public Exception
{
public:
    explicit SyntaxError(ParseError err)
        : Exception(err.formattedDiagnostic.empty() ? err.message : err.formattedDiagnostic)
        , m_error(std::move(err))
    { }

    const ParseError& error() const noexcept { return m_error; }
    const SourceLocation& location() const noexcept { return m_error.location; }
    ErrorCategory category() const noexcept { return m_error.category; }

private:
    ParseError m_error;
};

class TypeError : public Exception
{
public:
    explicit TypeError(const std::string& msg)
        : Exception(msg)
    { }

    explicit TypeError(const char* pMsg)
        : Exception(pMsg)
    { }
};

class OutOfRangeError : public Exception
{
public:
    explicit OutOfRangeError(const std::string& msg)
        : Exception(msg)
    { }

    explicit OutOfRangeError(const char* pMsg)
        : Exception(pMsg)
    { }
};

#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_ERROR_HPP
