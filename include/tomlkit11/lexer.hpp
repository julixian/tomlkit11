#pragma once

#ifndef TOMLKIT11_LEXER_HPP
#define TOMLKIT11_LEXER_HPP

#include "decor.hpp"
#include "error.hpp"
#include "spec.hpp"
#include "types.hpp"

#include <algorithm>
#include <string>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

namespace detail
{

struct LexedString
{
    std::string value;
    StringStyle style = StringStyle::Basic;
    StringView rawRepr;

    LexedString() = default;

    LexedString(std::string val, StringStyle st, StringView raw)
        : value(std::move(val))
        , style(st)
        , rawRepr(raw)
    { }
};

struct ScalarPayload
{
    enum class Kind : uint8_t
    {
        Boolean,
        Integer,
        Float,
        LocalDate,
        LocalTime,
        LocalDateTime,
        OffsetDateTime
    };

    union Storage
    {
        bool boolVal;
        int64_t intVal;
        double floatVal;
        LocalDate dateVal;
        LocalTime timeVal;
        LocalDateTime dateTimeVal;
        OffsetDateTime offsetDateTimeVal;

        Storage() noexcept
            : boolVal(false)
        { }

        explicit Storage(bool v) noexcept
            : boolVal(v)
        { }

        explicit Storage(int64_t v) noexcept
            : intVal(v)
        { }

        explicit Storage(double v) noexcept
            : floatVal(v)
        { }

        explicit Storage(LocalDate v) noexcept
            : dateVal(v)
        { }

        explicit Storage(LocalTime v) noexcept
            : timeVal(v)
        { }

        explicit Storage(LocalDateTime v) noexcept
            : dateTimeVal(v)
        { }

        explicit Storage(OffsetDateTime v) noexcept
            : offsetDateTimeVal(v)
        { }
    };

    Kind kind = Kind::Boolean;
    Storage storage;

    ScalarPayload() noexcept
        : kind(Kind::Boolean)
        , storage(false)
    { }

    explicit ScalarPayload(bool v) noexcept
        : kind(Kind::Boolean)
        , storage(v)
    { }

    explicit ScalarPayload(int64_t v) noexcept
        : kind(Kind::Integer)
        , storage(v)
    { }

    explicit ScalarPayload(double v) noexcept
        : kind(Kind::Float)
        , storage(v)
    { }

    explicit ScalarPayload(LocalDate v) noexcept
        : kind(Kind::LocalDate)
        , storage(v)
    { }

    explicit ScalarPayload(LocalTime v) noexcept
        : kind(Kind::LocalTime)
        , storage(v)
    { }

    explicit ScalarPayload(LocalDateTime v) noexcept
        : kind(Kind::LocalDateTime)
        , storage(v)
    { }

    explicit ScalarPayload(OffsetDateTime v) noexcept
        : kind(Kind::OffsetDateTime)
        , storage(v)
    { }

    bool isBoolean() const noexcept { return kind == Kind::Boolean; }
    bool isInteger() const noexcept { return kind == Kind::Integer; }
    bool isFloat() const noexcept { return kind == Kind::Float; }
    bool isLocalDate() const noexcept { return kind == Kind::LocalDate; }
    bool isLocalTime() const noexcept { return kind == Kind::LocalTime; }
    bool isLocalDateTime() const noexcept { return kind == Kind::LocalDateTime; }
    bool isOffsetDateTime() const noexcept { return kind == Kind::OffsetDateTime; }

    bool asBoolean() const noexcept { return storage.boolVal; }
    int64_t asInteger() const noexcept { return storage.intVal; }
    double asFloat() const noexcept { return storage.floatVal; }
    const LocalDate& asLocalDate() const noexcept { return storage.dateVal; }
    const LocalTime& asLocalTime() const noexcept { return storage.timeVal; }
    const LocalDateTime& asLocalDateTime() const noexcept { return storage.dateTimeVal; }
    const OffsetDateTime& asOffsetDateTime() const noexcept { return storage.offsetDateTimeVal; }
};

struct LexedScalar
{
    ScalarPayload payload;
    IntegerRadix integerRadix = IntegerRadix::Decimal;
    StringView rawRepr;

