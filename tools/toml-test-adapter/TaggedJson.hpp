#pragma once

#ifndef TOMLKIT11_TOOLS_TAGGED_JSON_HPP
#define TOMLKIT11_TOOLS_TAGGED_JSON_HPP

#include <tomlkit11/tomlkit11.hpp>

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <locale.h>
#    include <stdio.h>
#    include <stdlib.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#    include <locale.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <xlocale.h>
#elif defined(__linux__) || defined(__unix__)
#    include <locale.h>
#    include <stdio.h>
#    include <stdlib.h>
#endif

namespace tomlkit
{
namespace test_adapter
{

namespace adapter_detail
{

inline std::string svToStdString(StringView sv)
{
    return std::string(sv.data(), sv.size());
}

inline void appendInt64Dec(std::string& out, int64_t val)
{
    uint64_t mag = 0;
    if (val < 0) {
        out.push_back('-');
        mag = (uint64_t)(-(val + 1)) + 1ULL;
    }
    else {
        mag = (uint64_t)val;
    }

    char digits[32];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + (mag % 10ULL));
        mag /= 10ULL;
    }
    while (mag > 0ULL);

    while (n > 0) {
        out.push_back(digits[--n]);
    }
}

inline int64_t parseStrictInt64(StringView raw)
{
    if (raw.empty()) {
        throw std::runtime_error("Empty integer in tagged JSON");
    }

    bool negative = false;
    size_t pos = 0;
    if (raw[0] == '-') {
        negative = true;
        pos = 1;
    }
    else if (raw[0] == '+') {
        pos = 1;
    }

    if (pos >= raw.size()) {
        throw std::runtime_error("Invalid integer in tagged JSON: " + svToStdString(raw));
    }

    const uint64_t limit = negative
        ? ((uint64_t)(std::numeric_limits<int64_t>::max)() + 1ULL)
        : (uint64_t)(std::numeric_limits<int64_t>::max)();

    uint64_t acc = 0;
    for (; pos < raw.size(); ++pos) {
        const char c = raw[pos];
        if (c < '0' || c > '9') {
            throw std::runtime_error("Invalid integer in tagged JSON: " + svToStdString(raw));
        }
        const uint64_t digit = (uint64_t)(c - '0');
        if (acc > (limit - digit) / 10ULL) {
            throw std::runtime_error("Integer out of int64 range in tagged JSON: " + svToStdString(raw));
        }
        acc = acc * 10ULL + digit;
    }

    if (negative) {
        if (acc == limit) {
            return (std::numeric_limits<int64_t>::min)();
        }
        return -(int64_t)acc;
    }
    return (int64_t)acc;
}

inline double parseDoubleLocaleConvFallback(const std::string& clean, char*& outEnd) noexcept
{
    const std::lconv* pConv = std::localeconv();
    if (pConv != nullptr && pConv->decimal_point != nullptr && pConv->decimal_point[0] != '\0') {
        const char dec = pConv->decimal_point[0];
        if (dec != '.') {
            std::string patched = clean;
            for (size_t i = 0; i < patched.size(); ++i) {
                if (patched[i] == '.') {
                    patched[i] = dec;
                }
            }
            char* pLocalEnd = nullptr;
            errno = 0;
            const double val = std::strtod(patched.c_str(), &pLocalEnd);
            const size_t consumed = (pLocalEnd != nullptr) ? (size_t)(pLocalEnd - patched.c_str()) : 0;
            outEnd = (char*)(clean.c_str() + consumed);
            return val;
        }
    }
    errno = 0;
    return std::strtod(clean.c_str(), &outEnd);
}

#if defined(_WIN32)
struct AdapterCLocaleHolder
{
    _locale_t loc;

    AdapterCLocaleHolder() noexcept
        : loc(_create_locale(LC_NUMERIC, "C"))
    { }

    ~AdapterCLocaleHolder()
    {
        if (loc != nullptr) {
            _free_locale(loc);
        }
    }

