#pragma once

#ifndef TOMLKIT11_COMPAT_HPP
#define TOMLKIT11_COMPAT_HPP

#include "config.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#if TOMLKIT11_HAS_STRING_VIEW
#    include <string_view>
#endif

#if TOMLKIT11_HAS_OPTIONAL
#    include <optional>
#endif

#if TOMLKIT11_HAS_FILESYSTEM
#    include <filesystem>
#endif

#if TOMLKIT11_HAS_EXCEPTIONS
#    include <stdexcept>
#endif

#if defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
#    include <expected>
#endif

TOMLKIT11_NAMESPACE_BEGIN

using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::size_t;
using std::ptrdiff_t;

struct ParseError;

namespace detail
{
    template<bool B, typename T = void>
    using enable_if_t = typename std::enable_if<B, T>::type;

    template<bool B, typename T, typename F>
    using conditional_t = typename std::conditional<B, T, F>::type;

    template<typename T>
    using remove_cv_t = typename std::remove_cv<T>::type;

    template<typename T>
    using remove_reference_t = typename std::remove_reference<T>::type;

    template<typename T>
    using decay_t = typename std::decay<T>::type;

    template<typename T>
    struct RemoveCvRef
    {
        using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    };

    template<typename T>
    using remove_cvref_t = typename RemoveCvRef<T>::type;

    template<typename...>
    struct MakeVoid
    {
        using type = void;
    };

    template<typename... Ts>
    using void_t = typename MakeVoid<Ts...>::type;

    template<typename T>
    struct AlwaysFalse : std::false_type
    { };

    template<typename T, typename... Args>
    enable_if_t<!std::is_array<T>::value, std::unique_ptr<T>>
    makeUnique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
} // namespace detail

#if TOMLKIT11_HAS_STRING_VIEW

using StringView = std::string_view;
using WideStringView = std::wstring_view;

#else

template<typename CharT>
class BasicStringViewBase
{
public:
    using traits_type = std::char_traits<CharT>;
    using value_type = CharT;
    using pointer = CharT*;
    using const_pointer = const CharT*;
    using reference = CharT&;
    using const_reference = const CharT&;
    using const_iterator = const CharT*;
    using iterator = const_iterator;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using reverse_iterator = const_reverse_iterator;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    static constexpr size_type npos = (size_type)-1;

    constexpr BasicStringViewBase() noexcept
        : m_data(nullptr)
        , m_size(0)
    { }

    constexpr BasicStringViewBase(const BasicStringViewBase&) noexcept = default;
    BasicStringViewBase& operator=(const BasicStringViewBase&) noexcept = default;

    BasicStringViewBase(const CharT* pStr) noexcept
        : m_data(pStr)
        , m_size(pStr != nullptr ? traits_type::length(pStr) : 0)
    { }

    constexpr BasicStringViewBase(const CharT* pStr, size_type count) noexcept
        : m_data(pStr)
        , m_size(count)
    { }

    BasicStringViewBase(const std::basic_string<CharT>& str) noexcept
        : m_data(str.data())
        , m_size(str.size())
    { }

    constexpr const_iterator begin() const noexcept { return m_data; }
    constexpr const_iterator end() const noexcept { return m_data + m_size; }
    constexpr const_iterator cbegin() const noexcept { return m_data; }
    constexpr const_iterator cend() const noexcept { return m_data + m_size; }

    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

    constexpr const_reference operator[](size_type pos) const noexcept { return m_data[pos]; }

    const_reference at(size_type pos) const
    {
        if (pos >= m_size) {
#if TOMLKIT11_HAS_EXCEPTIONS
            throw std::out_of_range("tomlkit::BasicStringView::at: index out of range");
#else
            TOMLKIT11_ABORT();
#endif
        }
        return m_data[pos];
    }

    constexpr const_reference front() const noexcept { return m_data[0]; }
    constexpr const_reference back() const noexcept { return m_data[m_size - 1]; }
    constexpr const_pointer data() const noexcept { return m_data; }

    constexpr size_type size() const noexcept { return m_size; }
    constexpr size_type length() const noexcept { return m_size; }
    constexpr size_type max_size() const noexcept { return (std::numeric_limits<size_type>::max)(); }
    constexpr bool empty() const noexcept { return m_size == 0; }

    void remove_prefix(size_type n) noexcept
    {
        m_data += n;
        m_size -= n;
    }

    void remove_suffix(size_type n) noexcept
    {
        m_size -= n;
    }

    void swap(BasicStringViewBase& other) noexcept
    {
        const CharT* pTmpData = m_data;
        const size_type tmpSize = m_size;
        m_data = other.m_data;
        m_size = other.m_size;
        other.m_data = pTmpData;
        other.m_size = tmpSize;
    }

    size_type copy(CharT* pDest, size_type count, size_type pos = 0) const
    {
        if (pos > m_size) {
#if TOMLKIT11_HAS_EXCEPTIONS
            throw std::out_of_range("tomlkit::BasicStringView::copy: pos out of range");
#else
            TOMLKIT11_ABORT();
#endif
        }
        const size_type avail = m_size - pos;
        const size_type rcount = (count < avail) ? count : avail;
        if (rcount > 0) {
            traits_type::copy(pDest, m_data + pos, rcount);
        }
        return rcount;
    }

