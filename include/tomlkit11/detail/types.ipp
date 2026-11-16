#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#include <ctime>
#include <string>
#include <system_error>
#include <time.h>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

TOMLKIT11_NAMESPACE_BEGIN

namespace
{
    struct CivilYmd
    {
        int64_t year;
        uint32_t month;
        uint32_t day;
    };

    inline bool isLeapYear(uint16_t year) noexcept
    {
        return (year % 4u == 0u && year % 100u != 0u) || (year % 400u == 0u);
    }

    inline uint8_t daysInMonth(uint16_t year, uint8_t month) noexcept
    {
        switch (month)
        {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return isLeapYear(year) ? 29 : 28;
        default:
            return 0;
        }
    }

    // Howard Hinnant civil-to-days and days-to-civil conversion relative to 1970-01-01.
    inline int64_t daysFromCivil(int64_t y, uint32_t m, uint32_t d) noexcept
    {
        y -= (m <= 2) ? 1 : 0;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const uint32_t yoe = (uint32_t)(y - era * 400);
        const uint32_t doy = (153u * (m + (m > 2 ? (uint32_t)-3 : 9u)) + 2u) / 5u + d - 1u;
        const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097 + (int64_t)doe - 719468;
    }

    inline CivilYmd civilFromDays(int64_t z) noexcept
    {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const uint32_t doe = (uint32_t)(z - era * 146097);
        const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t y = (int64_t)yoe + era * 400;
        const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const uint32_t mp = (5 * doy + 2) / 153;
        const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
        const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
        CivilYmd res;
        res.year = y + (m <= 2 ? 1 : 0);
        res.month = m;
        res.day = d;
        return res;
    }

    inline std::string wideToUtf8Impl(WideStringView wide, std::true_type /* wchar_t is 16-bit */)
    {
        std::string out;
        out.reserve(wide.size() * 3);
        size_t i = 0;
        while (i < wide.size()) {
            const uint32_t u0 = (uint16_t)wide[i++];
            if (u0 >= 0xD800u && u0 <= 0xDBFFu) {
                if (i < wide.size()) {
                    const uint32_t u1 = (uint16_t)wide[i];
                    if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
                        ++i;
                        const char32_t cp = (char32_t)(0x10000u + ((u0 - 0xD800u) << 10) + (u1 - 0xDC00u));
                        appendUtf8Codepoint(out, cp);
                        continue;
                    }
                }
                appendUtf8Codepoint(out, 0xFFFDu);
            }
            else if (u0 >= 0xDC00u && u0 <= 0xDFFFu) {
                appendUtf8Codepoint(out, 0xFFFDu);
            }
            else {
                appendUtf8Codepoint(out, (char32_t)u0);
            }
        }
        return out;
    }

    inline std::string wideToUtf8Impl(WideStringView wide, std::false_type /* wchar_t is 32-bit */)
    {
        std::string out;
        out.reserve(wide.size() * 3);
        for (size_t i = 0; i < wide.size(); ++i) {
            const uint32_t cp = (uint32_t)wide[i];
            if (cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                appendUtf8Codepoint(out, (char32_t)cp);
            }
            else {
                appendUtf8Codepoint(out, 0xFFFDu);
            }
        }
        return out;
    }

    inline void appendCodepointToWide(std::wstring& out, char32_t cp, std::true_type /* wchar_t is 16-bit */)
    {
        if (cp <= 0xFFFFu) {
            out.push_back((wchar_t)cp);
        }
        else {
            const uint32_t v = (uint32_t)cp - 0x10000u;
            out.push_back((wchar_t)(0xD800u + (v >> 10)));
            out.push_back((wchar_t)(0xDC00u + (v & 0x3FFu)));
        }
    }

    inline void appendCodepointToWide(std::wstring& out, char32_t cp, std::false_type /* wchar_t is 32-bit */)
    {
        out.push_back((wchar_t)cp);
    }
} // namespace

