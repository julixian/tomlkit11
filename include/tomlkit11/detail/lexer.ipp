#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#ifndef TOMLKIT11_DETAIL_LEXER_IPP
#define TOMLKIT11_DETAIL_LEXER_IPP

#include "../lexer.hpp"

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#    include <locale.h>
#    include <stdlib.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#    include <locale.h>
#    include <stdlib.h>
#    include <xlocale.h>
#elif defined(__linux__) || defined(__unix__)
#    include <locale.h>
#    include <stdlib.h>
#endif

TOMLKIT11_NAMESPACE_BEGIN

namespace detail
{

namespace lexer_detail
{

inline constexpr bool isAsciiDigit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

inline constexpr bool isHexDigit(char ch) noexcept
{
    return (ch >= '0' && ch <= '9')
        || (ch >= 'a' && ch <= 'f')
        || (ch >= 'A' && ch <= 'F');
}

inline constexpr uint32_t hexValue(char ch) noexcept
{
    return (ch >= '0' && ch <= '9')
        ? (uint32_t)(ch - '0')
        : ((ch >= 'a' && ch <= 'f')
            ? (uint32_t)(ch - 'a' + 10)
            : (uint32_t)(ch - 'A' + 10));
}

inline constexpr bool isHorizontalWs(char ch) noexcept
{
    return ch == ' ' || ch == '\t';
}

inline constexpr bool isScalarDelimiter(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'
        || ch == '#' || ch == ',' || ch == ']' || ch == '}';
}

// Control characters 0x00..0x1F (except '\t') and 0x7F are forbidden in single-line strings and comments.
inline constexpr bool isForbiddenSingleLineCtrl(uint8_t b) noexcept
{
    return (b < 0x20u && b != 0x09u) || b == 0x7Fu;
}

// Multi-line strings allow '\t', '\n', and CRLF pairs, while forbidding other control characters.
inline constexpr bool isForbiddenMultiLineCtrl(uint8_t b) noexcept
{
    return (b < 0x20u && b != 0x09u && b != 0x0Au && b != 0x0Du) || b == 0x7Fu;
}

inline bool parseFixedDigits(StringView sv, size_t offset, size_t count, uint32_t& outVal) noexcept
{
    if (offset + count > sv.size()) {
        return false;
    }
    uint32_t acc = 0;
    for (size_t i = 0; i < count; ++i) {
        const char ch = sv[offset + i];
        if (!isAsciiDigit(ch)) {
            return false;
        }
        acc = acc * 10u + (uint32_t)(ch - '0');
    }
    outVal = acc;
    return true;
}

inline double parseDoubleLocaleConvFallback(
    const std::string& clean,
    char*& outEnd
) noexcept
{
    const std::lconv* pConv = std::localeconv();
    if (pConv != nullptr && pConv->decimal_point != nullptr && pConv->decimal_point[0] != '\0') {
        if (pConv->decimal_point[0] != '.' || pConv->decimal_point[1] != '\0') {
            const size_t dotPos = clean.find('.');
            if (dotPos != std::string::npos) {
                std::string localized = clean;
                localized.replace(dotPos, 1, pConv->decimal_point);
                char* pLocalEnd = nullptr;
                errno = 0;
                const double val = std::strtod(localized.c_str(), &pLocalEnd);
                const size_t consumed = (pLocalEnd != nullptr)
                    ? (size_t)(pLocalEnd - localized.c_str())
                    : 0;
                if (pLocalEnd == localized.c_str() + localized.size()) {
                    outEnd = (char*)(clean.c_str() + clean.size());
                }
                else {
                    outEnd = (char*)(clean.c_str() + (consumed < clean.size() ? consumed : clean.size()));
                }
                return val;
            }
        }
    }
    errno = 0;
    return std::strtod(clean.c_str(), &outEnd);
}

#if defined(_WIN32)
struct CLocaleHolder
{
    _locale_t loc;

    CLocaleHolder() noexcept
        : loc(_create_locale(LC_NUMERIC, "C"))
    { }

    ~CLocaleHolder()
    {
        if (loc != nullptr) {
            _free_locale(loc);
        }
    }

    CLocaleHolder(const CLocaleHolder&) = delete;
    CLocaleHolder& operator=(const CLocaleHolder&) = delete;
};

inline double parseDoubleCLocale(const std::string& clean, char*& outEnd) noexcept
{
    static const CLocaleHolder cLocale;
    if (cLocale.loc != nullptr) {
        errno = 0;
        return _strtod_l(clean.c_str(), &outEnd, cLocale.loc);
    }
    return parseDoubleLocaleConvFallback(clean, outEnd);
}
#elif defined(LC_NUMERIC_MASK) && (defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
struct CLocaleHolder
{
    locale_t loc;

    CLocaleHolder() noexcept
        : loc(::newlocale(LC_NUMERIC_MASK, "C", (locale_t)0))
    { }

    ~CLocaleHolder()
    {
        if (loc != (locale_t)0) {
            ::freelocale(loc);
        }
    }

