#pragma once

#ifndef TOMLKIT11_TYPES_HPP
#define TOMLKIT11_TYPES_HPP

#include "spec.hpp"

#include <string>
#include <system_error>
#include <utility>

TOMLKIT11_NAMESPACE_BEGIN

enum class ItemKind : uint8_t
{
    Empty,
    Boolean,
    Integer,
    Float,
    String,
    LocalDate,
    LocalTime,
    LocalDateTime,
    OffsetDateTime,
    Array,
    Table
};

TOMLKIT11_INLINE StringView itemKindName(ItemKind kind) noexcept;

enum class StringStyle : uint8_t
{
    Basic,
    Literal,
    MultilineBasic,
    MultilineLiteral
};

enum class IntegerRadix : uint8_t
{
    Decimal,
    Hexadecimal,
    Octal,
    Binary
};

enum class TableStyle : uint8_t
{
    DocumentRoot,
    Standard,
    ImplicitSuper,
    Dotted,
    Inline,
    ArrayOfTablesElement
};

enum class ArrayStyle : uint8_t
{
    Standard,
    ArrayOfTables
};

enum class KeyStyle : uint8_t
{
    Bare,
    BasicQuoted,
    LiteralQuoted
};

struct LocalDate
{
    uint16_t year = 1970;
    uint8_t month = 1;
    uint8_t day = 1;

    constexpr LocalDate(uint16_t y = 1970, uint8_t m = 1, uint8_t d = 1) noexcept
        : year(y)
        , month(m)
        , day(d)
    { }

    TOMLKIT11_INLINE bool isValid() const noexcept;
    TOMLKIT11_INLINE std::string toString() const;
};

inline bool operator==(const LocalDate& lhs, const LocalDate& rhs) noexcept
{
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day;
}

inline bool operator!=(const LocalDate& lhs, const LocalDate& rhs) noexcept
{
    return !(lhs == rhs);
}

inline bool operator<(const LocalDate& lhs, const LocalDate& rhs) noexcept
{
    if (lhs.year != rhs.year) {
        return lhs.year < rhs.year;
    }
    if (lhs.month != rhs.month) {
        return lhs.month < rhs.month;
    }
    return lhs.day < rhs.day;
}

inline bool operator<=(const LocalDate& lhs, const LocalDate& rhs) noexcept
{
    return !(rhs < lhs);
}

inline bool operator>(const LocalDate& lhs, const LocalDate& rhs) noexcept
{
    return rhs < lhs;
}

inline bool operator>=(const LocalDate& lhs, const LocalDate& rhs) noexcept
{
    return !(lhs < rhs);
}

struct LocalTime
{
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t nanosecond = 0;
    uint8_t subsecondDigits = 0;
    bool hasSeconds = true;

    constexpr LocalTime(
        uint8_t h = 0,
        uint8_t m = 0,
        uint8_t s = 0,
        uint32_t ns = 0,
        uint8_t subDigits = 0,
        bool hasSec = true
    ) noexcept
        : hour(h)
        , minute(m)
        , second(s)
        , nanosecond(ns)
        , subsecondDigits(subDigits)
        , hasSeconds(hasSec)
    { }

    TOMLKIT11_INLINE bool isValid() const noexcept;
    TOMLKIT11_INLINE std::string toString(bool forceSeconds = false) const;
};

inline bool operator==(const LocalTime& lhs, const LocalTime& rhs) noexcept
{
    return lhs.hour == rhs.hour
        && lhs.minute == rhs.minute
        && lhs.second == rhs.second
        && lhs.nanosecond == rhs.nanosecond;
}

inline bool operator!=(const LocalTime& lhs, const LocalTime& rhs) noexcept
{
    return !(lhs == rhs);
}

inline bool operator<(const LocalTime& lhs, const LocalTime& rhs) noexcept
{
    if (lhs.hour != rhs.hour) {
        return lhs.hour < rhs.hour;
    }
    if (lhs.minute != rhs.minute) {
        return lhs.minute < rhs.minute;
    }
    if (lhs.second != rhs.second) {
        return lhs.second < rhs.second;
    }
    return lhs.nanosecond < rhs.nanosecond;
}

inline bool operator<=(const LocalTime& lhs, const LocalTime& rhs) noexcept
{
    return !(rhs < lhs);
}

inline bool operator>(const LocalTime& lhs, const LocalTime& rhs) noexcept
{
    return rhs < lhs;
}