TOMLKIT11_INLINE StringView itemKindName(ItemKind kind) noexcept
{
    switch (kind)
    {
    case ItemKind::Empty:          return "empty";
    case ItemKind::Boolean:        return "boolean";
    case ItemKind::Integer:        return "integer";
    case ItemKind::Float:          return "float";
    case ItemKind::String:         return "string";
    case ItemKind::LocalDate:      return "local_date";
    case ItemKind::LocalTime:      return "local_time";
    case ItemKind::LocalDateTime:  return "local_date_time";
    case ItemKind::OffsetDateTime: return "offset_date_time";
    case ItemKind::Array:          return "array";
    case ItemKind::Table:          return "table";
    default:                       return "unknown";
    }
}

TOMLKIT11_INLINE bool LocalDate::isValid() const noexcept
{
    if (year > 9999 || month < 1 || month > 12 || day < 1) {
        return false;
    }
    return day <= daysInMonth(year, month);
}

TOMLKIT11_INLINE std::string LocalDate::toString() const
{
    std::string out;
    out.reserve(10);
    detail::appendZeroPaddedUint(out, (uint64_t)year, 4);
    out.push_back('-');
    detail::appendZeroPaddedUint(out, (uint64_t)month, 2);
    out.push_back('-');
    detail::appendZeroPaddedUint(out, (uint64_t)day, 2);
    return out;
}

TOMLKIT11_INLINE bool LocalTime::isValid() const noexcept
{
    if (hour > 23 || minute > 59 || second > 60 || nanosecond >= 1000000000u) {
        return false;
    }
    if (!hasSeconds && (second != 0 || nanosecond != 0)) {
        return false;
    }
    return true;
}

TOMLKIT11_INLINE std::string LocalTime::toString(bool forceSeconds) const
{
    if (!hasSeconds && !forceSeconds && second == 0 && nanosecond == 0) {
        std::string out;
        out.reserve(5);
        detail::appendZeroPaddedUint(out, (uint64_t)hour, 2);
        out.push_back(':');
        detail::appendZeroPaddedUint(out, (uint64_t)minute, 2);
        return out;
    }

    std::string res;
    res.reserve(18);
    detail::appendZeroPaddedUint(res, (uint64_t)hour, 2);
    res.push_back(':');
    detail::appendZeroPaddedUint(res, (uint64_t)minute, 2);
    res.push_back(':');
    detail::appendZeroPaddedUint(res, (uint64_t)second, 2);

    if (nanosecond != 0 || subsecondDigits > 0) {
        std::string frac;
        frac.reserve(9);
        detail::appendZeroPaddedUint(frac, (uint64_t)nanosecond, 9);
        if (subsecondDigits > 0 && subsecondDigits <= 9) {
            // Preserve all non-zero digits if precision exceeds original subsecondDigits.
            const size_t minKeep = (size_t)subsecondDigits;
            while (frac.size() > minKeep && frac.back() == '0') {
                frac.pop_back();
            }
            if (frac.size() > minKeep) {
                while (frac.size() > 1 && frac.back() == '0') {
                    frac.pop_back();
                }
            }
        }
        else {
            while (frac.size() > 1 && frac.back() == '0') {
                frac.pop_back();
            }
        }
        res.push_back('.');
        res.append(frac);
    }
    return res;
}

TOMLKIT11_INLINE bool LocalDateTime::isValid() const noexcept
{
    const bool validDelim = (delimiter == 'T' || delimiter == 't' || delimiter == ' ');
    return validDelim && date.isValid() && time.isValid();
}

TOMLKIT11_INLINE std::string LocalDateTime::toString(bool forceSeconds) const
{
    const char outDelim = forceSeconds ? 'T' : delimiter;
    std::string res = date.toString();
    res.push_back(outDelim);
    res.append(time.toString(forceSeconds));
    return res;
}

TOMLKIT11_INLINE bool OffsetDateTime::isValid() const noexcept
{
    const bool validDelim = (delimiter == 'T' || delimiter == 't' || delimiter == ' ');
    const bool validOffset = (offsetMinutes >= -1439 && offsetMinutes <= 1439);
    return validDelim && validOffset && date.isValid() && time.isValid();
}