    BasicStringViewBase substr(size_type pos = 0, size_type count = npos) const
    {
        if (pos > m_size) {
#if TOMLKIT11_HAS_EXCEPTIONS
            throw std::out_of_range("tomlkit::BasicStringView::substr: pos out of range");
#else
            TOMLKIT11_ABORT();
#endif
        }
        const size_type rem = m_size - pos;
        const size_type len = (count < rem) ? count : rem;
        return BasicStringViewBase(m_data + pos, len);
    }

    int compare(BasicStringViewBase other) const noexcept
    {
        const size_type minLen = (m_size < other.m_size) ? m_size : other.m_size;
        if (minLen > 0) {
            const int cmp = traits_type::compare(m_data, other.m_data, minLen);
            if (cmp != 0) {
                return cmp;
            }
        }
        if (m_size < other.m_size) {
            return -1;
        }
        if (m_size > other.m_size) {
            return 1;
        }
        return 0;
    }

    int compare(size_type pos1, size_type count1, BasicStringViewBase other) const
    {
        return substr(pos1, count1).compare(other);
    }

    int compare(
        size_type pos1,
        size_type count1,
        BasicStringViewBase other,
        size_type pos2,
        size_type count2
    ) const
    {
        return substr(pos1, count1).compare(other.substr(pos2, count2));
    }

    int compare(const CharT* pStr) const noexcept
    {
        return compare(BasicStringViewBase(pStr));
    }

    int compare(size_type pos1, size_type count1, const CharT* pStr) const
    {
        return substr(pos1, count1).compare(BasicStringViewBase(pStr));
    }

    int compare(size_type pos1, size_type count1, const CharT* pStr, size_type count2) const
    {
        return substr(pos1, count1).compare(BasicStringViewBase(pStr, count2));
    }

    size_type find(BasicStringViewBase sv, size_type pos = 0) const noexcept
    {
        if (pos > m_size) {
            return npos;
        }
        if (sv.m_size == 0) {
            return pos;
        }
        if (sv.m_size > m_size - pos) {
            return npos;
        }
        const size_type last = m_size - sv.m_size;
        for (size_type i = pos; i <= last; ++i) {
            if (traits_type::compare(m_data + i, sv.m_data, sv.m_size) == 0) {
                return i;
            }
        }
        return npos;
    }

    size_type find(CharT ch, size_type pos = 0) const noexcept
    {
        if (pos >= m_size) {
            return npos;
        }
        const CharT* pFound = traits_type::find(m_data + pos, m_size - pos, ch);
        return pFound != nullptr ? (size_type)(pFound - m_data) : npos;
    }

    size_type find(const CharT* pStr, size_type pos, size_type count) const noexcept
    {
        return find(BasicStringViewBase(pStr, count), pos);
    }

    size_type find(const CharT* pStr, size_type pos = 0) const noexcept
    {
        return find(BasicStringViewBase(pStr), pos);
    }

    size_type rfind(BasicStringViewBase sv, size_type pos = npos) const noexcept
    {
        if (sv.m_size > m_size) {
            return npos;
        }
        if (sv.m_size == 0) {
            return (pos < m_size) ? pos : m_size;
        }
        const size_type maxStart = m_size - sv.m_size;
        const size_type start = (pos < maxStart) ? pos : maxStart;
        for (size_type step = start + 1; step > 0; --step) {
            const size_type idx = step - 1;
            if (traits_type::compare(m_data + idx, sv.m_data, sv.m_size) == 0) {
                return idx;
            }
        }
        return npos;
    }

    size_type rfind(CharT ch, size_type pos = npos) const noexcept
    {
        if (m_size == 0) {
            return npos;
        }
        const size_type start = (pos < m_size) ? pos : (m_size - 1);
        for (size_type step = start + 1; step > 0; --step) {
            const size_type idx = step - 1;
            if (traits_type::eq(m_data[idx], ch)) {
                return idx;
            }
        }
        return npos;
    }

    size_type rfind(const CharT* pStr, size_type pos, size_type count) const noexcept
    {
        return rfind(BasicStringViewBase(pStr, count), pos);
    }

    size_type rfind(const CharT* pStr, size_type pos = npos) const noexcept
    {
        return rfind(BasicStringViewBase(pStr), pos);
    }

    size_type find_first_of(BasicStringViewBase sv, size_type pos = 0) const noexcept
    {
        if (sv.m_size == 0 || pos >= m_size) {
            return npos;
        }
        for (size_type i = pos; i < m_size; ++i) {
            if (traits_type::find(sv.m_data, sv.m_size, m_data[i]) != nullptr) {
                return i;
            }
        }
        return npos;
    }

    size_type find_first_of(CharT ch, size_type pos = 0) const noexcept
    {
        return find(ch, pos);
    }

    size_type find_last_of(BasicStringViewBase sv, size_type pos = npos) const noexcept
    {
        if (m_size == 0 || sv.m_size == 0) {
            return npos;
        }
        const size_type start = (pos < m_size) ? pos : (m_size - 1);
        for (size_type step = start + 1; step > 0; --step) {
            const size_type idx = step - 1;
            if (traits_type::find(sv.m_data, sv.m_size, m_data[idx]) != nullptr) {
                return idx;
            }
        }
        return npos;
    }

    size_type find_last_of(CharT ch, size_type pos = npos) const noexcept
    {
        return rfind(ch, pos);
    }

