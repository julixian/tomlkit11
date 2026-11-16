#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#include <string>
#include <utility>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

namespace
{
    inline std::string sanitizeSingleLineComment(StringView text)
    {
        std::string clean;
        clean.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            const uint8_t b = (uint8_t)ch;
            if (ch == '\r' || ch == '\n') {
                clean.push_back(' ');
            }
            else if ((b < 0x20u && ch != '\t') || b == 0x7Fu) {
                continue;
            }
            else {
                clean.push_back(ch);
            }
        }
        return clean;
    }

    inline StringView detectTrailingNewline(StringView s) noexcept
    {
        if (detail::svEndsWith(s, "\r\n")) {
            return "\r\n";
        }
        if (detail::svEndsWith(s, '\n')) {
            return "\n";
        }
        return "";
    }
} // namespace

TOMLKIT11_INLINE std::vector<std::string> Decor::leadingComments() const
{
    std::vector<std::string> result;
    size_t i = 0;
    while (i < prefix.size()) {
        while (i < prefix.size() && (prefix[i] == ' ' || prefix[i] == '\t' || prefix[i] == '\r' || prefix[i] == '\n')) {
            ++i;
        }
        if (i < prefix.size() && prefix[i] == '#') {
            ++i;
            if (i < prefix.size() && prefix[i] == ' ') {
                ++i;
            }
            const size_t start = i;
            while (i < prefix.size() && prefix[i] != '\r' && prefix[i] != '\n') {
                ++i;
            }
            result.emplace_back(prefix.substr(start, i - start));
        }
        else {
            while (i < prefix.size() && prefix[i] != '\n') {
                ++i;
            }
        }
    }
    return result;
}

TOMLKIT11_INLINE void Decor::addLeadingComment(StringView commentText, StringView newline)
{
    const std::string clean = sanitizeSingleLineComment(commentText);

    // Preserve indentation on the final line of prefix by inserting the comment line right before it.
    const size_t lastNl = prefix.rfind('\n');
    const size_t indentStart = (lastNl == std::string::npos) ? 0 : (lastNl + 1);
    const StringView indent = StringView(prefix).substr(indentStart);

    std::string commentLine;
    detail::appendSv(commentLine, indent);
    commentLine.push_back('#');
    if (!clean.empty()) {
        commentLine.push_back(' ');
        commentLine.append(clean);
    }
    detail::appendSv(commentLine, newline);

    prefix.insert(indentStart, commentLine);
}

TOMLKIT11_INLINE void Decor::clearLeadingComments()
{
    const size_t lastNl = prefix.rfind('\n');
    if (lastNl == std::string::npos) {
        const size_t hashPos = prefix.find('#');
        if (hashPos != std::string::npos) {
            prefix.erase(hashPos);
        }
        return;
    }

    // Keep blank lines (lines without '#') and the indentation of the final line.
    std::string rebuilt;
    size_t lineStart = 0;
    while (lineStart <= lastNl) {
        const size_t nlPos = prefix.find('\n', lineStart);
        const size_t lineEnd = (nlPos == std::string::npos) ? prefix.size() : (nlPos + 1);
        const StringView line = StringView(prefix).substr(lineStart, lineEnd - lineStart);
        if (!detail::svContains(line, '#')) {
            detail::appendSv(rebuilt, line);
        }
        lineStart = lineEnd;
    }
    detail::appendSv(rebuilt, StringView(prefix).substr(lastNl + 1));
    prefix = std::move(rebuilt);
}

TOMLKIT11_INLINE Optional<std::string> Decor::inlineComment() const
{
    const size_t hashPos = suffix.find('#');
    if (hashPos == std::string::npos) {
        return kNullopt;
    }
    size_t start = hashPos + 1;
    if (start < suffix.size() && suffix[start] == ' ') {
        ++start;
    }
    size_t end = start;
    while (end < suffix.size() && suffix[end] != '\r' && suffix[end] != '\n') {
        ++end;
    }
    return Optional<std::string>(suffix.substr(start, end - start));
}