TOMLKIT11_INLINE std::string OffsetDateTime::toString(bool forceSeconds) const
{
    const char outDelim = forceSeconds ? 'T' : delimiter;
    std::string res = date.toString();
    res.push_back(outDelim);
    res.append(time.toString(forceSeconds));
    if (isUtcZ) {
        res.push_back(forceSeconds ? 'Z' : zChar);
    }
    else {
        const bool isNeg = (offsetMinutes < 0) || (offsetMinutes == 0 && negativeZeroOffset);
        const int absMin = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
        res.push_back(isNeg ? '-' : '+');
        detail::appendZeroPaddedUint(res, (uint64_t)(absMin / 60), 2);
        res.push_back(':');
        detail::appendZeroPaddedUint(res, (uint64_t)(absMin % 60), 2);
    }
    return res;
}

TOMLKIT11_INLINE std::pair<int64_t, uint32_t> OffsetDateTime::toUtcEpoch() const noexcept
{
    const int64_t days = daysFromCivil((int64_t)date.year, (uint32_t)date.month, (uint32_t)date.day);
    const int64_t localSecs = days * 86400
        + (int64_t)time.hour * 3600
        + (int64_t)time.minute * 60
        + (int64_t)time.second;
    const int64_t utcSecs = localSecs - (int64_t)(isUtcZ ? 0 : offsetMinutes) * 60;
    return std::pair<int64_t, uint32_t>(utcSecs, time.nanosecond);
}

TOMLKIT11_INLINE int64_t OffsetDateTime::toEpochMillis() const noexcept
{
    const std::pair<int64_t, uint32_t> epoch = toUtcEpoch();
    return epoch.first * 1000 + (int64_t)(epoch.second / 1000000u);
}

TOMLKIT11_INLINE OffsetDateTime OffsetDateTime::fromEpochMillisLocal(int64_t epochMs) noexcept
{
    int64_t utcSec = epochMs / 1000;
    int64_t remMs = epochMs % 1000;
    if (remMs < 0) {
        remMs += 1000;
        --utcSec;
    }

    const std::time_t rawTime = (std::time_t)utcSec;
    std::tm localTm{};
    bool ok = false;
    if ((int64_t)rawTime == utcSec) {
#ifdef _WIN32
        ok = (::localtime_s(&localTm, &rawTime) == 0);
#else
        ok = (::localtime_r(&rawTime, &localTm) != nullptr);
#endif
    }

    OffsetDateTime out{};
    out.delimiter = 'T';
    out.time.nanosecond = (uint32_t)remMs * 1000000u;
    out.time.subsecondDigits = 3;
    out.time.hasSeconds = true;

    if (ok && localTm.tm_year + 1900 >= 0 && localTm.tm_year + 1900 <= 9999) {
        const uint16_t year = (uint16_t)(localTm.tm_year + 1900);
        const uint8_t month = (uint8_t)(localTm.tm_mon + 1);
        const uint8_t day = (uint8_t)localTm.tm_mday;
        const uint8_t hour = (uint8_t)localTm.tm_hour;
        const uint8_t minute = (uint8_t)localTm.tm_min;
        const uint8_t second = (uint8_t)localTm.tm_sec;

        const int64_t localEpochSec = daysFromCivil((int64_t)year, (uint32_t)month, (uint32_t)day) * 86400
            + (int64_t)hour * 3600
            + (int64_t)minute * 60
            + (int64_t)second;

        out.date = LocalDate(year, month, day);
        out.time.hour = hour;
        out.time.minute = minute;
        out.time.second = second;
        out.isUtcZ = false;
        out.offsetMinutes = (int16_t)((localEpochSec - utcSec) / 60);
        return out;
    }

    int64_t days = utcSec / 86400;
    int64_t secOfDay = utcSec % 86400;
    if (secOfDay < 0) {
        secOfDay += 86400;
        --days;
    }
    const CivilYmd civil = civilFromDays(days);
    const int64_t clampedYear = (civil.year < 0) ? 0 : ((civil.year > 9999) ? 9999 : civil.year);
    out.date = LocalDate((uint16_t)clampedYear, (uint8_t)civil.month, (uint8_t)civil.day);
    out.time.hour = (uint8_t)(secOfDay / 3600);
    out.time.minute = (uint8_t)((secOfDay % 3600) / 60);
    out.time.second = (uint8_t)(secOfDay % 60);
    out.isUtcZ = false;
    out.offsetMinutes = 0;
    return out;
}