    AdapterCLocaleHolder(const AdapterCLocaleHolder&) = delete;
    AdapterCLocaleHolder& operator=(const AdapterCLocaleHolder&) = delete;
};

inline double parseDoubleCLocale(const std::string& clean, char*& outEnd) noexcept
{
    static const AdapterCLocaleHolder cLocale;
    if (cLocale.loc != nullptr) {
        errno = 0;
        return _strtod_l(clean.c_str(), &outEnd, cLocale.loc);
    }
    return parseDoubleLocaleConvFallback(clean, outEnd);
}

inline int formatDoubleCLocale(char* buf, size_t bufSize, int pass, double v) noexcept
{
    static const AdapterCLocaleHolder cLocale;
    if (cLocale.loc != nullptr) {
#if defined(_MSC_VER)
        switch (pass)
        {
        case 0: return _snprintf_s_l(buf, bufSize, _TRUNCATE, "%.15g", cLocale.loc, v);
        case 1: return _snprintf_s_l(buf, bufSize, _TRUNCATE, "%.16g", cLocale.loc, v);
        default: return _snprintf_s_l(buf, bufSize, _TRUNCATE, "%.17g", cLocale.loc, v);
        }
#else
        switch (pass)
        {
        case 0: return _snprintf_l(buf, bufSize, "%.15g", cLocale.loc, v);
        case 1: return _snprintf_l(buf, bufSize, "%.16g", cLocale.loc, v);
        default: return _snprintf_l(buf, bufSize, "%.17g", cLocale.loc, v);
        }
#endif
    }
    switch (pass)
    {
    case 0: return std::snprintf(buf, bufSize, "%.15g", v);
    case 1: return std::snprintf(buf, bufSize, "%.16g", v);
    default: return std::snprintf(buf, bufSize, "%.17g", v);
    }
}
#elif defined(LC_NUMERIC_MASK) && (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
struct AdapterCLocaleHolder
{
    locale_t loc;

    AdapterCLocaleHolder() noexcept
        : loc(::newlocale(LC_NUMERIC_MASK, "C", (locale_t)0))
    { }

    ~AdapterCLocaleHolder()
    {
        if (loc != (locale_t)0) {
            ::freelocale(loc);
        }
    }

    AdapterCLocaleHolder(const AdapterCLocaleHolder&) = delete;
    AdapterCLocaleHolder& operator=(const AdapterCLocaleHolder&) = delete;
};

inline double parseDoubleCLocale(const std::string& clean, char*& outEnd) noexcept
{
    static const AdapterCLocaleHolder cLocale;
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

#if !defined(_WIN32)
inline int formatDoubleCLocale(char* buf, size_t bufSize, int pass, double v) noexcept
{
    int n = 0;
    switch (pass)
    {
    case 0: n = std::snprintf(buf, bufSize, "%.15g", v); break;
    case 1: n = std::snprintf(buf, bufSize, "%.16g", v); break;
    default: n = std::snprintf(buf, bufSize, "%.17g", v); break;
    }
    if (n > 0) {
        const std::lconv* pConv = std::localeconv();
        if (pConv != nullptr && pConv->decimal_point != nullptr && pConv->decimal_point[0] != '\0') {
            const char dec = pConv->decimal_point[0];
            if (dec != '.') {
                for (int i = 0; i < n && (size_t)i < bufSize; ++i) {
                    if (buf[i] == dec) {
                        buf[i] = '.';
                    }
                }
            }
        }
    }
    return n;
}
#endif

inline std::string formatDoubleShortestRoundtrip(double v)
{
    if (std::isnan(v)) {
        return "nan";
    }
    if (std::isinf(v)) {
        return v < 0.0 ? "-inf" : "inf";
    }
    if (v == 0.0) {
        return std::signbit(v) ? "-0.0" : "0.0";
    }

    std::string out;
    char buf[128];

    for (int i = 0; i < 3; ++i) {
        const int n = formatDoubleCLocale(buf, sizeof(buf), i, v);
        if (n <= 0 || (size_t)n >= sizeof(buf)) {
            continue;
        }
        std::string candidate(buf, (size_t)n);
        if (i < 2) {
            char* pEnd = nullptr;
            const double reparsed = parseDoubleCLocale(candidate, pEnd);
            if (pEnd == candidate.c_str() + candidate.size() && reparsed == v) {
                out = std::move(candidate);
                break;
            }
        }
        else {
            out = std::move(candidate);
        }
    }

    if (out.empty()) {
        out = "0.0";
    }
    if (out.find('.') == std::string::npos && out.find('e') == std::string::npos && out.find('E') == std::string::npos) {
        out.append(".0");
    }
    return out;
}

} // namespace adapter_detail

struct JsonNode
{
    enum class Kind : uint8_t
    {
        String,
        Array,
        Object
    };