inline bool operator>=(const LocalTime& lhs, const LocalTime& rhs) noexcept
{
    return !(lhs < rhs);
}

struct LocalDateTime
{
    LocalDate date{};
    LocalTime time{};
    char delimiter = 'T';

    constexpr LocalDateTime(
        LocalDate d = LocalDate(),
        LocalTime t = LocalTime(),
        char delim = 'T'
    ) noexcept
        : date(d)
        , time(t)
        , delimiter(delim)
    { }

    TOMLKIT11_INLINE bool isValid() const noexcept;
    TOMLKIT11_INLINE std::string toString(bool forceSeconds = false) const;
};

inline bool operator==(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
{
    return lhs.date == rhs.date && lhs.time == rhs.time;
}

inline bool operator!=(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
{
    return !(lhs == rhs);
}

inline bool operator<(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
{
    if (lhs.date != rhs.date) {
        return lhs.date < rhs.date;
    }
    return lhs.time < rhs.time;
}

inline bool operator<=(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
{
    return !(rhs < lhs);
}

inline bool operator>(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
{
    return rhs < lhs;
}

inline bool operator>=(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
{
    return !(lhs < rhs);
}

struct OffsetDateTime
{
    LocalDate date{};
    LocalTime time{};
    int16_t offsetMinutes = 0;
    bool isUtcZ = true;
    bool negativeZeroOffset = false;
    char delimiter = 'T';
    char zChar = 'Z';

    constexpr OffsetDateTime(
        LocalDate d = LocalDate(),
        LocalTime t = LocalTime(),
        int16_t offMin = 0,
        bool utcZ = true,
        bool negZeroOff = false,
        char delim = 'T',
        char z = 'Z'
    ) noexcept
        : date(d)
        , time(t)
        , offsetMinutes(offMin)
        , isUtcZ(utcZ)
        , negativeZeroOffset(negZeroOff)
        , delimiter(delim)
        , zChar(z)
    { }

    TOMLKIT11_INLINE bool isValid() const noexcept;
    TOMLKIT11_INLINE std::string toString(bool forceSeconds = false) const;

    // Converts to UTC (seconds, nanoseconds) relative to 1970-01-01T00:00:00Z for exact comparison.
    TOMLKIT11_INLINE std::pair<int64_t, uint32_t> toUtcEpoch() const noexcept;
    TOMLKIT11_INLINE int64_t toEpochMillis() const noexcept;

    // Converts Unix epoch milliseconds into an OffsetDateTime using the current local system timezone offset.
    TOMLKIT11_INLINE static OffsetDateTime fromEpochMillisLocal(int64_t epochMs) noexcept;
};

TOMLKIT11_INLINE bool operator==(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept;
TOMLKIT11_INLINE bool operator!=(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept;
TOMLKIT11_INLINE bool operator<(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept;
TOMLKIT11_INLINE bool operator<=(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept;
TOMLKIT11_INLINE bool operator>(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept;
TOMLKIT11_INLINE bool operator>=(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept;

// UTF-8, wide string, std::filesystem::path, and bare-key validation utilities.
TOMLKIT11_INLINE bool decodeUtf8Codepoint(StringView text, size_t& pos, char32_t& outCp) noexcept;
TOMLKIT11_INLINE bool isValidUtf8(StringView text, size_t* pErrorOffset = nullptr) noexcept;
TOMLKIT11_INLINE void appendUtf8Codepoint(std::string& out, char32_t cp);

TOMLKIT11_INLINE std::string wideToUtf8(WideStringView wide);
TOMLKIT11_INLINE std::wstring utf8ToWide(StringView utf8);

#if TOMLKIT11_HAS_FILESYSTEM
TOMLKIT11_INLINE std::string pathToUtf8(const std::filesystem::path& path);
TOMLKIT11_INLINE std::filesystem::path utf8ToPath(StringView utf8);
#endif

TOMLKIT11_INLINE std::string formatErrorCodeUtf8(const std::error_code& ec);

TOMLKIT11_INLINE bool isBareKeyChar(char32_t cp, SpecVersion spec = SpecVersion::v1_1) noexcept;
TOMLKIT11_INLINE bool isValidBareKey(StringView key, SpecVersion spec = SpecVersion::v1_1) noexcept;

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_TYPES_HPP