TOMLKIT11_INLINE bool operator==(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
{
    return lhs.toUtcEpoch() == rhs.toUtcEpoch();
}

TOMLKIT11_INLINE bool operator!=(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
{
    return !(lhs == rhs);
}

TOMLKIT11_INLINE bool operator<(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
{
    return lhs.toUtcEpoch() < rhs.toUtcEpoch();
}

TOMLKIT11_INLINE bool operator<=(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
{
    return !(rhs < lhs);
}

TOMLKIT11_INLINE bool operator>(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
{
    return rhs < lhs;
}

TOMLKIT11_INLINE bool operator>=(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
{
    return !(lhs < rhs);
}

TOMLKIT11_INLINE bool decodeUtf8Codepoint(StringView text, size_t& pos, char32_t& outCp) noexcept
{
    if (pos >= text.size()) {
        return false;
    }

    const uint8_t b0 = (uint8_t)text[pos];
    if (b0 < 0x80u) {
        outCp = (char32_t)b0;
        ++pos;
        return true;
    }

    // 2-byte sequence: C2..DF 80..BF (rejects C0/C1 overlong encodings).
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (pos + 1 >= text.size()) {
            return false;
        }
        const uint8_t b1 = (uint8_t)text[pos + 1];
        if ((b1 & 0xC0u) != 0x80u) {
            return false;
        }
        outCp = ((char32_t)(b0 & 0x1Fu) << 6) | (char32_t)(b1 & 0x3Fu);
        pos += 2;
        return true;
    }

    // 3-byte sequence: E0..EF, rejecting overlong encodings and UTF-16 surrogates U+D800..U+DFFF.
    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        if (pos + 2 >= text.size()) {
            return false;
        }
        const uint8_t b1 = (uint8_t)text[pos + 1];
        const uint8_t b2 = (uint8_t)text[pos + 2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return false;
        }
        if (b0 == 0xE0u && b1 < 0xA0u) {
            return false;
        }
        if (b0 == 0xEDu && b1 >= 0xA0u) {
            return false;
        }
        outCp = ((char32_t)(b0 & 0x0Fu) << 12)
            | ((char32_t)(b1 & 0x3Fu) << 6)
            | (char32_t)(b2 & 0x3Fu);
        pos += 3;
        return true;
    }

    // 4-byte sequence: F0..F4, bounded to <= U+10FFFF.
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (pos + 3 >= text.size()) {
            return false;
        }
        const uint8_t b1 = (uint8_t)text[pos + 1];
        const uint8_t b2 = (uint8_t)text[pos + 2];
        const uint8_t b3 = (uint8_t)text[pos + 3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return false;
        }
        if (b0 == 0xF0u && b1 < 0x90u) {
            return false;
        }
        if (b0 == 0xF4u && b1 > 0x8Fu) {
            return false;
        }
        outCp = ((char32_t)(b0 & 0x07u) << 18)
            | ((char32_t)(b1 & 0x3Fu) << 12)
            | ((char32_t)(b2 & 0x3Fu) << 6)
            | (char32_t)(b3 & 0x3Fu);
        pos += 4;
        return true;
    }

    return false;
}

TOMLKIT11_INLINE bool isValidUtf8(StringView text, size_t* pErrorOffset) noexcept
{
    size_t pos = 0;
    char32_t cp = 0;
    while (pos < text.size()) {
        const size_t startPos = pos;
        if (!decodeUtf8Codepoint(text, pos, cp)) {
            if (pErrorOffset != nullptr) {
                *pErrorOffset = startPos;
            }
            return false;
        }
    }
    return true;
}

TOMLKIT11_INLINE void appendUtf8Codepoint(std::string& out, char32_t cp)
{
    if (cp <= 0x7Fu) {
        out.push_back((char)cp);
    }
    else if (cp <= 0x7FFu) {
        out.push_back((char)(0xC0u | (cp >> 6)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else if (cp <= 0xFFFFu) {
        out.push_back((char)(0xE0u | (cp >> 12)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else if (cp <= 0x10FFFFu) {
        out.push_back((char)(0xF0u | (cp >> 18)));
        out.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
}

TOMLKIT11_INLINE std::string wideToUtf8(WideStringView wide)
{
    return wideToUtf8Impl(wide, std::integral_constant<bool, (sizeof(wchar_t) == 2)>());
}

TOMLKIT11_INLINE std::wstring utf8ToWide(StringView utf8)
{
    std::wstring out;
    out.reserve(utf8.size());

    size_t pos = 0;
    char32_t cp = 0;
    while (pos < utf8.size()) {
        if (!decodeUtf8Codepoint(utf8, pos, cp)) {
            ++pos;
            cp = 0xFFFDu;
        }
        appendCodepointToWide(out, cp, std::integral_constant<bool, (sizeof(wchar_t) == 2)>());
    }

    return out;
}

#if TOMLKIT11_HAS_FILESYSTEM

TOMLKIT11_INLINE std::string pathToUtf8(const std::filesystem::path& path)
{
#ifdef _WIN32
    return wideToUtf8(path.native());
#else
    return path.native();
#endif
}

TOMLKIT11_INLINE std::filesystem::path utf8ToPath(StringView utf8)
{
#ifdef _WIN32
    return std::filesystem::path(utf8ToWide(utf8));
#else
    return std::filesystem::path(detail::toString(utf8));
#endif
}

#endif // TOMLKIT11_HAS_FILESYSTEM

TOMLKIT11_INLINE std::string formatErrorCodeUtf8(const std::error_code& ec)
{
#ifdef _WIN32
    if (ec.category() == std::system_category()) {
        LPWSTR pBuffer = nullptr;
        const DWORD length = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            (DWORD)ec.value(),
            0,
            (LPWSTR)&pBuffer,
            0,
            nullptr
        );
        if (length != 0 && pBuffer != nullptr) {
            WideStringView wideMsg(pBuffer, (size_t)length);
            while (!wideMsg.empty() && (wideMsg.back() == L'\r' || wideMsg.back() == L'\n' || wideMsg.back() == L' ')) {
                wideMsg.remove_suffix(1);
            }
            std::string utf8Msg = wideToUtf8(wideMsg);
            ::LocalFree(pBuffer);
            return utf8Msg;
        }
        if (pBuffer != nullptr) {
            ::LocalFree(pBuffer);
        }
    }
#endif

    const std::string fallback = ec.message();
    if (isValidUtf8(fallback)) {
        return fallback;
    }
    return "system error " + detail::intToString((int64_t)ec.value());
}

TOMLKIT11_INLINE bool isBareKeyChar(char32_t cp, SpecVersion spec) noexcept
{
    (void)spec;
    return (cp >= U'A' && cp <= U'Z')
        || (cp >= U'a' && cp <= U'z')
        || (cp >= U'0' && cp <= U'9')
        || cp == U'-'
        || cp == U'_';
}

TOMLKIT11_INLINE bool isValidBareKey(StringView key, SpecVersion spec) noexcept
{
    if (key.empty()) {
        return false;
    }
    for (size_t i = 0; i < key.size(); ++i) {
        if (!isBareKeyChar((char32_t)(uint8_t)key[i], spec)) {
            return false;
        }
    }
    return true;
}

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