    Kind kind;
    std::string strVal;
    std::vector<JsonNode> arrVal;
    std::vector<std::pair<std::string, JsonNode>> objVal;

    JsonNode()
        : kind(Kind::String)
    { }

    explicit JsonNode(Kind k)
        : kind(k)
    { }

    JsonNode(Kind k, std::string s)
        : kind(k)
        , strVal(std::move(s))
    { }

    const JsonNode* findProp(StringView key) const noexcept
    {
        for (size_t i = 0; i < objVal.size(); ++i) {
            if (StringView(objVal[i].first) == key) {
                return &objVal[i].second;
            }
        }
        return nullptr;
    }
};

inline void appendJsonEscaped(std::string& out, StringView s)
{
    static const char kHexDigits[] = "0123456789abcdef";
    out.push_back('"');
    for (size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        const uint8_t b = (uint8_t)ch;
        switch (ch)
        {
        case '"':  out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
            if (b < 0x20u) {
                out.append("\\u00");
                out.push_back(kHexDigits[(b >> 4) & 0x0Fu]);
                out.push_back(kHexDigits[b & 0x0Fu]);
            }
            else {
                out.push_back(ch);
            }
            break;
        }
    }
    out.push_back('"');
}

inline void emitTagObject(std::string& out, StringView tag, StringView val)
{
    out.append("{\"type\":");
    appendJsonEscaped(out, tag);
    out.append(",\"value\":");
    appendJsonEscaped(out, val);
    out.push_back('}');
}

// Encodes `tomlkit::Item` into official `toml-test` tagged JSON text.
inline void emitTaggedJson(std::string& out, const Item& item)
{
    switch (item.kind())
    {
    case ItemKind::Empty:
        emitTagObject(out, "string", "");
        return;
    case ItemKind::Boolean:
        emitTagObject(out, "bool", *item.ifBoolean() ? "true" : "false");
        return;
    case ItemKind::Integer:
    {
        std::string numStr;
        adapter_detail::appendInt64Dec(numStr, *item.ifInteger());
        emitTagObject(out, "integer", numStr);
        return;
    }
    case ItemKind::Float:
        emitTagObject(out, "float", adapter_detail::formatDoubleShortestRoundtrip(*item.ifFloat()));
        return;
    case ItemKind::String:
        emitTagObject(out, "string", *item.ifString());
        return;
    case ItemKind::LocalDate:
        emitTagObject(out, "date-local", item.ifLocalDate()->toString());
        return;
    case ItemKind::LocalTime:
        emitTagObject(out, "time-local", item.ifLocalTime()->toString(true));
        return;
    case ItemKind::LocalDateTime:
        emitTagObject(out, "datetime-local", item.ifLocalDateTime()->toString(true));
        return;
    case ItemKind::OffsetDateTime:
        emitTagObject(out, "datetime", item.ifOffsetDateTime()->toString(true));
        return;
    case ItemKind::Array:
    {
        out.push_back('[');
        bool first = true;
        for (Item::const_iterator it = item.begin(); it != item.end(); ++it) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            emitTaggedJson(out, *it);
        }
        out.push_back(']');
        return;
    }
    case ItemKind::Table:
    {
        out.push_back('{');
        bool first = true;
        const TableItemsRange<true> entries = item.tableItems();
        for (TableIterator<true> it = entries.begin(); it != entries.end(); ++it) {
            const TableKeyValuePair<true> kv = *it;
            if (!first) {
                out.push_back(',');
            }
            first = false;
            appendJsonEscaped(out, kv.key.name());
            out.push_back(':');
            emitTaggedJson(out, kv.value);
        }
        out.push_back('}');
        return;
    }
    }
}