    size_type find_first_not_of(BasicStringViewBase sv, size_type pos = 0) const noexcept
    {
        if (pos >= m_size) {
            return npos;
        }
        for (size_type i = pos; i < m_size; ++i) {
            if (sv.m_size == 0 || traits_type::find(sv.m_data, sv.m_size, m_data[i]) == nullptr) {
                return i;
            }
        }
        return npos;
    }

    size_type find_first_not_of(CharT ch, size_type pos = 0) const noexcept
    {
        for (size_type i = pos; i < m_size; ++i) {
            if (!traits_type::eq(m_data[i], ch)) {
                return i;
            }
        }
        return npos;
    }

    size_type find_last_not_of(BasicStringViewBase sv, size_type pos = npos) const noexcept
    {
        if (m_size == 0) {
            return npos;
        }
        const size_type start = (pos < m_size) ? pos : (m_size - 1);
        for (size_type step = start + 1; step > 0; --step) {
            const size_type idx = step - 1;
            if (sv.m_size == 0 || traits_type::find(sv.m_data, sv.m_size, m_data[idx]) == nullptr) {
                return idx;
            }
        }
        return npos;
    }

    size_type find_last_not_of(CharT ch, size_type pos = npos) const noexcept
    {
        if (m_size == 0) {
            return npos;
        }
        const size_type start = (pos < m_size) ? pos : (m_size - 1);
        for (size_type step = start + 1; step > 0; --step) {
            const size_type idx = step - 1;
            if (!traits_type::eq(m_data[idx], ch)) {
                return idx;
            }
        }
        return npos;
    }

    friend bool operator==(BasicStringViewBase lhs, BasicStringViewBase rhs) noexcept
    {
        return lhs.size() == rhs.size() && lhs.compare(rhs) == 0;
    }

    friend bool operator!=(BasicStringViewBase lhs, BasicStringViewBase rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend bool operator<(BasicStringViewBase lhs, BasicStringViewBase rhs) noexcept
    {
        return lhs.compare(rhs) < 0;
    }

    friend bool operator<=(BasicStringViewBase lhs, BasicStringViewBase rhs) noexcept
    {
        return lhs.compare(rhs) <= 0;
    }

    friend bool operator>(BasicStringViewBase lhs, BasicStringViewBase rhs) noexcept
    {
        return lhs.compare(rhs) > 0;
    }

    friend bool operator>=(BasicStringViewBase lhs, BasicStringViewBase rhs) noexcept
    {
        return lhs.compare(rhs) >= 0;
    }

private:
    const CharT* m_data;
    size_type m_size;
};

template<typename CharT>
constexpr typename BasicStringViewBase<CharT>::size_type BasicStringViewBase<CharT>::npos;

template<typename CharT, typename Traits>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,
    BasicStringViewBase<CharT> sv
)
{
    if (sv.empty()) {
        return os << std::basic_string<CharT, Traits>();
    }
    return os << std::basic_string<CharT, Traits>(sv.data(), sv.size());
}

using BasicStringView = BasicStringViewBase<char>;
using BasicWideStringView = BasicStringViewBase<wchar_t>;
using StringView = BasicStringView;
using WideStringView = BasicWideStringView;

#endif // TOMLKIT11_HAS_STRING_VIEW

namespace detail
{
    inline std::string toString(StringView sv)
    {
        if (sv.empty()) {
            return std::string();
        }
        return std::string(sv.data(), sv.size());
    }

    inline void appendSv(std::string& out, StringView sv)
    {
        if (!sv.empty()) {
            out.append(sv.data(), sv.size());
        }
    }

    inline bool svContains(StringView sv, char ch) noexcept
    {
        return sv.find(ch) != StringView::npos;
    }

    inline bool svContains(StringView sv, StringView needle) noexcept
    {
        return sv.find(needle) != StringView::npos;
    }

    inline bool svStartsWith(StringView sv, char ch) noexcept
    {
        return !sv.empty() && sv.front() == ch;
    }

    inline bool svStartsWith(StringView sv, StringView prefix) noexcept
    {
        return prefix.size() <= sv.size() && sv.compare(0, prefix.size(), prefix) == 0;
    }

    inline bool svEndsWith(StringView sv, char ch) noexcept
    {
        return !sv.empty() && sv.back() == ch;
    }