TOMLKIT11_INLINE void Decor::setInlineComment(StringView commentText)
{
    const std::string clean = sanitizeSingleLineComment(commentText);
    const std::string nl = detail::toString(detectTrailingNewline(suffix));

    std::string wsBeforeHash = " ";
    const size_t hashPos = suffix.find('#');
    if (hashPos != std::string::npos) {
        wsBeforeHash = suffix.substr(0, hashPos);
        if (wsBeforeHash.empty()) {
            wsBeforeHash = " ";
        }
    }
    else {
        const size_t contentLen = suffix.size() - nl.size();
        if (contentLen > 0) {
            wsBeforeHash = suffix.substr(0, contentLen);
        }
    }

    suffix.clear();
    suffix.append(wsBeforeHash);
    suffix.push_back('#');
    if (!clean.empty()) {
        suffix.push_back(' ');
        suffix.append(clean);
    }
    suffix.append(nl);
}

TOMLKIT11_INLINE void Decor::clearInlineComment()
{
    const size_t hashPos = suffix.find('#');
    if (hashPos != std::string::npos) {
        suffix = detail::toString(detectTrailingNewline(suffix));
    }
}

TOMLKIT11_INLINE std::vector<std::string> Key::leadingComments() const
{
    return lineLeadDecor().leadingComments();
}

TOMLKIT11_INLINE void Key::addLeadingComment(StringView text, StringView newline)
{
    lineLeadDecor().addLeadingComment(text, newline);
}

TOMLKIT11_INLINE void Key::clearLeadingComments()
{
    lineLeadDecor().clearLeadingComments();
}

TOMLKIT11_INLINE std::string escapeBasicString(StringView value, bool multiline, SpecVersion spec)
{
    std::string out;
    out.reserve(value.size() + 8);
    size_t consecutiveQuotes = 0;

    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        const uint8_t b = (uint8_t)ch;

        if (ch == '"') {
            if (multiline) {
                ++consecutiveQuotes;
                if (consecutiveQuotes == 3) {
                    out.append("\\\"");
                    consecutiveQuotes = 0;
                }
                else {
                    out.push_back('"');
                }
            }
            else {
                out.append("\\\"");
            }
            continue;
        }
        consecutiveQuotes = 0;

        switch (ch)
        {
        case '\\':
            out.append("\\\\");
            break;
        case '\b':
            out.append("\\b");
            break;
        case '\f':
            out.append("\\f");
            break;
        case '\n':
            if (multiline) {
                out.push_back('\n');
            }
            else {
                out.append("\\n");
            }
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        default:
            if (b < 0x20u || b == 0x7Fu) {
                if (b == 0x1Bu && spec == SpecVersion::v1_1) {
                    out.append("\\e");
                }
                else {
                    out.append("\\u");
                    detail::appendHexUint32Fixed(out, (uint32_t)b, 4, true);
                }
            }
            else {
                out.push_back(ch);
            }
            break;
        }
    }

    // Escape trailing unescaped quote in multiline basic string so it cannot merge with closing """.
    if (multiline && consecutiveQuotes > 0 && !out.empty() && out.back() == '"') {
        out.pop_back();
        out.append("\\\"");
    }

    return out;
}

TOMLKIT11_INLINE std::string formatKeySegment(const KeySegment& seg, SpecVersion spec)
{
    if (!seg.rawRepr.empty()) {
        return seg.rawRepr;
    }

    if (seg.style == KeyStyle::Bare && isValidBareKey(seg.name, spec)) {
        return seg.name;
    }

    if (seg.style == KeyStyle::LiteralQuoted) {
        bool canUseLiteral = (seg.name.find('\'') == std::string::npos);
        if (canUseLiteral) {
            for (size_t i = 0; i < seg.name.size(); ++i) {
                const char ch = seg.name[i];
                const uint8_t b = (uint8_t)ch;
                if ((b < 0x20u && ch != '\t') || b == 0x7Fu) {
                    canUseLiteral = false;
                    break;
                }
            }
        }
        if (canUseLiteral) {
            std::string res;
            res.reserve(seg.name.size() + 2);
            res.push_back('\'');
            res.append(seg.name);
            res.push_back('\'');
            return res;
        }
    }

    std::string res;
    res.reserve(seg.name.size() + 2);
    res.push_back('"');
    res.append(escapeBasicString(seg.name, false, spec));
    res.push_back('"');
    return res;
}

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