inline std::string itemToTaggedJson(const Item& doc)
{
    std::string out;
    emitTaggedJson(out, doc);
    return out;
}

// Lightweight self-contained JSON parser tailored for `toml-test` UTF-8 JSON fixtures.
class MiniJsonParser
{
public:
    explicit MiniJsonParser(StringView src) noexcept
        : m_src(src)
        , m_pos(0)
    { }

    JsonNode parseRoot()
    {
        skipWs();
        JsonNode root = parseValue();
        skipWs();
        if (m_pos != m_src.size()) {
            throw std::runtime_error("Trailing data in JSON");
        }
        return root;
    }

private:
    void skipWs() noexcept
    {
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            }
            else {
                break;
            }
        }
    }

    char peek() const
    {
        if (m_pos >= m_src.size()) {
            throw std::runtime_error("Unexpected EOF in JSON");
        }
        return m_src[m_pos];
    }

    static uint32_t hexDigit(char c)
    {
        if (c >= '0' && c <= '9') {
            return (uint32_t)(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return (uint32_t)(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return (uint32_t)(c - 'A' + 10);
        }
        throw std::runtime_error("Invalid hex in JSON escape");
    }

    uint16_t parseU16Hex()
    {
        if (m_pos + 4 > m_src.size()) {
            throw std::runtime_error("Truncated \\u escape in JSON");
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 4) | hexDigit(m_src[m_pos++]);
        }
        return (uint16_t)v;
    }

    std::string parseString()
    {
        if (peek() != '"') {
            throw std::runtime_error("Expected '\"' in JSON");
        }
        ++m_pos;
        std::string out;
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (m_pos >= m_src.size()) {
                    throw std::runtime_error("Truncated escape in JSON");
                }
                const char esc = m_src[m_pos++];
                switch (esc)
                {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    const uint16_t u1 = parseU16Hex();
                    if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
                        if (m_pos + 2 > m_src.size() || m_src[m_pos] != '\\' || m_src[m_pos + 1] != 'u') {
                            throw std::runtime_error("Unpaired high surrogate in JSON");
                        }
                        m_pos += 2;
                        const uint16_t u2 = parseU16Hex();
                        if (u2 < 0xDC00u || u2 > 0xDFFFu) {
                            throw std::runtime_error("Invalid low surrogate in JSON");
                        }
                        const char32_t cp = 0x10000u + (((char32_t)(u1 - 0xD800u) << 10) | (char32_t)(u2 - 0xDC00u));
                        appendUtf8Codepoint(out, cp);
                    }
                    else {
                        appendUtf8Codepoint(out, (char32_t)u1);
                    }
                    break;
                }
                default:
                    throw std::runtime_error("Unknown escape in JSON");
                }
            }
            else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated string in JSON");
    }

    JsonNode parseValue()
    {
        skipWs();
        const char c = peek();
        if (c == '"') {
            return JsonNode(JsonNode::Kind::String, parseString());
        }
        if (c == '[') {
            ++m_pos;
            JsonNode node(JsonNode::Kind::Array);
            skipWs();
            if (peek() == ']') {
                ++m_pos;
                return node;
            }
            while (true) {
                node.arrVal.push_back(parseValue());
                skipWs();
                if (peek() == ',') {
                    ++m_pos;
                    continue;
                }
                if (peek() == ']') {
                    ++m_pos;
                    break;
                }
                throw std::runtime_error("Expected ',' or ']' in JSON array");
            }
            return node;
        }
        if (c == '{') {
            ++m_pos;
            JsonNode node(JsonNode::Kind::Object);
            skipWs();
            if (peek() == '}') {
                ++m_pos;
                return node;
            }
            while (true) {
                skipWs();
                std::string k = parseString();
                skipWs();
                if (peek() != ':') {
                    throw std::runtime_error("Expected ':' in JSON object");
                }
                ++m_pos;
                JsonNode v = parseValue();
                node.objVal.emplace_back(std::move(k), std::move(v));
                skipWs();
                if (peek() == ',') {
                    ++m_pos;
                    continue;
                }
                if (peek() == '}') {
                    ++m_pos;
                    break;
                }
                throw std::runtime_error("Expected ',' or '}' in JSON object");
            }
            return node;
        }
        throw std::runtime_error("Unexpected token in toml-test JSON");
    }

    StringView m_src;
    size_t m_pos;
};

