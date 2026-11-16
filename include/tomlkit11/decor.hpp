#pragma once

#ifndef TOMLKIT11_DECOR_HPP
#define TOMLKIT11_DECOR_HPP

#include "spec.hpp"
#include "types.hpp"

#include <string>
#include <utility>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

struct Decor
{
    std::string prefix;
    std::string suffix;

    Decor() = default;

    Decor(std::string p, std::string s = std::string())
        : prefix(std::move(p))
        , suffix(std::move(s))
    { }

    void clear() noexcept
    {
        prefix.clear();
        suffix.clear();
    }

    // Extracts all line-level comment texts from prefix (without leading '#' and single space).
    TOMLKIT11_INLINE std::vector<std::string> leadingComments() const;
    TOMLKIT11_INLINE void addLeadingComment(StringView commentText, StringView newline = "\n");
    TOMLKIT11_INLINE void clearLeadingComments();

    // Extracts or modifies the trailing inline comment in suffix (preserving trailing newline).
    TOMLKIT11_INLINE Optional<std::string> inlineComment() const;
    TOMLKIT11_INLINE void setInlineComment(StringView commentText);
    TOMLKIT11_INLINE void clearInlineComment();
};

inline bool operator==(const Decor& lhs, const Decor& rhs) noexcept
{
    return lhs.prefix == rhs.prefix && lhs.suffix == rhs.suffix;
}

inline bool operator!=(const Decor& lhs, const Decor& rhs) noexcept
{
    return !(lhs == rhs);
}

struct KeySegment
{
    std::string name;
    KeyStyle style = KeyStyle::Bare;
    std::string rawRepr;
    Decor decor{};

    KeySegment() = default;

    explicit KeySegment(std::string keyName, KeyStyle keyStyle = KeyStyle::Bare)
        : name(std::move(keyName))
        , style(keyStyle)
    { }

    KeySegment(
        std::string keyName,
        KeyStyle keyStyle,
        std::string raw,
        Decor segDecor = Decor()
    )
        : name(std::move(keyName))
        , style(keyStyle)
        , rawRepr(std::move(raw))
        , decor(std::move(segDecor))
    { }

    void setName(std::string newName)
    {
        name = std::move(newName);
        rawRepr.clear();
    }
};

inline bool operator==(const KeySegment& lhs, const KeySegment& rhs) noexcept
{
    return lhs.name == rhs.name;
}

inline bool operator!=(const KeySegment& lhs, const KeySegment& rhs) noexcept
{
    return !(lhs == rhs);
}

// Represents a single key in a key-value pair or table header,
// preserving quoting style and whitespace around each dotted segment.
class Key
{
public:
    Key() = default;

    explicit Key(std::string leafName, KeyStyle style = KeyStyle::Bare)
        : m_leaf(std::move(leafName), style)
    { }

    Key(std::vector<KeySegment> dottedPrefix, KeySegment leaf)
        : m_dottedPrefix(std::move(dottedPrefix))
        , m_leaf(std::move(leaf))
    { }

    const std::string& name() const noexcept { return m_leaf.name; }
    void setName(std::string newName) { m_leaf.setName(std::move(newName)); }

    KeyStyle style() const noexcept { return m_leaf.style; }
    void setStyle(KeyStyle style) noexcept
    {
        m_leaf.style = style;
        m_leaf.rawRepr.clear();
    }

    KeySegment& leaf() noexcept { return m_leaf; }
    const KeySegment& leaf() const noexcept { return m_leaf; }

    std::vector<KeySegment>& dottedPrefix() noexcept { return m_dottedPrefix; }
    const std::vector<KeySegment>& dottedPrefix() const noexcept { return m_dottedPrefix; }

    // Leading trivia for the whole line lives on the first dotted segment if present.
    Decor& lineLeadDecor() noexcept
    {
        return m_dottedPrefix.empty() ? m_leaf.decor : m_dottedPrefix.front().decor;
    }

    const Decor& lineLeadDecor() const noexcept
    {
        return m_dottedPrefix.empty() ? m_leaf.decor : m_dottedPrefix.front().decor;
    }

    TOMLKIT11_INLINE std::vector<std::string> leadingComments() const;
    TOMLKIT11_INLINE void addLeadingComment(StringView text, StringView newline = "\n");
    TOMLKIT11_INLINE void clearLeadingComments();

private:
    std::vector<KeySegment> m_dottedPrefix;
    KeySegment m_leaf;
};

TOMLKIT11_INLINE std::string formatKeySegment(
    const KeySegment& seg,
    SpecVersion spec = SpecVersion::v1_1
);
TOMLKIT11_INLINE std::string escapeBasicString(
    StringView value,
    bool multiline = false,
    SpecVersion spec = SpecVersion::v1_1
);

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_DECOR_HPP