    inline bool svEndsWith(StringView sv, StringView suffix) noexcept
    {
        return suffix.size() <= sv.size()
            && sv.compare(sv.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    inline void appendUint(std::string& out, uint64_t value)
    {
        char buf[32];
        size_t pos = sizeof(buf);
        do {
            buf[--pos] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (value != 0u);
        out.append(buf + pos, sizeof(buf) - pos);
    }

    inline void appendInt(std::string& out, int64_t value)
    {
        if (value < 0) {
            out.push_back('-');
            const uint64_t mag = (uint64_t)(-(value + 1)) + 1u;
            appendUint(out, mag);
            return;
        }
        appendUint(out, (uint64_t)value);
    }

    inline std::string uintToString(uint64_t value)
    {
        std::string out;
        appendUint(out, value);
        return out;
    }

    inline std::string intToString(int64_t value)
    {
        std::string out;
        appendInt(out, value);
        return out;
    }

    inline void appendZeroPaddedUint(std::string& out, uint64_t value, size_t width)
    {
        char buf[32];
        size_t pos = sizeof(buf);
        do {
            buf[--pos] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (value != 0u);
        const size_t digits = sizeof(buf) - pos;
        if (digits < width) {
            out.append(width - digits, '0');
        }
        out.append(buf + pos, digits);
    }

    inline void appendHexUint32Fixed(
        std::string& out,
        uint32_t value,
        size_t width,
        bool uppercase = true
    )
    {
        const char* const pDigits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        char buf[16];
        size_t pos = sizeof(buf);
        do {
            buf[--pos] = pDigits[value & 0x0Fu];
            value >>= 4;
        }
        while (value != 0u);
        const size_t written = sizeof(buf) - pos;
        if (written < width) {
            out.append(width - written, '0');
        }
        out.append(buf + pos, written);
    }
} // namespace detail

#if TOMLKIT11_HAS_OPTIONAL

template<typename T>
using Optional = std::optional<T>;

using NulloptT = std::nullopt_t;
static constexpr NulloptT kNullopt = std::nullopt;

#else

struct NulloptT
{
    struct Tag { };
    explicit constexpr NulloptT(Tag) noexcept { }
};

static constexpr NulloptT kNullopt(NulloptT::Tag{});

template<typename T>
class Optional
{
public:
    using value_type = T;

    constexpr Optional() noexcept
        : m_hasValue(false)
    { }

    constexpr Optional(NulloptT) noexcept
        : m_hasValue(false)
    { }

    Optional(const Optional& other)
        : m_hasValue(false)
    {
        if (other.m_hasValue) {
            new (ptr()) T(*other.ptr());
            m_hasValue = true;
        }
    }

    Optional(Optional&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
        : m_hasValue(false)
    {
        if (other.m_hasValue) {
            new (ptr()) T(std::move(*other.ptr()));
            m_hasValue = true;
        }
    }

    template<typename U = T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Optional<T>>::value &&
        !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value &&
        std::is_constructible<T, U&&>::value &&
        std::is_convertible<U&&, T>::value, int> = 0>
    Optional(U&& val)
        : m_hasValue(false)
    {
        new (ptr()) T(std::forward<U>(val));
        m_hasValue = true;
    }

    template<typename U = T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Optional<T>>::value &&
        !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value &&
        std::is_constructible<T, U&&>::value &&
        !std::is_convertible<U&&, T>::value, int> = 0>
    explicit Optional(U&& val)
        : m_hasValue(false)
    {
        new (ptr()) T(std::forward<U>(val));
        m_hasValue = true;
    }

    ~Optional()
    {
        reset();
    }

    Optional& operator=(NulloptT) noexcept
    {
        reset();
        return *this;
    }

    Optional& operator=(const Optional& other)
    {
        if (this == &other) {
            return *this;
        }
        if (!other.m_hasValue) {
            reset();
        }
        else if (m_hasValue) {
            *ptr() = *other.ptr();
        }
        else {
            new (ptr()) T(*other.ptr());
            m_hasValue = true;
        }
        return *this;
    }

    Optional& operator=(Optional&& other) noexcept(
        std::is_nothrow_move_assignable<T>::value &&
        std::is_nothrow_move_constructible<T>::value
    )
    {
        if (this == &other) {
            return *this;
        }
        if (!other.m_hasValue) {
            reset();
        }
        else if (m_hasValue) {
            *ptr() = std::move(*other.ptr());
        }
        else {
            new (ptr()) T(std::move(*other.ptr()));
            m_hasValue = true;
        }
        return *this;
    }

    template<typename U = T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Optional<T>>::value &&
        !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value &&
        std::is_constructible<T, U&&>::value &&
        std::is_assignable<T&, U&&>::value, int> = 0>
    Optional& operator=(U&& val)
    {
        if (m_hasValue) {
            *ptr() = std::forward<U>(val);
        }
        else {
            new (ptr()) T(std::forward<U>(val));
            m_hasValue = true;
        }
        return *this;
    }

    template<typename... Args>
    T& emplace(Args&&... args)
    {
        reset();
        new (ptr()) T(std::forward<Args>(args)...);
        m_hasValue = true;
        return *ptr();
    }

    void reset() noexcept
    {
        if (m_hasValue) {
            ptr()->~T();
            m_hasValue = false;
        }
    }

    void swap(Optional& other) noexcept(
        std::is_nothrow_move_constructible<T>::value &&
        noexcept(std::swap(*std::declval<T*>(), *std::declval<T*>()))
    )
    {
        if (m_hasValue && other.m_hasValue) {
            using std::swap;
            swap(*ptr(), *other.ptr());
        }
        else if (m_hasValue && !other.m_hasValue) {
            new (other.ptr()) T(std::move(*ptr()));
            other.m_hasValue = true;
            reset();
        }
        else if (!m_hasValue && other.m_hasValue) {
            new (ptr()) T(std::move(*other.ptr()));
            m_hasValue = true;
            other.reset();
        }
    }

    bool has_value() const noexcept { return m_hasValue; }
    explicit operator bool() const noexcept { return m_hasValue; }

    T* operator->() noexcept { return ptr(); }
    const T* operator->() const noexcept { return ptr(); }

    T& operator*() & noexcept { return *ptr(); }
    const T& operator*() const& noexcept { return *ptr(); }
    T&& operator*() && noexcept { return std::move(*ptr()); }
    const T&& operator*() const&& noexcept { return std::move(*ptr()); }

    T& value() &
    {
        ensureValue();
        return *ptr();
    }

    const T& value() const&
    {
        ensureValue();
        return *ptr();
    }

    T&& value() &&
    {
        ensureValue();
        return std::move(*ptr());
    }

    const T&& value() const&&
    {
        ensureValue();
        return std::move(*ptr());
    }

    template<typename U>
    T value_or(U&& defaultVal) const&
    {
        if (m_hasValue) {
            return *ptr();
        }
        return static_cast<T>(std::forward<U>(defaultVal));
    }

    template<typename U>
    T value_or(U&& defaultVal) &&
    {
        if (m_hasValue) {
            return std::move(*ptr());
        }
        return static_cast<T>(std::forward<U>(defaultVal));
    }

private:
    T* ptr() noexcept { return (T*)(void*)(&m_storage); }
    const T* ptr() const noexcept { return (const T*)(const void*)(&m_storage); }

    void ensureValue() const
    {
        if (!m_hasValue) {
#if TOMLKIT11_HAS_EXCEPTIONS
            throw std::logic_error("tomlkit::Optional: bad optional access");
#else
            TOMLKIT11_ABORT();
#endif
        }
    }

    alignas(T) unsigned char m_storage[sizeof(T)];
    bool m_hasValue;
};

namespace detail
{
    template<typename T>
    struct IsOptional : std::false_type
    { };

    template<typename T>
    struct IsOptional<Optional<T>> : std::true_type
    { };
} // namespace detail

template<typename T, typename U>
inline bool operator==(const Optional<T>& lhs, const Optional<U>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || (*lhs == *rhs);
}

template<typename T, typename U>
inline bool operator!=(const Optional<T>& lhs, const Optional<U>& rhs)
{
    return !(lhs == rhs);
}

template<typename T, typename U>
inline bool operator<(const Optional<T>& lhs, const Optional<U>& rhs)
{
    if (!rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }
    return *lhs < *rhs;
}

template<typename T, typename U>
inline bool operator<=(const Optional<T>& lhs, const Optional<U>& rhs)
{
    return !(rhs < lhs);
}

template<typename T, typename U>
inline bool operator>(const Optional<T>& lhs, const Optional<U>& rhs)
{
    return rhs < lhs;
}

template<typename T, typename U>
inline bool operator>=(const Optional<T>& lhs, const Optional<U>& rhs)
{
    return !(lhs < rhs);
}

template<typename T>
inline bool operator==(const Optional<T>& opt, NulloptT) noexcept
{
    return !opt.has_value();
}

template<typename T>
inline bool operator==(NulloptT, const Optional<T>& opt) noexcept
{
    return !opt.has_value();
}

template<typename T>
inline bool operator!=(const Optional<T>& opt, NulloptT) noexcept
{
    return opt.has_value();
}

template<typename T>
inline bool operator!=(NulloptT, const Optional<T>& opt) noexcept
{
    return opt.has_value();
}

template<typename T>
inline bool operator<(const Optional<T>&, NulloptT) noexcept
{
    return false;
}

template<typename T>
inline bool operator<(NulloptT, const Optional<T>& opt) noexcept
{
    return opt.has_value();
}

template<typename T>
inline bool operator<=(const Optional<T>& opt, NulloptT) noexcept
{
    return !opt.has_value();
}

template<typename T>
inline bool operator<=(NulloptT, const Optional<T>&) noexcept
{
    return true;
}

template<typename T>
inline bool operator>(const Optional<T>& opt, NulloptT) noexcept
{
    return opt.has_value();
}

template<typename T>
inline bool operator>(NulloptT, const Optional<T>&) noexcept
{
    return false;
}

template<typename T>
inline bool operator>=(const Optional<T>&, NulloptT) noexcept
{
    return true;
}

template<typename T>
inline bool operator>=(NulloptT, const Optional<T>& opt) noexcept
{
    return !opt.has_value();
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator==(const Optional<T>& lhs, const U& rhs)
{
    return lhs.has_value() && (*lhs == rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator==(const U& lhs, const Optional<T>& rhs)
{
    return rhs.has_value() && (lhs == *rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator!=(const Optional<T>& lhs, const U& rhs)
{
    return !(lhs == rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator!=(const U& lhs, const Optional<T>& rhs)
{
    return !(lhs == rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator<(const Optional<T>& lhs, const U& rhs)
{
    return !lhs.has_value() || (*lhs < rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator<(const U& lhs, const Optional<T>& rhs)
{
    return rhs.has_value() && (lhs < *rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator<=(const Optional<T>& lhs, const U& rhs)
{
    return !lhs.has_value() || (*lhs <= rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator<=(const U& lhs, const Optional<T>& rhs)
{
    return rhs.has_value() && (lhs <= *rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator>(const Optional<T>& lhs, const U& rhs)
{
    return lhs.has_value() && (*lhs > rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator>(const U& lhs, const Optional<T>& rhs)
{
    return !rhs.has_value() || (lhs > *rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator>=(const Optional<T>& lhs, const U& rhs)
{
    return lhs.has_value() && (*lhs >= rhs);
}

template<typename T, typename U, detail::enable_if_t<
    !detail::IsOptional<detail::remove_cvref_t<U>>::value &&
    !std::is_same<detail::remove_cvref_t<U>, NulloptT>::value, int> = 0>
inline bool operator>=(const U& lhs, const Optional<T>& rhs)
{
    return !rhs.has_value() || (lhs >= *rhs);
}

#endif // TOMLKIT11_HAS_OPTIONAL

template<typename E>
class Unexpected
{
public:
    using error_type = E;

    Unexpected(const Unexpected&) = default;
    Unexpected(Unexpected&&) = default;
    Unexpected& operator=(const Unexpected&) = default;
    Unexpected& operator=(Unexpected&&) = default;

    explicit Unexpected(const E& err)
        : m_error(err)
    { }

    explicit Unexpected(E&& err) noexcept(std::is_nothrow_move_constructible<E>::value)
        : m_error(std::move(err))
    { }

    template<typename U, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Unexpected<E>>::value &&
        std::is_constructible<E, U&&>::value, int> = 0>
    explicit Unexpected(U&& err)
        : m_error(std::forward<U>(err))
    { }

    const E& error() const& noexcept { return m_error; }
    E& error() & noexcept { return m_error; }
    const E&& error() const&& noexcept { return std::move(m_error); }
    E&& error() && noexcept { return std::move(m_error); }

private:
    E m_error;
};

template<typename E>
inline Unexpected<detail::decay_t<E>> makeUnexpected(E&& err)
{
    return Unexpected<detail::decay_t<E>>(std::forward<E>(err));
}

namespace detail
{
    template<typename T>
    struct IsUnexpected : std::false_type
    { };

    template<typename E>
    struct IsUnexpected<Unexpected<E>> : std::true_type
    { };
} // namespace detail

template<typename T, typename E>
class Result
{
public:
    using value_type = T;
    using error_type = E;

    template<typename U = T, detail::enable_if_t<std::is_default_constructible<U>::value, int> = 0>
    Result() noexcept(std::is_nothrow_default_constructible<T>::value)
        : m_hasValue(true)
    {
        new (valuePtr()) T();
    }

    Result(const Result& other)
        : m_hasValue(other.m_hasValue)
    {
        if (m_hasValue) {
            new (valuePtr()) T(*other.valuePtr());
        }
        else {
            new (errorPtr()) E(*other.errorPtr());
        }
    }

    Result(Result&& other) noexcept(
        std::is_nothrow_move_constructible<T>::value &&
        std::is_nothrow_move_constructible<E>::value
    )
        : m_hasValue(other.m_hasValue)
    {
        if (m_hasValue) {
            new (valuePtr()) T(std::move(*other.valuePtr()));
        }
        else {
            new (errorPtr()) E(std::move(*other.errorPtr()));
        }
    }

    template<typename U = T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Result<T, E>>::value &&
        !detail::IsUnexpected<detail::remove_cvref_t<U>>::value &&
        std::is_constructible<T, U&&>::value &&
        std::is_convertible<U&&, T>::value, int> = 0>
    Result(U&& val)
        : m_hasValue(true)
    {
        new (valuePtr()) T(std::forward<U>(val));
    }

    template<typename U = T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Result<T, E>>::value &&
        !detail::IsUnexpected<detail::remove_cvref_t<U>>::value &&
        std::is_constructible<T, U&&>::value &&
        !std::is_convertible<U&&, T>::value, int> = 0>
    explicit Result(U&& val)
        : m_hasValue(true)
    {
        new (valuePtr()) T(std::forward<U>(val));
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, const G&>::value &&
        std::is_convertible<const G&, E>::value, int> = 0>
    Result(const Unexpected<G>& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(err.error());
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, const G&>::value &&
        !std::is_convertible<const G&, E>::value, int> = 0>
    explicit Result(const Unexpected<G>& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(err.error());
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, G&&>::value &&
        std::is_convertible<G&&, E>::value, int> = 0>
    Result(Unexpected<G>&& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(std::move(err.error()));
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, G&&>::value &&
        !std::is_convertible<G&&, E>::value, int> = 0>
    explicit Result(Unexpected<G>&& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(std::move(err.error()));
    }

    ~Result()
    {
        destroy();
    }

    Result& operator=(const Result& other)
    {
        if (this == &other) {
            return *this;
        }
        if (m_hasValue && other.m_hasValue) {
            *valuePtr() = *other.valuePtr();
        }
        else if (!m_hasValue && !other.m_hasValue) {
            *errorPtr() = *other.errorPtr();
        }
        else if (other.m_hasValue) {
            T tmp(*other.valuePtr());
            errorPtr()->~E();
            new (valuePtr()) T(std::move(tmp));
            m_hasValue = true;
        }
        else {
            E tmp(*other.errorPtr());
            valuePtr()->~T();
            new (errorPtr()) E(std::move(tmp));
            m_hasValue = false;
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible<T>::value &&
        std::is_nothrow_move_assignable<T>::value &&
        std::is_nothrow_move_constructible<E>::value &&
        std::is_nothrow_move_assignable<E>::value
    )
    {
        if (this == &other) {
            return *this;
        }
        if (m_hasValue && other.m_hasValue) {
            *valuePtr() = std::move(*other.valuePtr());
        }
        else if (!m_hasValue && !other.m_hasValue) {
            *errorPtr() = std::move(*other.errorPtr());
        }
        else if (other.m_hasValue) {
            T tmp(std::move(*other.valuePtr()));
            errorPtr()->~E();
            new (valuePtr()) T(std::move(tmp));
            m_hasValue = true;
        }
        else {
            E tmp(std::move(*other.errorPtr()));
            valuePtr()->~T();
            new (errorPtr()) E(std::move(tmp));
            m_hasValue = false;
        }
        return *this;
    }

    template<typename U = T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<U>, Result<T, E>>::value &&
        !detail::IsUnexpected<detail::remove_cvref_t<U>>::value &&
        std::is_constructible<T, U&&>::value &&
        std::is_assignable<T&, U&&>::value, int> = 0>
    Result& operator=(U&& val)
    {
        if (m_hasValue) {
            *valuePtr() = std::forward<U>(val);
        }
        else {
            T tmp(std::forward<U>(val));
            errorPtr()->~E();
            new (valuePtr()) T(std::move(tmp));
            m_hasValue = true;
        }
        return *this;
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, const G&>::value &&
        std::is_assignable<E&, const G&>::value, int> = 0>
    Result& operator=(const Unexpected<G>& err)
    {
        if (!m_hasValue) {
            *errorPtr() = err.error();
        }
        else {
            E tmp(err.error());
            valuePtr()->~T();
            new (errorPtr()) E(std::move(tmp));
            m_hasValue = false;
        }
        return *this;
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, G&&>::value &&
        std::is_assignable<E&, G&&>::value, int> = 0>
    Result& operator=(Unexpected<G>&& err)
    {
        if (!m_hasValue) {
            *errorPtr() = std::move(err.error());
        }
        else {
            E tmp(std::move(err.error()));
            valuePtr()->~T();
            new (errorPtr()) E(std::move(tmp));
            m_hasValue = false;
        }
        return *this;
    }

    bool has_value() const noexcept { return m_hasValue; }
    explicit operator bool() const noexcept { return m_hasValue; }

    T* operator->() noexcept { return valuePtr(); }
    const T* operator->() const noexcept { return valuePtr(); }

    T& operator*() & noexcept { return *valuePtr(); }
    const T& operator*() const& noexcept { return *valuePtr(); }
    T&& operator*() && noexcept { return std::move(*valuePtr()); }
    const T&& operator*() const&& noexcept { return std::move(*valuePtr()); }

    T& value() &
    {
        ensureValue();
        return *valuePtr();
    }

    const T& value() const&
    {
        ensureValue();
        return *valuePtr();
    }

    T&& value() &&
    {
        ensureValue();
        return std::move(*valuePtr());
    }

    const T&& value() const&&
    {
        ensureValue();
        return std::move(*valuePtr());
    }

    E& error() & noexcept { return *errorPtr(); }
    const E& error() const& noexcept { return *errorPtr(); }
    E&& error() && noexcept { return std::move(*errorPtr()); }
    const E&& error() const&& noexcept { return std::move(*errorPtr()); }

    template<typename U>
    T value_or(U&& defaultVal) const&
    {
        if (m_hasValue) {
            return *valuePtr();
        }
        return static_cast<T>(std::forward<U>(defaultVal));
    }

    template<typename U>
    T value_or(U&& defaultVal) &&
    {
        if (m_hasValue) {
            return std::move(*valuePtr());
        }
        return static_cast<T>(std::forward<U>(defaultVal));
    }

#if defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
    operator std::expected<T, E>() const&
    {
        if (m_hasValue) {
            return std::expected<T, E>(*valuePtr());
        }
        return std::unexpected<E>(*errorPtr());
    }

    operator std::expected<T, E>() &&
    {
        if (m_hasValue) {
            return std::expected<T, E>(std::move(*valuePtr()));
        }
        return std::unexpected<E>(std::move(*errorPtr()));
    }
#endif

private:
    static constexpr size_t kStorageSize = (sizeof(T) > sizeof(E)) ? sizeof(T) : sizeof(E);
    static constexpr size_t kStorageAlign = (alignof(T) > alignof(E)) ? alignof(T) : alignof(E);

    T* valuePtr() noexcept { return (T*)(void*)(&m_storage); }
    const T* valuePtr() const noexcept { return (const T*)(const void*)(&m_storage); }
    E* errorPtr() noexcept { return (E*)(void*)(&m_storage); }
    const E* errorPtr() const noexcept { return (const E*)(const void*)(&m_storage); }

    void destroy() noexcept
    {
        if (m_hasValue) {
            valuePtr()->~T();
        }
        else {
            errorPtr()->~E();
        }
    }

    void ensureValue() const
    {
        if (!m_hasValue) {
#if TOMLKIT11_HAS_EXCEPTIONS
            throw std::logic_error("tomlkit::Result: bad expected access");
#else
            TOMLKIT11_ABORT();
#endif
        }
    }

    alignas(kStorageAlign) unsigned char m_storage[kStorageSize];
    bool m_hasValue;
};

template<typename E>
class Result<void, E>
{
public:
    using value_type = void;
    using error_type = E;

    Result() noexcept
        : m_hasValue(true)
    { }

    Result(const Result& other)
        : m_hasValue(other.m_hasValue)
    {
        if (!m_hasValue) {
            new (errorPtr()) E(*other.errorPtr());
        }
    }

    Result(Result&& other) noexcept(std::is_nothrow_move_constructible<E>::value)
        : m_hasValue(other.m_hasValue)
    {
        if (!m_hasValue) {
            new (errorPtr()) E(std::move(*other.errorPtr()));
        }
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, const G&>::value &&
        std::is_convertible<const G&, E>::value, int> = 0>
    Result(const Unexpected<G>& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(err.error());
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, const G&>::value &&
        !std::is_convertible<const G&, E>::value, int> = 0>
    explicit Result(const Unexpected<G>& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(err.error());
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, G&&>::value &&
        std::is_convertible<G&&, E>::value, int> = 0>
    Result(Unexpected<G>&& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(std::move(err.error()));
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, G&&>::value &&
        !std::is_convertible<G&&, E>::value, int> = 0>
    explicit Result(Unexpected<G>&& err)
        : m_hasValue(false)
    {
        new (errorPtr()) E(std::move(err.error()));
    }

    ~Result()
    {
        destroy();
    }

    Result& operator=(const Result& other)
    {
        if (this == &other) {
            return *this;
        }
        if (m_hasValue && other.m_hasValue) {
            return *this;
        }
        if (!m_hasValue && !other.m_hasValue) {
            *errorPtr() = *other.errorPtr();
        }
        else if (other.m_hasValue) {
            destroy();
            m_hasValue = true;
        }
        else {
            new (errorPtr()) E(*other.errorPtr());
            m_hasValue = false;
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible<E>::value &&
        std::is_nothrow_move_assignable<E>::value
    )
    {
        if (this == &other) {
            return *this;
        }
        if (m_hasValue && other.m_hasValue) {
            return *this;
        }
        if (!m_hasValue && !other.m_hasValue) {
            *errorPtr() = std::move(*other.errorPtr());
        }
        else if (other.m_hasValue) {
            destroy();
            m_hasValue = true;
        }
        else {
            new (errorPtr()) E(std::move(*other.errorPtr()));
            m_hasValue = false;
        }
        return *this;
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, const G&>::value &&
        std::is_assignable<E&, const G&>::value, int> = 0>
    Result& operator=(const Unexpected<G>& err)
    {
        if (!m_hasValue) {
            *errorPtr() = err.error();
        }
        else {
            new (errorPtr()) E(err.error());
            m_hasValue = false;
        }
        return *this;
    }

    template<typename G, detail::enable_if_t<
        std::is_constructible<E, G&&>::value &&
        std::is_assignable<E&, G&&>::value, int> = 0>
    Result& operator=(Unexpected<G>&& err)
    {
        if (!m_hasValue) {
            *errorPtr() = std::move(err.error());
        }
        else {
            new (errorPtr()) E(std::move(err.error()));
            m_hasValue = false;
        }
        return *this;
    }

    bool has_value() const noexcept { return m_hasValue; }
    explicit operator bool() const noexcept { return m_hasValue; }

    void operator*() const noexcept { }

    void value() const
    {
        if (!m_hasValue) {
#if TOMLKIT11_HAS_EXCEPTIONS
            throw std::logic_error("tomlkit::Result<void>: bad expected access");
#else
            TOMLKIT11_ABORT();
#endif
        }
    }

    E& error() & noexcept { return *errorPtr(); }
    const E& error() const& noexcept { return *errorPtr(); }
    E&& error() && noexcept { return std::move(*errorPtr()); }
    const E&& error() const&& noexcept { return std::move(*errorPtr()); }

#if defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
    operator std::expected<void, E>() const&
    {
        if (m_hasValue) {
            return std::expected<void, E>();
        }
        return std::unexpected<E>(*errorPtr());
    }

    operator std::expected<void, E>() &&
    {
        if (m_hasValue) {
            return std::expected<void, E>();
        }
        return std::unexpected<E>(std::move(*errorPtr()));
    }
#endif

private:
    E* errorPtr() noexcept { return (E*)(void*)(&m_errorStorage); }
    const E* errorPtr() const noexcept { return (const E*)(const void*)(&m_errorStorage); }

    void destroy() noexcept
    {
        if (!m_hasValue) {
            errorPtr()->~E();
        }
    }

    alignas(E) unsigned char m_errorStorage[sizeof(E)];
    bool m_hasValue;
};

template<typename T, typename E>
inline bool operator==(const Result<T, E>& lhs, const Result<T, E>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (lhs.has_value()) {
        return *lhs == *rhs;
    }
    return lhs.error() == rhs.error();
}

template<typename T, typename E>
inline bool operator!=(const Result<T, E>& lhs, const Result<T, E>& rhs)
{
    return !(lhs == rhs);
}

template<typename E>
inline bool operator==(const Result<void, E>& lhs, const Result<void, E>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (lhs.has_value()) {
        return true;
    }
    return lhs.error() == rhs.error();
}

template<typename E>
inline bool operator!=(const Result<void, E>& lhs, const Result<void, E>& rhs)
{
    return !(lhs == rhs);
}

namespace detail
{
    TOMLKIT11_INLINE Result<std::string, ParseError> readBinaryFileUtf8Path(StringView utf8Path);
    TOMLKIT11_INLINE Result<std::string, ParseError> readBinaryFileWidePath(
        WideStringView widePath,
        std::string* pOutUtf8Path = nullptr
    );
} // namespace detail

TOMLKIT11_NAMESPACE_END

#if !TOMLKIT11_HAS_STRING_VIEW
namespace std
{
    template<typename CharT>
    struct hash<tomlkit::BasicStringViewBase<CharT>>
    {
        std::size_t operator()(tomlkit::BasicStringViewBase<CharT> sv) const noexcept
        {
            std::uint64_t h = 14695981039346656037ULL;
            const unsigned char* pBytes = (const unsigned char*)sv.data();
            const std::size_t totalBytes = sv.size() * sizeof(CharT);
            for (std::size_t i = 0; i < totalBytes; ++i) {
                h ^= (std::uint64_t)pBytes[i];
                h *= 1099511628211ULL;
            }
            return (std::size_t)h;
        }
    };
} // namespace std
#endif

#endif // TOMLKIT11_COMPAT_HPP