// Reconstructs `tomlkit::Item` from an official `toml-test` tagged JSON node.
inline Item taggedJsonNodeToItem(const JsonNode& node, bool isRoot = false)
{
    if (node.kind == JsonNode::Kind::Array) {
        bool allTables = !node.arrVal.empty();
        for (size_t i = 0; i < node.arrVal.size(); ++i) {
            const JsonNode& el = node.arrVal[i];
            if (el.kind != JsonNode::Kind::Object) {
                allTables = false;
                break;
            }
            if (el.objVal.size() == 2) {
                const JsonNode* pType = el.findProp("type");
                const JsonNode* pVal = el.findProp("value");
                if (pType != nullptr && pVal != nullptr
                    && pType->kind == JsonNode::Kind::String
                    && pVal->kind == JsonNode::Kind::String)
                {
                    allTables = false;
                    break;
                }
            }
        }

        if (allTables) {
            Item aot = Item::arrayOfTables();
            for (size_t i = 0; i < node.arrVal.size(); ++i) {
                Item elemTbl = taggedJsonNodeToItem(node.arrVal[i], false);
                elemTbl.setTableStyle(TableStyle::ArrayOfTablesElement);
                aot.push_back(std::move(elemTbl));
            }
            return aot;
        }

        Item arr = Item::array();
        for (size_t i = 0; i < node.arrVal.size(); ++i) {
            Item child = taggedJsonNodeToItem(node.arrVal[i], false);
            if (child.isTable()) {
                child.setTableStyle(TableStyle::Inline);
            }
            arr.push_back(std::move(child));
        }
        return arr;
    }

    if (node.kind == JsonNode::Kind::Object) {
        if (node.objVal.size() == 2) {
            const JsonNode* pType = node.findProp("type");
            const JsonNode* pVal = node.findProp("value");
            if (pType != nullptr && pVal != nullptr
                && pType->kind == JsonNode::Kind::String
                && pVal->kind == JsonNode::Kind::String)
            {
                const StringView tag(pType->strVal);
                const StringView raw(pVal->strVal);

                if (tag == "string") {
                    return Item(raw);
                }
                if (tag == "bool") {
                    return Item(raw == "true");
                }
                if (tag == "integer") {
                    return Item(adapter_detail::parseStrictInt64(raw));
                }
                if (tag == "float") {
                    if (raw == "inf" || raw == "+inf") {
                        return Item(std::numeric_limits<double>::infinity());
                    }
                    if (raw == "-inf") {
                        return Item(-std::numeric_limits<double>::infinity());
                    }
                    if (raw == "nan" || raw == "+nan") {
                        return Item(std::numeric_limits<double>::quiet_NaN());
                    }
                    if (raw == "-nan") {
                        return Item(-std::numeric_limits<double>::quiet_NaN());
                    }
                    StringView cleanView = raw;
                    if (!cleanView.empty() && cleanView[0] == '+') {
                        cleanView.remove_prefix(1);
                    }
                    const std::string clean = adapter_detail::svToStdString(cleanView);
                    if (clean.empty()) {
                        throw std::runtime_error("Empty float in tagged JSON");
                    }
                    char* pEnd = nullptr;
                    double dv = adapter_detail::parseDoubleCLocale(clean, pEnd);
                    if (pEnd != clean.c_str() + clean.size()) {
                        throw std::runtime_error("Invalid float in tagged JSON: " + clean);
                    }
                    if (clean[0] == '-' && dv == 0.0 && !std::signbit(dv)) {
                        dv = -0.0;
                    }
                    return Item(dv);
                }
                if (tag == "datetime" || tag == "datetime-local" || tag == "date-local" || tag == "time-local") {
                    detail::Lexer lex(raw);
                    const Result<detail::LexedScalar, ParseError> scRes = lex.lexScalar();
                    if (!scRes) {
                        throw std::runtime_error("Invalid datetime in tagged JSON: " + adapter_detail::svToStdString(raw));
                    }
                    switch (scRes->payload.kind)
                    {
                    case detail::ScalarPayload::Kind::Boolean:
                        return Item(scRes->payload.asBoolean());
                    case detail::ScalarPayload::Kind::Integer:
                        return Item(scRes->payload.asInteger());
                    case detail::ScalarPayload::Kind::Float:
                        return Item(scRes->payload.asFloat());
                    case detail::ScalarPayload::Kind::LocalDate:
                        return Item(scRes->payload.asLocalDate());
                    case detail::ScalarPayload::Kind::LocalTime:
                        return Item(scRes->payload.asLocalTime());
                    case detail::ScalarPayload::Kind::LocalDateTime:
                        return Item(scRes->payload.asLocalDateTime());
                    case detail::ScalarPayload::Kind::OffsetDateTime:
                        return Item(scRes->payload.asOffsetDateTime());
                    }
                    throw std::runtime_error("Unsupported scalar payload kind in tagged JSON");
                }
                throw std::runtime_error("Unknown tag '" + adapter_detail::svToStdString(tag) + "' in tagged JSON");
            }
        }

        Item tbl = isRoot ? Item::document() : Item::table(TableStyle::Standard);
        for (size_t i = 0; i < node.objVal.size(); ++i) {
            tbl.insertOrAssign(StringView(node.objVal[i].first), taggedJsonNodeToItem(node.objVal[i].second, false));
        }
        return tbl;
    }

    throw std::runtime_error("Unexpected bare string node in tagged JSON");
}