    CLocaleHolder(const CLocaleHolder&) = delete;
    CLocaleHolder& operator=(const CLocaleHolder&) = delete;
};

inline double parseDoubleCLocale(const std::string& clean, char*& outEnd) noexcept
{
    static const CLocaleHolder cLocale;
    if (cLocale.loc != (locale_t)0) {
        errno = 0;
        return ::strtod_l(clean.c_str(), &outEnd, cLocale.loc);
    }
    return parseDoubleLocaleConvFallback(clean, outEnd);
}
#else
inline double parseDoubleCLocale(const std::string& clean, char*& outEnd) noexcept
{
    return parseDoubleLocaleConvFallback(clean, outEnd);
}
#endif

} // namespace lexer_detail

TOMLKIT11_INLINE Lexer::Lexer(
    StringView source,
    ParseOptions options,
    StringView sourcePath
) noexcept
    : m_source(source)
    , m_options(options)
    , m_sourcePath(sourcePath)
{ }

TOMLKIT11_INLINE ParseError Lexer::errorAt(
    ErrorCategory category,
    size_t offset,
    uint32_t length,
    std::string msg
) const
{
    return makeParseError(category, m_source, offset, length, std::move(msg), m_sourcePath);
}

TOMLKIT11_INLINE ParseError Lexer::errorHere(
    ErrorCategory category,
    std::string msg,
    uint32_t length
) const
{
    return makeParseError(category, m_source, m_pos, length, std::move(msg), m_sourcePath);
}

TOMLKIT11_INLINE Result<bool, ParseError> Lexer::validateUtf8AndConsumeBom()
{
    size_t errOffset = 0;
    if (!isValidUtf8(m_source, &errOffset)) {
        return makeUnexpected(errorAt(
            ErrorCategory::InvalidUtf8,
            errOffset,
            1,
            "Invalid UTF-8 byte sequence in TOML input"
        ));
    }

    bool hasBom = false;
    if (m_pos == 0 && svStartsWith(m_source, StringView("\xEF\xBB\xBF", 3))) {
        m_pos = 3;
        hasBom = true;
    }
    return hasBom;
}

TOMLKIT11_INLINE StringView Lexer::consumeHorizontalWs() noexcept
{
    const size_t start = m_pos;
    while (!isEof() && lexer_detail::isHorizontalWs(peek())) {
        ++m_pos;
    }
    return sliceFrom(start);
}

TOMLKIT11_INLINE Result<void, ParseError> Lexer::consumeCommentBody()
{
    // Caller guarantees current char is '#'
    ++m_pos;
    while (!isEof()) {
        const char ch = peek();
        if (ch == '\n') {
            break;
        }
        if (ch == '\r') {
            if (peek(1) == '\n') {
                break;
            }
            return makeUnexpected(errorHere(
                ErrorCategory::ControlCharacter,
                "Bare carriage return '\\r' without '\\n' is not permitted in comments"
            ));
        }
        if (lexer_detail::isForbiddenSingleLineCtrl((uint8_t)ch)) {
            std::string msg = "Control character 0x";
            appendHexUint32Fixed(msg, (uint32_t)(uint8_t)ch, 2, true);
            msg += " is not permitted in comments";
            return makeUnexpected(errorHere(
                ErrorCategory::ControlCharacter,
                std::move(msg)
            ));
        }
        ++m_pos;
    }
    return Result<void, ParseError>();
}

TOMLKIT11_INLINE Result<void, ParseError> Lexer::consumeNewline()
{
    if (peek() == '\n') {
        ++m_pos;
        return Result<void, ParseError>();
    }
    if (peek() == '\r') {
        if (peek(1) == '\n') {
            m_pos += 2;
            return Result<void, ParseError>();
        }
        return makeUnexpected(errorHere(
            ErrorCategory::ControlCharacter,
            "Bare carriage return '\\r' without '\\n' is not permitted in TOML"
        ));
    }
    return makeUnexpected(errorHere(ErrorCategory::UnexpectedCharacter, "Expected newline"));
}

TOMLKIT11_INLINE Result<StringView, ParseError> Lexer::consumeWsCommentNewlines()
{
    const size_t start = m_pos;
    while (!isEof()) {
        consumeHorizontalWs();
        if (isEof()) {
            break;
        }
        const char ch = peek();
        if (ch == '#') {
            Result<void, ParseError> res = consumeCommentBody();
            if (!res) {
                return makeUnexpected(std::move(res.error()));
            }
            if (!isEof()) {
                Result<void, ParseError> nlRes = consumeNewline();
                if (!nlRes) {
                    return makeUnexpected(std::move(nlRes.error()));
                }
            }
        }
        else if (ch == '\n' || ch == '\r') {
            Result<void, ParseError> nlRes = consumeNewline();
            if (!nlRes) {
                return makeUnexpected(std::move(nlRes.error()));
            }
        }
        else {
            break;
        }
    }
    return sliceFrom(start);
}

TOMLKIT11_INLINE Result<StringView, ParseError> Lexer::consumeLineTrail()
{
    const size_t start = m_pos;
    consumeHorizontalWs();
    if (!isEof() && peek() == '#') {
        Result<void, ParseError> res = consumeCommentBody();
        if (!res) {
            return makeUnexpected(std::move(res.error()));
        }
    }
    if (isEof()) {
        return sliceFrom(start);
    }
    if (peek() == '\n' || peek() == '\r') {
        Result<void, ParseError> nlRes = consumeNewline();
        if (!nlRes) {
            return makeUnexpected(std::move(nlRes.error()));
        }
        return sliceFrom(start);
    }

    std::string msg = "Unexpected trailing character '";
    msg.push_back(peek());
    msg += "' after statement; expected newline or '#'";
    return makeUnexpected(errorHere(
        ErrorCategory::UnexpectedCharacter,
        std::move(msg)
    ));
}

TOMLKIT11_INLINE Result<StringView, ParseError> Lexer::consumeAfterCommaSameLine()
{
    const size_t start = m_pos;
    consumeHorizontalWs();
    if (!isEof() && peek() == '#') {
        Result<void, ParseError> res = consumeCommentBody();
        if (!res) {
            return makeUnexpected(std::move(res.error()));
        }
    }
    if (!isEof() && (peek() == '\n' || peek() == '\r')) {
        Result<void, ParseError> nlRes = consumeNewline();
        if (!nlRes) {
            return makeUnexpected(std::move(nlRes.error()));
        }
    }
    return sliceFrom(start);
}

TOMLKIT11_INLINE Result<KeySegment, ParseError> Lexer::lexKeySegment()
{
    if (isEof()) {
        return makeUnexpected(errorHere(
            ErrorCategory::UnexpectedEof,
            "Unexpected end of file while expecting a key"
        ));
    }

    const size_t start = m_pos;
    const char ch = peek();

    if (ch == '"' || ch == '\'') {
        if (peek(1) == ch && peek(2) == ch) {
            return makeUnexpected(errorAt(
                ErrorCategory::InvalidKey,
                start,
                3,
                "Multi-line strings cannot be used as TOML keys"
            ));
        }

        Result<LexedString, ParseError> strRes = lexString();
        if (!strRes) {
            return makeUnexpected(std::move(strRes.error()));
        }

        return KeySegment(
            std::move(strRes->value),
            (ch == '"') ? KeyStyle::BasicQuoted : KeyStyle::LiteralQuoted,
            toString(strRes->rawRepr)
        );
    }

    while (!isEof() && isBareKeyChar((char32_t)(uint8_t)peek(), m_options.spec)) {
        ++m_pos;
    }

    const StringView raw = sliceFrom(start);
    if (raw.empty()) {
        return makeUnexpected(errorHere(
            ErrorCategory::InvalidKey,
            "Expected a valid key (bare key or quoted string)"
        ));
    }

    std::string rawStr = toString(raw);
    return KeySegment(rawStr, KeyStyle::Bare, rawStr);
}

TOMLKIT11_INLINE Result<std::vector<KeySegment>, ParseError> Lexer::lexDottedKey()
{
    std::vector<KeySegment> segments;
    uint32_t depth = 0;

    while (true) {
        ++depth;
        if (depth > m_options.maxNestingDepth) {
            std::string msg = "Dotted key exceeds maximum nesting depth (";
            appendUint(msg, (uint64_t)m_options.maxNestingDepth);
            msg.push_back(')');
            return makeUnexpected(errorHere(
                ErrorCategory::NestingLimitExceeded,
                std::move(msg)
            ));
        }

        const StringView prefixWs = segments.empty() ? StringView() : consumeHorizontalWs();
        Result<KeySegment, ParseError> segRes = lexKeySegment();
        if (!segRes) {
            return makeUnexpected(std::move(segRes.error()));
        }
        const StringView suffixWs = consumeHorizontalWs();

        segRes->decor.prefix = toString(prefixWs);
        segRes->decor.suffix = toString(suffixWs);
        segments.push_back(std::move(*segRes));

        if (!isEof() && peek() == '.') {
            ++m_pos; // skip '.'
            continue;
        }
        break;
    }

    return segments;
}

TOMLKIT11_INLINE Result<char32_t, ParseError> Lexer::lexHexEscape(
    size_t digits,
    size_t escapeStartOffset
)
{
    if (m_pos + digits > m_source.size()) {
        return makeUnexpected(errorAt(
            ErrorCategory::InvalidEscape,
            escapeStartOffset,
            (uint32_t)(m_source.size() - escapeStartOffset),
            "Incomplete hexadecimal escape sequence in string"
        ));
    }

    uint32_t codepoint = 0;
    for (size_t i = 0; i < digits; ++i) {
        const char ch = m_source[m_pos + i];
        if (!lexer_detail::isHexDigit(ch)) {
            std::string msg = "Invalid hexadecimal digit '";
            msg.push_back(ch);
            msg += "' in string escape";
            return makeUnexpected(errorAt(
                ErrorCategory::InvalidEscape,
                escapeStartOffset,
                (uint32_t)(m_pos + i + 1 - escapeStartOffset),
                std::move(msg)
            ));
        }
        codepoint = (codepoint << 4) | lexer_detail::hexValue(ch);
    }

    m_pos += digits;

    // Validate Unicode scalar value: reject UTF-16 surrogates U+D800..U+DFFF and > U+10FFFF.
    if ((codepoint >= 0xD800u && codepoint <= 0xDFFFu) || codepoint > 0x10FFFFu) {
        std::string msg = "Escaped codepoint U+";
        appendHexUint32Fixed(msg, codepoint, 4, true);
        msg += " is not a valid Unicode scalar value";
        return makeUnexpected(errorAt(
            ErrorCategory::InvalidEscape,
            escapeStartOffset,
            (uint32_t)(m_pos - escapeStartOffset),
            std::move(msg)
        ));
    }

    return (char32_t)codepoint;
}

TOMLKIT11_INLINE Result<LexedString, ParseError> Lexer::lexBasicString(
    bool multiline,
    size_t startOffset
)
{
    // Opening '"' already verified by caller.
    m_pos += multiline ? 3 : 1;

    // Strip a single newline immediately following opening `"""`.
    if (multiline && !isEof()) {
        if (peek() == '\n') {
            ++m_pos;
        }
        else if (peek() == '\r') {
            if (peek(1) == '\n') {
                m_pos += 2;
            }
            else {
                return makeUnexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    "Bare carriage return '\\r' without '\\n' in multi-line basic string"
                ));
            }
        }
    }

    std::string decoded;
    while (!isEof()) {
        const char ch = peek();

        if (ch == '"') {
            if (!multiline) {
                ++m_pos;
                return LexedString(std::move(decoded), StringStyle::Basic, sliceFrom(startOffset));
            }

            size_t quoteRun = 0;
            while (peek(quoteRun) == '"') {
                ++quoteRun;
            }
            if (quoteRun < 3) {
                decoded.append(quoteRun, '"');
                m_pos += quoteRun;
                continue;
            }
            const size_t extra = quoteRun - 3;
            const size_t bodyQuotes = (extra < 2) ? extra : 2;
            decoded.append(bodyQuotes, '"');
            m_pos += bodyQuotes + 3;
            return LexedString(std::move(decoded), StringStyle::MultilineBasic, sliceFrom(startOffset));
        }

        if (ch == '\\') {
            const size_t escStart = m_pos;
            ++m_pos; // skip '\\'
            if (isEof()) {
                return makeUnexpected(errorAt(
                    ErrorCategory::UnexpectedEof,
                    escStart,
                    1,
                    "Unterminated escape sequence at end of file"
                ));
            }

            const char esc = peek();

            // Line-ending backslash in multi-line basic strings trims trailing ws and subsequent newlines/ws.
            if (multiline && (lexer_detail::isHorizontalWs(esc) || esc == '\n' || esc == '\r')) {
                consumeHorizontalWs();
                if (!isEof() && (peek() == '\n' || peek() == '\r')) {
                    Result<void, ParseError> nlRes = consumeNewline();
                    if (!nlRes) {
                        return makeUnexpected(std::move(nlRes.error()));
                    }
                    while (!isEof()) {
                        consumeHorizontalWs();
                        if (!isEof() && (peek() == '\n' || peek() == '\r')) {
                            Result<void, ParseError> innerNlRes = consumeNewline();
                            if (!innerNlRes) {
                                return makeUnexpected(std::move(innerNlRes.error()));
                            }
                        }
                        else {
                            break;
                        }
                    }
                    continue;
                }
                return makeUnexpected(errorAt(
                    ErrorCategory::InvalidEscape,
                    escStart,
                    (uint32_t)(m_pos - escStart + 1),
                    "Invalid escape sequence: backslash followed by whitespace without newline"
                ));
            }

            ++m_pos;
            switch (esc)
            {
            case '"':  decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case 'b':  decoded.push_back('\b'); break;
            case 'f':  decoded.push_back('\f'); break;
            case 'n':  decoded.push_back('\n'); break;
            case 'r':  decoded.push_back('\r'); break;
            case 't':  decoded.push_back('\t'); break;
            case 'e':
                if (m_options.spec == SpecVersion::v1_0) {
                    return makeUnexpected(errorAt(
                        ErrorCategory::SpecRestriction,
                        escStart,
                        2,
                        "Escape sequence '\\e' requires TOML v1.1"
                    ));
                }
                decoded.push_back('\x1B');
                break;
            case 'x':
            {
                if (m_options.spec == SpecVersion::v1_0) {
                    return makeUnexpected(errorAt(
                        ErrorCategory::SpecRestriction,
                        escStart,
                        2,
                        "Hexadecimal escape '\\xHH' requires TOML v1.1"
                    ));
                }
                Result<char32_t, ParseError> cpRes = lexHexEscape(2, escStart);
                if (!cpRes) {
                    return makeUnexpected(std::move(cpRes.error()));
                }
                appendUtf8Codepoint(decoded, *cpRes);
                break;
            }
            case 'u':
            {
                Result<char32_t, ParseError> cpRes = lexHexEscape(4, escStart);
                if (!cpRes) {
                    return makeUnexpected(std::move(cpRes.error()));
                }
                appendUtf8Codepoint(decoded, *cpRes);
                break;
            }
            case 'U':
            {
                Result<char32_t, ParseError> cpRes = lexHexEscape(8, escStart);
                if (!cpRes) {
                    return makeUnexpected(std::move(cpRes.error()));
                }
                appendUtf8Codepoint(decoded, *cpRes);
                break;
            }
            default:
            {
                std::string msg = "Invalid string escape sequence '\\";
                msg.push_back(esc);
                msg.push_back('\'');
                return makeUnexpected(errorAt(
                    ErrorCategory::InvalidEscape,
                    escStart,
                    2,
                    std::move(msg)
                ));
            }
            }
            continue;
        }

        if (!multiline) {
            if (ch == '\n' || ch == '\r') {
                return makeUnexpected(errorAt(
                    ErrorCategory::UnexpectedCharacter,
                    startOffset,
                    (uint32_t)(m_pos - startOffset),
                    "Unterminated single-line basic string before newline"
                ));
            }
            if (lexer_detail::isForbiddenSingleLineCtrl((uint8_t)ch)) {
                std::string msg = "Control character 0x";
                appendHexUint32Fixed(msg, (uint32_t)(uint8_t)ch, 2, true);
                msg += " is not permitted in basic string";
                return makeUnexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::move(msg)
                ));
            }
            decoded.push_back(advance());
        }
        else {
            if (ch == '\r') {
                if (peek(1) != '\n') {
                    return makeUnexpected(errorHere(
                        ErrorCategory::ControlCharacter,
                        "Bare carriage return '\\r' without '\\n' is not permitted in multi-line basic string"
                    ));
                }
                m_pos += 2;
                decoded.push_back('\n'); // normalize multi-line semantic newlines while rawRepr preserves CRLF
                continue;
            }
            if (lexer_detail::isForbiddenMultiLineCtrl((uint8_t)ch)) {
                std::string msg = "Control character 0x";
                appendHexUint32Fixed(msg, (uint32_t)(uint8_t)ch, 2, true);
                msg += " is not permitted in multi-line basic string";
                return makeUnexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::move(msg)
                ));
            }
            decoded.push_back(advance());
        }
    }

    return makeUnexpected(errorAt(
        ErrorCategory::UnexpectedEof,
        startOffset,
        (uint32_t)(m_pos - startOffset),
        multiline ? "Unterminated multi-line basic string" : "Unterminated basic string"
    ));
}