    LexedScalar() noexcept
        : payload(false)
        , integerRadix(IntegerRadix::Decimal)
        , rawRepr()
    { }

    LexedScalar(
        ScalarPayload p,
        StringView raw,
        IntegerRadix radix = IntegerRadix::Decimal
    ) noexcept
        : payload(p)
        , integerRadix(radix)
        , rawRepr(raw)
    { }
};

// Context-sensitive zero-copy cursor lexer separating key, value, and trivia domains.
class Lexer
{
public:
    TOMLKIT11_INLINE Lexer(
        StringView source,
        ParseOptions options = ParseOptions(),
        StringView sourcePath = ""
    ) noexcept;

    // Validates UTF-8 across the full document and consumes an optional leading UTF-8 BOM.
    TOMLKIT11_INLINE Result<bool, ParseError> validateUtf8AndConsumeBom();

    bool isEof() const noexcept { return m_pos >= m_source.size(); }
    size_t position() const noexcept { return m_pos; }
    void setPosition(size_t pos) noexcept
    {
        m_pos = (pos < m_source.size()) ? pos : m_source.size();
    }

    char peek(size_t lookahead = 0) const noexcept
    {
        const size_t idx = m_pos + lookahead;
        return idx < m_source.size() ? m_source[idx] : '\0';
    }

    char advance() noexcept
    {
        return m_pos < m_source.size() ? m_source[m_pos++] : '\0';
    }

    StringView sliceFrom(size_t startPos) const noexcept
    {
        return m_source.substr(startPos, m_pos - startPos);
    }

    StringView source() const noexcept { return m_source; }
    const ParseOptions& options() const noexcept { return m_options; }

    TOMLKIT11_INLINE ParseError errorAt(
        ErrorCategory category,
        size_t offset,
        uint32_t length,
        std::string msg
    ) const;

    TOMLKIT11_INLINE ParseError errorHere(
        ErrorCategory category,
        std::string msg,
        uint32_t length = 1
    ) const;

    // Trivia scanning (returns zero-copy views into m_source)
    TOMLKIT11_INLINE StringView consumeHorizontalWs() noexcept;
    TOMLKIT11_INLINE Result<StringView, ParseError> consumeWsCommentNewlines();
    TOMLKIT11_INLINE Result<StringView, ParseError> consumeLineTrail();
    TOMLKIT11_INLINE Result<StringView, ParseError> consumeAfterCommaSameLine();

    // Key-domain scanning
    TOMLKIT11_INLINE Result<KeySegment, ParseError> lexKeySegment();
    TOMLKIT11_INLINE Result<std::vector<KeySegment>, ParseError> lexDottedKey();

    // Value-domain scanning
    TOMLKIT11_INLINE Result<LexedString, ParseError> lexString();
    TOMLKIT11_INLINE Result<LexedScalar, ParseError> lexScalar();

private:
    TOMLKIT11_INLINE Result<void, ParseError> consumeCommentBody();
    TOMLKIT11_INLINE Result<void, ParseError> consumeNewline();

    TOMLKIT11_INLINE Result<LexedString, ParseError> lexBasicString(bool multiline, size_t startOffset);
    TOMLKIT11_INLINE Result<LexedString, ParseError> lexLiteralString(bool multiline, size_t startOffset);
    TOMLKIT11_INLINE Result<char32_t, ParseError> lexHexEscape(size_t digits, size_t escapeStartOffset);

    TOMLKIT11_INLINE Result<LexedScalar, ParseError> lexDateOrTime(size_t startOffset, StringView token);
    TOMLKIT11_INLINE Result<LexedScalar, ParseError> lexNumber(size_t startOffset, StringView token);

    StringView m_source;
    ParseOptions m_options;
    StringView m_sourcePath;
    size_t m_pos = 0;
};

} // namespace detail

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_LEXER_HPP