inline Item taggedJsonToItem(StringView jsonText)
{
    MiniJsonParser parser(jsonText);
    const JsonNode root = parser.parseRoot();
    return taggedJsonNodeToItem(root, true);
}

// Semantic equivalence comparison with float tolerance matching official `toml-test` rules.
inline bool itemsMatchForTomlTest(const Item& a, const Item& b) noexcept
{
    if (a.kind() != b.kind()) {
        return false;
    }
    if (a.isFloat()) {
        const double x = *a.ifFloat();
        const double y = *b.ifFloat();
        if (std::isnan(x) && std::isnan(y)) {
            return true;
        }
        if (std::isinf(x) || std::isinf(y)) {
            return x == y;
        }
        if (x == y) {
            return true;
        }
        const double diff = std::fabs(x - y);
        const double scale = (std::max)(1.0, (std::max)(std::fabs(x), std::fabs(y)));
        return diff <= 1e-12 * scale;
    }
    if (a.isArray()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (!itemsMatchForTomlTest(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }
    if (a.isTable()) {
        if (a.size() != b.size()) {
            return false;
        }
        const TableItemsRange<true> entries = a.tableItems();
        for (TableIterator<true> it = entries.begin(); it != entries.end(); ++it) {
            const TableKeyValuePair<true> kv = *it;
            const Item* pOther = b.find(kv.key.name());
            if (pOther == nullptr || !itemsMatchForTomlTest(kv.value, *pOther)) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

} // namespace test_adapter
} // namespace tomlkit

#endif // TOMLKIT11_TOOLS_TAGGED_JSON_HPP