TOMLKIT11_INLINE Result<LexedString, ParseError> Lexer::lexLiteralString(
    bool multiline,
    size_t startOffset
)
{
    m_pos += multiline ? 3 : 1;

    if (multiline && !isEof()) {
        if (peek() == '\n') {
            ++m_pos;
        }
        else if (peek() == '\r') {
            if (peek(1) == '\n') {
                m_pos += 2;
            }
            else {
                return makeUnexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    "Bare carriage return '\\r' without '\\n' in multi-line literal string"
                ));
            }
        }
    }

    std::string decoded;
    while (!isEof()) {
        const char ch = peek();

        if (ch == '\'') {
            if (!multiline) {
                ++m_pos;
                return LexedString(std::move(decoded), StringStyle::Literal, sliceFrom(startOffset));
            }

            size_t quoteRun = 0;
            while (peek(quoteRun) == '\'') {
                ++quoteRun;
            }
            if (quoteRun < 3) {
                decoded.append(quoteRun, '\'');
                m_pos += quoteRun;
                continue;
            }
            const size_t extra = quoteRun - 3;
            const size_t bodyQuotes = (extra < 2) ? extra : 2;
            decoded.append(bodyQuotes, '\'');
            m_pos += bodyQuotes + 3;
            return LexedString(std::move(decoded), StringStyle::MultilineLiteral, sliceFrom(startOffset));
        }

        if (!multiline) {
            if (ch == '\n' || ch == '\r') {
                return makeUnexpected(errorAt(
                    ErrorCategory::UnexpectedCharacter,
                    startOffset,
                    (uint32_t)(m_pos - startOffset),
                    "Unterminated single-line literal string before newline"
                ));
            }
            if (lexer_detail::isForbiddenSingleLineCtrl((uint8_t)ch)) {
                std::string msg = "Control character 0x";
                appendHexUint32Fixed(msg, (uint32_t)(uint8_t)ch, 2, true);
                msg += " is not permitted in literal string";
                return makeUnexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::move(msg)
                ));
            }
            decoded.push_back(advance());
        }
        else {
            if (ch == '\r') {
                if (peek(1) != '\n') {
                    return makeUnexpected(errorHere(
                        ErrorCategory::ControlCharacter,
                        "Bare carriage return '\\r' without '\\n' is not permitted in multi-line literal string"
                    ));
                }
                m_pos += 2;
                decoded.push_back('\n');
                continue;
            }
            if (lexer_detail::isForbiddenMultiLineCtrl((uint8_t)ch)) {
                std::string msg = "Control character 0x";
                appendHexUint32Fixed(msg, (uint32_t)(uint8_t)ch, 2, true);
                msg += " is not permitted in multi-line literal string";
                return makeUnexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::move(msg)
                ));
            }
            decoded.push_back(advance());
        }
    }

    return makeUnexpected(errorAt(
        ErrorCategory::UnexpectedEof,
        startOffset,
        (uint32_t)(m_pos - startOffset),
        multiline ? "Unterminated multi-line literal string" : "Unterminated literal string"
    ));
}

TOMLKIT11_INLINE Result<LexedString, ParseError> Lexer::lexString()
{
    const size_t start = m_pos;
    if (peek() == '"') {
        const bool ml = (peek(1) == '"' && peek(2) == '"');
        return lexBasicString(ml, start);
    }
    if (peek() == '\'') {
        const bool ml = (peek(1) == '\'' && peek(2) == '\'');
        return lexLiteralString(ml, start);
    }
    return makeUnexpected(errorHere(ErrorCategory::UnexpectedCharacter, "Expected string literal"));
}

TOMLKIT11_INLINE Result<LexedScalar, ParseError> Lexer::lexScalar()
{
    if (isEof()) {
        return makeUnexpected(errorHere(
            ErrorCategory::UnexpectedEof,
            "Expected scalar value before end of file"
        ));
    }

    const size_t start = m_pos;
    while (!isEof() && !lexer_detail::isScalarDelimiter(peek())) {
        ++m_pos;
    }

    // Handle space-separated RFC 3339 date-time (e.g. `1979-05-27 07:32:00Z` or v1.1 `1979-05-27 07:32`).
    if ((m_pos - start) == 10
        && m_source[start + 4] == '-'
        && m_source[start + 7] == '-'
        && peek() == ' '
        && lexer_detail::isAsciiDigit(peek(1)))
    {
        ++m_pos; // include separating space
        while (!isEof() && !lexer_detail::isScalarDelimiter(peek())) {
            ++m_pos;
        }
    }

    const StringView token = sliceFrom(start);
    if (token.empty()) {
        return makeUnexpected(errorHere(ErrorCategory::UnexpectedCharacter, "Expected a value"));
    }

    if (token == "true") {
        return LexedScalar(ScalarPayload(true), token);
    }
    if (token == "false") {
        return LexedScalar(ScalarPayload(false), token);
    }

    // Distinguish date/time from numeric literals:
    // 1) Contains ':' -> time or date-time
    // 2) Starts with digit and '-' appears before exponent 'e'/'E' -> date or date-time
    const bool startsWithRadixPrefix =
        svStartsWith(token, "0x") || svStartsWith(token, "0X")
        || svStartsWith(token, "0o") || svStartsWith(token, "0O")
        || svStartsWith(token, "0b") || svStartsWith(token, "0B");

    if (!startsWithRadixPrefix) {
        if (svContains(token, ':')) {
            return lexDateOrTime(start, token);
        }
        if (lexer_detail::isAsciiDigit(token[0])) {
            const size_t dashPos = token.find('-');
            const size_t expPos = token.find_first_of("eE");
            if (dashPos != StringView::npos && (expPos == StringView::npos || dashPos < expPos)) {
                return lexDateOrTime(start, token);
            }
        }
    }

    return lexNumber(start, token);
}

TOMLKIT11_INLINE Result<LexedScalar, ParseError> Lexer::lexDateOrTime(
    size_t startOffset,
    StringView token
)
{
    const auto makeDtErrFunc = [&](std::string msg) -> Unexpected<ParseError>
        {
            return makeUnexpected(errorAt(
                ErrorCategory::InvalidDateTime,
                startOffset,
                (uint32_t)token.size(),
                std::move(msg)
            ));
        };

    const auto parseTimePartFunc = [&](StringView sv, size_t& idx, LocalTime& outTime) -> Result<void, ParseError>
        {
            uint32_t hh = 0;
            uint32_t mm = 0;
            if (!lexer_detail::parseFixedDigits(sv, idx, 2, hh) || idx + 2 >= sv.size() || sv[idx + 2] != ':') {
                return makeDtErrFunc("Invalid time format; expected HH:MM");
            }
            if (!lexer_detail::parseFixedDigits(sv, idx + 3, 2, mm)) {
                return makeDtErrFunc("Invalid minute digits in time literal");
            }
            idx += 5;

            outTime.hour = (uint8_t)hh;
            outTime.minute = (uint8_t)mm;
            outTime.second = 0;
            outTime.nanosecond = 0;
            outTime.subsecondDigits = 0;
            outTime.hasSeconds = false;

            if (idx < sv.size() && sv[idx] == ':') {
                ++idx;
                uint32_t ss = 0;
                if (!lexer_detail::parseFixedDigits(sv, idx, 2, ss)) {
                    return makeDtErrFunc("Invalid second digits in time literal");
                }
                idx += 2;
                outTime.second = (uint8_t)ss;
                outTime.hasSeconds = true;

                if (idx < sv.size() && sv[idx] == '.') {
                    ++idx;
                    const size_t fracStart = idx;
                    while (idx < sv.size() && lexer_detail::isAsciiDigit(sv[idx])) {
                        ++idx;
                    }
                    const size_t fracLen = idx - fracStart;
                    if (fracLen == 0) {
                        return makeDtErrFunc("Fractional seconds must contain at least one digit after '.'");
                    }
                    uint32_t nanos = 0;
                    const size_t takeDigits = (fracLen < 9) ? fracLen : 9;
                    for (size_t i = 0; i < takeDigits; ++i) {
                        nanos = nanos * 10u + (uint32_t)(sv[fracStart + i] - '0');
                    }
                    for (size_t i = takeDigits; i < 9; ++i) {
                        nanos *= 10u;
                    }
                    outTime.nanosecond = nanos;
                    outTime.subsecondDigits = (uint8_t)takeDigits;
                }
            }
            else if (m_options.spec == SpecVersion::v1_0) {
                return makeUnexpected(errorAt(
                    ErrorCategory::SpecRestriction,
                    startOffset,
                    (uint32_t)token.size(),
                    "Omitting seconds in time/datetime requires TOML v1.1"
                ));
            }

            if (!outTime.isValid()) {
                return makeDtErrFunc("Time value out of valid RFC 3339 range");
            }
            return Result<void, ParseError>();
        };

    // 1. Local time: HH:MM[:SS[.frac]]
    if (token.size() >= 5 && token[2] == ':') {
        size_t idx = 0;
        LocalTime lt;
        Result<void, ParseError> res = parseTimePartFunc(token, idx, lt);
        if (!res) {
            return makeUnexpected(std::move(res.error()));
        }
        if (idx != token.size()) {
            return makeDtErrFunc("Unexpected trailing characters in local time (offsets require a date part)");
        }
        return LexedScalar(ScalarPayload(lt), token);
    }

    // 2. Date-prefixed: YYYY-MM-DD
    uint32_t year = 0;
    uint32_t month = 0;
    uint32_t day = 0;
    if (token.size() < 10
        || !lexer_detail::parseFixedDigits(token, 0, 4, year)
        || token[4] != '-'
        || !lexer_detail::parseFixedDigits(token, 5, 2, month)
        || token[7] != '-'
        || !lexer_detail::parseFixedDigits(token, 8, 2, day))
    {
        return makeDtErrFunc("Invalid date format; expected YYYY-MM-DD");
    }

    const LocalDate ld((uint16_t)year, (uint8_t)month, (uint8_t)day);
    if (!ld.isValid()) {
        return makeDtErrFunc("Calendar date out of valid range (check month/day or leap year)");
    }

    if (token.size() == 10) {
        return LexedScalar(ScalarPayload(ld), token);
    }

    const char delim = token[10];
    if (delim != 'T' && delim != 't' && delim != ' ') {
        return makeDtErrFunc("Invalid date-time separator; expected 'T', 't', or space");
    }

    size_t idx = 11;
    LocalTime lt;
    Result<void, ParseError> res = parseTimePartFunc(token, idx, lt);
    if (!res) {
        return makeUnexpected(std::move(res.error()));
    }

    if (idx == token.size()) {
        const LocalDateTime ldt(ld, lt, delim);
        return LexedScalar(ScalarPayload(ldt), token);
    }

    // 3. Timezone offset: Z / z / +HH:MM / -HH:MM
    OffsetDateTime odt(ld, lt, 0, true, false, delim, 'Z');

    const char tzLead = token[idx];
    if (tzLead == 'Z' || tzLead == 'z') {
        ++idx;
        if (idx != token.size()) {
            return makeDtErrFunc("Unexpected characters after timezone 'Z'");
        }
        odt.isUtcZ = true;
        odt.zChar = tzLead;
        return LexedScalar(ScalarPayload(odt), token);
    }

    if (tzLead == '+' || tzLead == '-') {
        ++idx;
        uint32_t offH = 0;
        uint32_t offM = 0;
        if (idx + 5 != token.size()
            || !lexer_detail::parseFixedDigits(token, idx, 2, offH)
            || token[idx + 2] != ':'
            || !lexer_detail::parseFixedDigits(token, idx + 3, 2, offM))
        {
            return makeDtErrFunc("Invalid timezone offset; expected +HH:MM or -HH:MM");
        }
        if (offH > 23 || offM > 59) {
            return makeDtErrFunc("Timezone offset hours/minutes out of valid range (00:00..23:59)");
        }
        const int16_t totalMins = (int16_t)(offH * 60u + offM);
        odt.isUtcZ = false;
        odt.negativeZeroOffset = (tzLead == '-' && totalMins == 0);
        odt.offsetMinutes = (tzLead == '-') ? (int16_t)(-totalMins) : totalMins;
        return LexedScalar(ScalarPayload(odt), token);
    }

    return makeDtErrFunc("Unexpected characters after time component in datetime literal");
}

TOMLKIT11_INLINE Result<LexedScalar, ParseError> Lexer::lexNumber(
    size_t startOffset,
    StringView token
)
{
    const auto makeNumErrFunc = [&](std::string msg) -> Unexpected<ParseError>
        {
            return makeUnexpected(errorAt(
                ErrorCategory::InvalidNumber,
                startOffset,
                (uint32_t)token.size(),
                std::move(msg)
            ));
        };

    StringView rest = token;
    char sign = '\0';
    if (svStartsWith(rest, '+') || svStartsWith(rest, '-')) {
        sign = rest[0];
        rest.remove_prefix(1);
    }

    if (rest.empty()) {
        return makeNumErrFunc("Incomplete numeric sign without digits");
    }

    // Special float literals: inf / nan (must be strictly lowercase).
    if (rest == "inf") {
        const double val = (sign == '-')
            ? -std::numeric_limits<double>::infinity()
            : std::numeric_limits<double>::infinity();
        return LexedScalar(ScalarPayload(val), token);
    }
    if (rest == "nan") {
        const double val = (sign == '-')
            ? -std::numeric_limits<double>::quiet_NaN()
            : std::numeric_limits<double>::quiet_NaN();
        return LexedScalar(ScalarPayload(val), token);
    }

    // Non-decimal integers: 0x, 0o, 0b (no sign allowed, prefix must be lowercase).
    if (rest.size() >= 2 && rest[0] == '0'
        && (rest[1] == 'x' || rest[1] == 'X'
            || rest[1] == 'o' || rest[1] == 'O'
            || rest[1] == 'b' || rest[1] == 'B'))
    {
        if (sign != '\0') {
            return makeNumErrFunc("Radix-prefixed integers (0x, 0o, 0b) cannot have a '+' or '-' sign");
        }
        const char prefixCh = rest[1];
        if (prefixCh == 'X' || prefixCh == 'O' || prefixCh == 'B') {
            return makeNumErrFunc("Radix prefix must be lowercase ('0x', '0o', or '0b')");
        }

        IntegerRadix radix = IntegerRadix::Hexadecimal;
        uint32_t base = 16;
        if (prefixCh == 'o') {
            radix = IntegerRadix::Octal;
            base = 8;
        }
        else if (prefixCh == 'b') {
            radix = IntegerRadix::Binary;
            base = 2;
        }

        const StringView digits = rest.substr(2);
        if (digits.empty()) {
            return makeNumErrFunc("Missing digits after integer radix prefix");
        }

        const auto isValidRadixDigitFunc = [&](char ch) -> bool
            {
                if (base == 2) {
                    return ch == '0' || ch == '1';
                }
                if (base == 8) {
                    return ch >= '0' && ch <= '7';
                }
                return lexer_detail::isHexDigit(ch);
            };

        uint64_t acc = 0;
        const uint64_t kMaxSigned = (uint64_t)(std::numeric_limits<int64_t>::max)();
        bool prevUnderscore = false;

        for (size_t i = 0; i < digits.size(); ++i) {
            const char ch = digits[i];
            if (ch == '_') {
                if (i == 0 || i + 1 == digits.size() || prevUnderscore) {
                    return makeNumErrFunc("Underscores in integer literal must be surrounded by valid digits");
                }
                prevUnderscore = true;
                continue;
            }
            prevUnderscore = false;

            if (!isValidRadixDigitFunc(ch)) {
                std::string msg = "Invalid digit '";
                msg.push_back(ch);
                msg += "' for base-";
                appendUint(msg, (uint64_t)base);
                msg += " integer literal";
                return makeNumErrFunc(std::move(msg));
            }
            const uint64_t d = (uint64_t)lexer_detail::hexValue(ch);
            if (acc > (kMaxSigned - d) / base) {
                return makeNumErrFunc("Integer literal exceeds 64-bit signed integer range");
            }
            acc = acc * base + d;
        }

        return LexedScalar(ScalarPayload((int64_t)acc), token, radix);
    }

    // Check if literal is a float (contains '.' or 'e'/'E').
    const bool isFloat = svContains(rest, '.') || svContains(rest, 'e') || svContains(rest, 'E');

    if (!isFloat) {
        if (rest.size() > 1 && rest[0] == '0') {
            return makeNumErrFunc("Leading zeros are not allowed in decimal integer literals");
        }

        uint64_t acc = 0;
        const uint64_t kInt64MaxU = (uint64_t)(std::numeric_limits<int64_t>::max)();
        const uint64_t maxMag = (sign == '-') ? (kInt64MaxU + 1ull) : kInt64MaxU;

        bool prevUnderscore = false;
        for (size_t i = 0; i < rest.size(); ++i) {
            const char ch = rest[i];
            if (ch == '_') {
                if (i == 0 || i + 1 == rest.size() || prevUnderscore) {
                    return makeNumErrFunc("Underscores in decimal integer must be surrounded by digits");
                }
                prevUnderscore = true;
                continue;
            }
            prevUnderscore = false;

            if (!lexer_detail::isAsciiDigit(ch)) {
                std::string msg = "Invalid character '";
                msg.push_back(ch);
                msg += "' in numeric literal";
                return makeNumErrFunc(std::move(msg));
            }
            const uint64_t d = (uint64_t)(ch - '0');
            if (acc > (maxMag - d) / 10ull) {
                return makeNumErrFunc("Decimal integer literal overflows 64-bit signed integer range");
            }
            acc = acc * 10ull + d;
        }

        int64_t signedVal = 0;
        if (sign == '-') {
            if (acc == kInt64MaxU + 1ull) {
                signedVal = (std::numeric_limits<int64_t>::min)();
            }
            else {
                signedVal = -(int64_t)acc;
            }
        }
        else {
            signedVal = (int64_t)acc;
        }

        return LexedScalar(ScalarPayload(signedVal), token, IntegerRadix::Decimal);
    }

    // Float syntax validation: float-int-part ( exp / frac [ exp ] )
    const auto validateDigitSeqFunc = [](StringView part, bool allowLeadingZero) -> bool
        {
            if (part.empty()) {
                return false;
            }
            if (!allowLeadingZero && part.size() > 1 && part[0] == '0') {
                return false;
            }
            bool prevUs = false;
            for (size_t i = 0; i < part.size(); ++i) {
                const char ch = part[i];
                if (ch == '_') {
                    if (i == 0 || i + 1 == part.size() || prevUs) {
                        return false;
                    }
                    prevUs = true;
                }
                else if (lexer_detail::isAsciiDigit(ch)) {
                    prevUs = false;
                }
                else {
                    return false;
                }
            }
            return true;
        };

    const size_t dotPos = rest.find('.');
    const size_t expPos = rest.find_first_of("eE");

    if (dotPos != StringView::npos && expPos != StringView::npos && expPos < dotPos) {
        return makeNumErrFunc("Decimal point cannot appear in exponent part of float literal");
    }

    const size_t intEnd = (dotPos != StringView::npos) ? dotPos : expPos;
    const StringView intPart = rest.substr(0, intEnd);
    if (!validateDigitSeqFunc(intPart, false)) {
        return makeNumErrFunc("Invalid integer part in float literal");
    }

    if (dotPos != StringView::npos) {
        const size_t fracStart = dotPos + 1;
        const size_t fracEnd = (expPos != StringView::npos) ? expPos : rest.size();
        if (rest.find('.', fracStart) != StringView::npos) {
            return makeNumErrFunc("Multiple decimal points in float literal");
        }
        const StringView fracPart = rest.substr(fracStart, fracEnd - fracStart);
        if (!validateDigitSeqFunc(fracPart, true)) {
            return makeNumErrFunc("Invalid fractional part in float literal");
        }
    }

    if (expPos != StringView::npos) {
        if (rest.find_first_of("eE", expPos + 1) != StringView::npos) {
            return makeNumErrFunc("Multiple exponent markers in float literal");
        }
        StringView expPart = rest.substr(expPos + 1);
        if (svStartsWith(expPart, '+') || svStartsWith(expPart, '-')) {
            expPart.remove_prefix(1);
        }
        if (!validateDigitSeqFunc(expPart, true)) {
            return makeNumErrFunc("Invalid exponent digits in float literal");
        }
    }

    std::string clean;
    clean.reserve(token.size());
    if (sign == '-') {
        clean.push_back('-');
    }
    for (size_t i = 0; i < rest.size(); ++i) {
        const char ch = rest[i];
        if (ch != '_') {
            clean.push_back(ch);
        }
    }

    char* pEnd = nullptr;
    double floatVal = lexer_detail::parseDoubleCLocale(clean, pEnd);
    if (pEnd != clean.c_str() + clean.size()) {
        return makeNumErrFunc("Failed to parse float literal");
    }

    // Preserve negative zero on `-0.0` or underflow (and +/-inf overflow per TOML v1.1 #1058).
    if (sign == '-' && floatVal == 0.0 && !std::signbit(floatVal)) {
        floatVal = -0.0;
    }

    return LexedScalar(ScalarPayload(floatVal), token);
}

} // namespace detail

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_DETAIL_LEXER_IPP

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
