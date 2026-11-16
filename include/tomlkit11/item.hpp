#pragma once

#ifndef TOMLKIT11_ITEM_HPP
#define TOMLKIT11_ITEM_HPP

#include "spec.hpp"
#include "error.hpp"
#include "types.hpp"
#include "decor.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

class Item;
using Document = Item;

template<typename T, typename Enable = void>
struct Serializer
{ };

namespace detail
{
    template<typename T>
    inline void destroyAt(T* pObj) noexcept
    {
        pObj->~T();
    }

    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortNegativeArrayIndex(int64_t index);
    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortTypeError(StringView msg);
    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortOutOfRange(StringView msg);
    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortCannotConvertKind(StringView kindName);
    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortCannotLookupKeyOnKind(StringView key, StringView kindName);
    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortCannotIndexOnKind(StringView kindName);
    [[noreturn]] TOMLKIT11_INLINE void throwOrAbortUnsignedOverflow();

    template<typename T>
    inline int64_t checkedToInt64(T val, std::true_type /*isUnsigned*/)
    {
        if ((uint64_t)val > (uint64_t)(std::numeric_limits<int64_t>::max)()) {
            throwOrAbortUnsignedOverflow();
        }
        return (int64_t)val;
    }

    template<typename T>
    inline int64_t checkedToInt64(T val, std::false_type /*isUnsigned*/) noexcept
    {
        return (int64_t)val;
    }

    template<typename T, typename = void>
    struct IsCustomTomlIntoImpl : std::false_type
    { };

    template<typename T>
    struct IsCustomTomlIntoImpl<T, void_t<decltype(
        Serializer<remove_cvref_t<T>>::toToml(
            std::declval<const remove_cvref_t<T>&>(),
            std::declval<Item&>()
        )
    )>> : std::true_type
    { };

    template<typename T>
    struct IsCustomTomlInto : IsCustomTomlIntoImpl<remove_cvref_t<T>>
    { };

    template<typename T, typename = void>
    struct IsCustomTomlFromImpl : std::false_type
    { };

    template<typename T>
    struct IsCustomTomlFromImpl<T, enable_if_t<std::is_same<
        decltype(Serializer<remove_cvref_t<T>>::fromToml(std::declval<const Item&>())),
        remove_cvref_t<T>
    >::value>> : std::true_type
    { };

    template<typename T>
    struct IsCustomTomlFrom : IsCustomTomlFromImpl<remove_cvref_t<T>>
    { };

    template<typename T>
    struct IsPathKeyLike
        : std::integral_constant<bool, std::is_convertible<const T&, StringView>::value>
    { };

    template<typename T>
    struct IsPathIndexLike
        : std::integral_constant<bool,
            std::is_integral<remove_cvref_t<T>>::value
            && !std::is_same<remove_cvref_t<T>, bool>::value>
    { };

    template<typename T>
    struct IsPathStep
        : std::integral_constant<bool, IsPathKeyLike<T>::value || IsPathIndexLike<T>::value>
    { };

    template<typename... Ts>
    struct AllPathSteps : std::true_type
    { };

    template<typename Head, typename... Tail>
    struct AllPathSteps<Head, Tail...>
        : std::integral_constant<bool, IsPathStep<Head>::value && AllPathSteps<Tail...>::value>
    { };

    template<typename Step>
    inline bool isNegativeIndex(const Step& step, std::true_type /*isSigned*/) noexcept
    {
        return step < 0;
    }

    template<typename Step>
    inline bool isNegativeIndex(const Step&, std::false_type /*isSigned*/) noexcept
    {
        return false;
    }

    template<typename Step>
    inline bool isNegativeIndexValue(const Step& step) noexcept
    {
        return isNegativeIndex(step, std::is_signed<remove_cvref_t<Step>>());
    }
} // namespace detail

struct ArrayElement
{
    TOMLKIT11_INLINE Item* pValue() noexcept;
    TOMLKIT11_INLINE const Item* pValue() const noexcept;

    std::unique_ptr<Item> value;
    std::string beforeComma;
    std::string afterComma;
    bool hasComma = false;

    TOMLKIT11_INLINE ArrayElement();
    TOMLKIT11_INLINE explicit ArrayElement(Item val);
    TOMLKIT11_INLINE ArrayElement(const ArrayElement& other);
    TOMLKIT11_INLINE ArrayElement(ArrayElement&& other) noexcept;
    TOMLKIT11_INLINE ArrayElement& operator=(const ArrayElement& other);
    TOMLKIT11_INLINE ArrayElement& operator=(ArrayElement&& other) noexcept;
    TOMLKIT11_INLINE ~ArrayElement();
};

struct ArrayStorage
{
    ArrayStyle style = ArrayStyle::Standard;
    bool multiline = false;
    bool hasExplicitLayout = false;
    std::vector<ArrayElement> elements;
    std::string trailing;

    TOMLKIT11_INLINE ArrayStorage();
    TOMLKIT11_INLINE ArrayStorage(const ArrayStorage& other);
    TOMLKIT11_INLINE ArrayStorage(ArrayStorage&& other) noexcept;
    TOMLKIT11_INLINE ArrayStorage& operator=(const ArrayStorage& other);
    TOMLKIT11_INLINE ArrayStorage& operator=(ArrayStorage&& other) noexcept;
    TOMLKIT11_INLINE ~ArrayStorage();
};

struct TableEntry
{
    Key key;
    std::unique_ptr<Item> value;
    uint32_t entryOrder = 0;

    // Used under TableStyle::Inline to preserve whitespace and inline comments around commas.
    std::string beforeComma;
    std::string afterComma;
    bool hasComma = false;

    TOMLKIT11_INLINE TableEntry(Key k, Item v, uint32_t order = 0);
    TOMLKIT11_INLINE TableEntry(const TableEntry& other);
    TOMLKIT11_INLINE TableEntry(TableEntry&& other) noexcept;
    TOMLKIT11_INLINE TableEntry& operator=(const TableEntry& other);
    TOMLKIT11_INLINE TableEntry& operator=(TableEntry&& other) noexcept;
    TOMLKIT11_INLINE ~TableEntry();

    TOMLKIT11_INLINE void relinkOwner() noexcept;
};

struct TableStorage
{
    static constexpr size_t kHashIndexThreshold = 16;

    TableStyle style = TableStyle::Standard;
    bool hasBom = false;
    bool multilineInline = false;

    // Parse-time state (enforcing TOML v1.1 #859 table and dotted-key redefinition rules).
    bool isSealed = false;
    bool isExplicitlyDefined = false;
    uint32_t dottedKeyScopeId = 0;

    // Document-level [header] appearance order (0 means programmatically created).
    uint32_t sectionOrder = 0;
    uint32_t nextEntryOrder = 1;

    Decor headerDecor{};
    std::vector<KeySegment> headerKeyPath;

    std::vector<std::unique_ptr<TableEntry>> entries;
    std::string trailing;

    // Open-addressing hash index slots (0 = empty, otherwise entryIndex + 1).
    mutable std::vector<size_t> hashSlots;
    mutable bool hashDirty = true;

    TOMLKIT11_INLINE TableStorage();
    TOMLKIT11_INLINE explicit TableStorage(TableStyle tblStyle);
    TOMLKIT11_INLINE TableStorage(const TableStorage& other);
    TOMLKIT11_INLINE TableStorage(TableStorage&& other) noexcept;
    TOMLKIT11_INLINE TableStorage& operator=(const TableStorage& other);
    TOMLKIT11_INLINE TableStorage& operator=(TableStorage&& other) noexcept;
    TOMLKIT11_INLINE ~TableStorage();

    TOMLKIT11_INLINE size_t findIndex(StringView keyName) const noexcept;
    void invalidateIndex() noexcept { hashDirty = true; }
    TOMLKIT11_INLINE void rebuildIndexIfNeeded() const;
};

// Array element dereferencing iterator, supporting range-based for and standard algorithms.
template<bool IsConst>
class ArrayIterator
{
public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using value_type = Item;
    using difference_type = ptrdiff_t;
    using pointer = detail::conditional_t<IsConst, const Item*, Item*>;
    using reference = detail::conditional_t<IsConst, const Item&, Item&>;
    using ElemIter = detail::conditional_t<
        IsConst,
        std::vector<ArrayElement>::const_iterator,
        std::vector<ArrayElement>::iterator
    >;

    ArrayIterator() = default;

    explicit ArrayIterator(ElemIter it) noexcept
        : m_it(it)
    { }

    template<bool WasConst, detail::enable_if_t<IsConst && !WasConst, int> = 0>
    ArrayIterator(const ArrayIterator<WasConst>& other) noexcept
        : m_it(other.rawIter())
    { }

    ElemIter rawIter() const noexcept { return m_it; }

    reference operator*() const noexcept { return *m_it->value; }
    pointer operator->() const noexcept { return m_it->value.get(); }
    reference operator[](difference_type n) const noexcept { return *m_it[n].value; }

    ArrayIterator& operator++() noexcept { ++m_it; return *this; }
    ArrayIterator operator++(int) noexcept { ArrayIterator tmp = *this; ++m_it; return tmp; }
    ArrayIterator& operator--() noexcept { --m_it; return *this; }
    ArrayIterator operator--(int) noexcept { ArrayIterator tmp = *this; --m_it; return tmp; }

    ArrayIterator& operator+=(difference_type n) noexcept { m_it += n; return *this; }
    ArrayIterator& operator-=(difference_type n) noexcept { m_it -= n; return *this; }

    friend ArrayIterator operator+(ArrayIterator it, difference_type n) noexcept { it += n; return it; }
    friend ArrayIterator operator+(difference_type n, ArrayIterator it) noexcept { it += n; return it; }
    friend ArrayIterator operator-(ArrayIterator it, difference_type n) noexcept { it -= n; return it; }
    friend difference_type operator-(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it - b.m_it; }

    friend bool operator==(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it == b.m_it; }
    friend bool operator!=(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it != b.m_it; }
    friend bool operator<(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it < b.m_it; }
    friend bool operator<=(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it <= b.m_it; }
    friend bool operator>(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it > b.m_it; }
    friend bool operator>=(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it >= b.m_it; }

private:
    ElemIter m_it{};
};

// Table key-value pair view, supporting kv.key / kv.value in C++11 and structured bindings in C++17+.
template<bool IsConst>
struct TableKeyValuePair
{
    using ItemRef = detail::conditional_t<IsConst, const Item&, Item&>;

    const Key& key;
    ItemRef value;
};

template<size_t I, bool IsConst>
inline detail::enable_if_t<I == 0, const Key&>
get(const TableKeyValuePair<IsConst>& pair) noexcept
{
    return pair.key;
}

template<size_t I, bool IsConst>
inline detail::enable_if_t<I == 1, typename TableKeyValuePair<IsConst>::ItemRef>
get(const TableKeyValuePair<IsConst>& pair) noexcept
{
    return pair.value;
}

template<bool IsConst>
class TableIterator
{
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    using value_type = TableKeyValuePair<IsConst>;
    using difference_type = ptrdiff_t;
    using EntryIter = detail::conditional_t<
        IsConst,
        std::vector<std::unique_ptr<TableEntry>>::const_iterator,
        std::vector<std::unique_ptr<TableEntry>>::iterator
    >;

    TableIterator() = default;

    explicit TableIterator(EntryIter it) noexcept
        : m_it(it)
    { }

    template<bool WasConst, detail::enable_if_t<IsConst && !WasConst, int> = 0>
    TableIterator(const TableIterator<WasConst>& other) noexcept
        : m_it(other.rawIter())
    { }

    EntryIter rawIter() const noexcept { return m_it; }

    value_type operator*() const noexcept
    {
        return value_type{ (*m_it)->key, *(*m_it)->value };
    }

    TableIterator& operator++() noexcept { ++m_it; return *this; }
    TableIterator operator++(int) noexcept { TableIterator tmp = *this; ++m_it; return tmp; }
    TableIterator& operator--() noexcept { --m_it; return *this; }
    TableIterator operator--(int) noexcept { TableIterator tmp = *this; --m_it; return tmp; }

    friend bool operator==(const TableIterator& a, const TableIterator& b) noexcept { return a.m_it == b.m_it; }
    friend bool operator!=(const TableIterator& a, const TableIterator& b) noexcept { return a.m_it != b.m_it; }

private:
    EntryIter m_it{};
};

template<bool IsConst>
class TableItemsRange
{
public:
    using EntryVec = detail::conditional_t<
        IsConst,
        const std::vector<std::unique_ptr<TableEntry>>,
        std::vector<std::unique_ptr<TableEntry>>
    >;

    explicit TableItemsRange(EntryVec* pEntries) noexcept
        : m_pEntries(pEntries)
    { }

    TableIterator<IsConst> begin() const noexcept
    {
        return m_pEntries ? TableIterator<IsConst>(m_pEntries->begin()) : TableIterator<IsConst>();
    }

    TableIterator<IsConst> end() const noexcept
    {
        return m_pEntries ? TableIterator<IsConst>(m_pEntries->end()) : TableIterator<IsConst>();
    }

    size_t size() const noexcept { return m_pEntries ? m_pEntries->size() : 0; }
    bool empty() const noexcept { return size() == 0; }

private:
    EntryVec* m_pEntries = nullptr;
};

namespace detail
{
    template<typename FallbackT>
    using NormalizedFallbackType = conditional_t<
        std::is_convertible<FallbackT, StringView>::value
        && !std::is_same<remove_cvref_t<FallbackT>, StringView>::value,
        std::string,
        remove_cvref_t<FallbackT>
    >;

    template<typename T>
    struct TryGetHelper;
} // namespace detail

// Core dynamic TOML node holding scalars, arrays, inline tables, standard tables, or the document root,
// while preserving full CST formatting and comment trivia.
class Item
{
public:
    using iterator = ArrayIterator<false>;
    using const_iterator = ArrayIterator<true>;

    // ==================== Construction & Static Factories ====================
    TOMLKIT11_INLINE Item() noexcept;
    TOMLKIT11_INLINE Item(const Item& other);
    TOMLKIT11_INLINE Item(Item&& other) noexcept;
    TOMLKIT11_INLINE Item& operator=(const Item& other);
    TOMLKIT11_INLINE Item& operator=(Item&& other) noexcept;
    TOMLKIT11_INLINE ~Item();

    TOMLKIT11_INLINE Item(bool val) noexcept;

    template<typename T, detail::enable_if_t<
        std::is_integral<detail::remove_cvref_t<T>>::value
        && !std::is_same<detail::remove_cvref_t<T>, bool>::value
        && !std::is_same<detail::remove_cvref_t<T>, char>::value, int> = 0>
    Item(T val)
        : m_kind(ItemKind::Empty)
        , m_storage()
    {
        const int64_t checked = detail::checkedToInt64(
            val,
            std::is_unsigned<detail::remove_cvref_t<T>>()
        );
        m_storage.m_int = checked;
        m_kind = ItemKind::Integer;
    }

    template<typename T, detail::enable_if_t<
        std::is_floating_point<detail::remove_cvref_t<T>>::value, int> = 0>
    Item(T val) noexcept
        : m_kind(ItemKind::Float)
        , m_storage()
    {
        m_storage.m_float = (double)val;
    }

    TOMLKIT11_INLINE Item(const char* pStr);
    TOMLKIT11_INLINE Item(StringView sv);
    TOMLKIT11_INLINE Item(std::string str);

    TOMLKIT11_INLINE Item(LocalDate val) noexcept;
    TOMLKIT11_INLINE Item(LocalTime val) noexcept;
    TOMLKIT11_INLINE Item(LocalDateTime val) noexcept;
    TOMLKIT11_INLINE Item(OffsetDateTime val) noexcept;

    // Brace-init constructs an Array by default so { "name", 1 } is never misread as a Table.
    TOMLKIT11_INLINE Item(std::initializer_list<Item> arrayElements);

    template<typename T, detail::enable_if_t<
        detail::IsCustomTomlInto<detail::remove_cvref_t<T>>::value
        && !std::is_same<detail::remove_cvref_t<T>, Item>::value, int> = 0>
    Item(const T& customVal)
        : m_kind(ItemKind::Empty)
        , m_storage()
    {
        Serializer<detail::remove_cvref_t<T>>::toToml(customVal, *this);
    }

    static TOMLKIT11_INLINE Item document();
    static TOMLKIT11_INLINE Item array();
    static TOMLKIT11_INLINE Item array(std::initializer_list<Item> init);
    static TOMLKIT11_INLINE Item arrayOfTables();
    static TOMLKIT11_INLINE Item table(TableStyle style = TableStyle::Standard);
    static TOMLKIT11_INLINE Item table(
        std::initializer_list<std::pair<StringView, Item>> init,
        TableStyle style = TableStyle::Standard
    );
    static TOMLKIT11_INLINE Item inlineTable();
    static TOMLKIT11_INLINE Item inlineTable(std::initializer_list<std::pair<StringView, Item>> init);

    template<typename T, detail::enable_if_t<
        !std::is_same<detail::remove_cvref_t<T>, Item>::value
        && std::is_constructible<Item, T&&>::value, int> = 0>
    Item& operator=(T&& val)
    {
        assignPreservingDecor(Item(std::forward<T>(val)));
        return *this;
    }

    Item& operator=(std::initializer_list<Item> arrayElements)
    {
        assignPreservingDecor(Item(arrayElements));
        return *this;
    }

    // ==================== Kind Discrimination ====================
    ItemKind kind() const noexcept { return m_kind; }
    StringView kindName() const noexcept { return itemKindName(m_kind); }

    bool isEmpty() const noexcept { return m_kind == ItemKind::Empty; }
    bool isBoolean() const noexcept { return m_kind == ItemKind::Boolean; }
    bool isInteger() const noexcept { return m_kind == ItemKind::Integer; }
    bool isFloat() const noexcept { return m_kind == ItemKind::Float; }
    bool isNumber() const noexcept { return isInteger() || isFloat(); }
    bool isString() const noexcept { return m_kind == ItemKind::String; }
    bool isLocalDate() const noexcept { return m_kind == ItemKind::LocalDate; }
    bool isLocalTime() const noexcept { return m_kind == ItemKind::LocalTime; }
    bool isLocalDateTime() const noexcept { return m_kind == ItemKind::LocalDateTime; }
    bool isOffsetDateTime() const noexcept { return m_kind == ItemKind::OffsetDateTime; }
    bool isDateTime() const noexcept
    {
        return isLocalDate() || isLocalTime() || isLocalDateTime() || isOffsetDateTime();
    }
    bool isArray() const noexcept { return m_kind == ItemKind::Array; }
    bool isTable() const noexcept { return m_kind == ItemKind::Table; }
    TOMLKIT11_INLINE bool isInlineTable() const noexcept;
    TOMLKIT11_INLINE bool isArrayOfTables() const noexcept;

    // ==================== Non-Throwing Scalar Pointer Access ====================
    bool* ifBoolean() noexcept
    {
        if (m_kind != ItemKind::Boolean) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_bool;
    }

    const bool* ifBoolean() const noexcept
    {
        return (m_kind == ItemKind::Boolean) ? &m_storage.m_bool : nullptr;
    }

    int64_t* ifInteger() noexcept
    {
        if (m_kind != ItemKind::Integer) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_int;
    }

    const int64_t* ifInteger() const noexcept
    {
        return (m_kind == ItemKind::Integer) ? &m_storage.m_int : nullptr;
    }

    double* ifFloat() noexcept
    {
        if (m_kind != ItemKind::Float) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_float;
    }

    const double* ifFloat() const noexcept
    {
        return (m_kind == ItemKind::Float) ? &m_storage.m_float : nullptr;
    }

    std::string* ifString() noexcept
    {
        if (m_kind != ItemKind::String) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_string;
    }

    const std::string* ifString() const noexcept
    {
        return (m_kind == ItemKind::String) ? &m_storage.m_string : nullptr;
    }

    LocalDate* ifLocalDate() noexcept
    {
        if (m_kind != ItemKind::LocalDate) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_localDate;
    }

    const LocalDate* ifLocalDate() const noexcept
    {
        return (m_kind == ItemKind::LocalDate) ? &m_storage.m_localDate : nullptr;
    }

    LocalTime* ifLocalTime() noexcept
    {
        if (m_kind != ItemKind::LocalTime) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_localTime;
    }

    const LocalTime* ifLocalTime() const noexcept
    {
        return (m_kind == ItemKind::LocalTime) ? &m_storage.m_localTime : nullptr;
    }

    LocalDateTime* ifLocalDateTime() noexcept
    {
        if (m_kind != ItemKind::LocalDateTime) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_localDateTime;
    }

    const LocalDateTime* ifLocalDateTime() const noexcept
    {
        return (m_kind == ItemKind::LocalDateTime) ? &m_storage.m_localDateTime : nullptr;
    }

    OffsetDateTime* ifOffsetDateTime() noexcept
    {
        if (m_kind != ItemKind::OffsetDateTime) {
            return nullptr;
        }
        m_rawRepr.clear();
        return &m_storage.m_offsetDateTime;
    }

    const OffsetDateTime* ifOffsetDateTime() const noexcept
    {
        return (m_kind == ItemKind::OffsetDateTime) ? &m_storage.m_offsetDateTime : nullptr;
    }

#if TOMLKIT11_HAS_EXCEPTIONS
    // ==================== Checked Reference Access ====================
    bool& asBoolean()
    {
        ensureKind(ItemKind::Boolean);
        m_rawRepr.clear();
        return m_storage.m_bool;
    }

    const bool& asBoolean() const
    {
        ensureKind(ItemKind::Boolean);
        return m_storage.m_bool;
    }

    int64_t& asInteger()
    {
        ensureKind(ItemKind::Integer);
        m_rawRepr.clear();
        return m_storage.m_int;
    }

    const int64_t& asInteger() const
    {
        ensureKind(ItemKind::Integer);
        return m_storage.m_int;
    }

    double& asFloat()
    {
        ensureKind(ItemKind::Float);
        m_rawRepr.clear();
        return m_storage.m_float;
    }

    const double& asFloat() const
    {
        ensureKind(ItemKind::Float);
        return m_storage.m_float;
    }

    std::string& asString()
    {
        ensureKind(ItemKind::String);
        m_rawRepr.clear();
        return m_storage.m_string;
    }

    const std::string& asString() const
    {
        ensureKind(ItemKind::String);
        return m_storage.m_string;
    }

    LocalDate& asLocalDate()
    {
        ensureKind(ItemKind::LocalDate);
        m_rawRepr.clear();
        return m_storage.m_localDate;
    }

    const LocalDate& asLocalDate() const
    {
        ensureKind(ItemKind::LocalDate);
        return m_storage.m_localDate;
    }

    LocalTime& asLocalTime()
    {
        ensureKind(ItemKind::LocalTime);
        m_rawRepr.clear();
        return m_storage.m_localTime;
    }

    const LocalTime& asLocalTime() const
    {
        ensureKind(ItemKind::LocalTime);
        return m_storage.m_localTime;
    }

    LocalDateTime& asLocalDateTime()
    {
        ensureKind(ItemKind::LocalDateTime);
        m_rawRepr.clear();
        return m_storage.m_localDateTime;
    }

    const LocalDateTime& asLocalDateTime() const
    {
        ensureKind(ItemKind::LocalDateTime);
        return m_storage.m_localDateTime;
    }

    OffsetDateTime& asOffsetDateTime()
    {
        ensureKind(ItemKind::OffsetDateTime);
        m_rawRepr.clear();
        return m_storage.m_offsetDateTime;
    }

    const OffsetDateTime& asOffsetDateTime() const
    {
        ensureKind(ItemKind::OffsetDateTime);
        return m_storage.m_offsetDateTime;
    }

    TOMLKIT11_INLINE double asNumber() const;
#endif // TOMLKIT11_HAS_EXCEPTIONS

    // ==================== Generic Extraction ====================
    template<typename T>
    Optional<T> tryGet() const;

#if TOMLKIT11_HAS_EXCEPTIONS
    template<typename T>
    T get() const;

    template<typename T>
    void getTo(T& dst) const;
#endif

    // ==================== Container Capacity, Lookup, Subscript & Mutation ====================
    TOMLKIT11_INLINE size_t size() const noexcept;
    TOMLKIT11_INLINE bool empty() const noexcept;
    TOMLKIT11_INLINE void clear();

    TOMLKIT11_INLINE bool contains(StringView key) const noexcept;

    TOMLKIT11_INLINE Item* find(StringView key) noexcept;
    TOMLKIT11_INLINE const Item* find(StringView key) const noexcept;

    TOMLKIT11_INLINE Item* find(size_t index) noexcept;
    TOMLKIT11_INLINE const Item* find(size_t index) const noexcept;

    template<typename First, typename Second, detail::enable_if_t<
        detail::IsPathStep<First>::value && detail::IsPathStep<Second>::value, int> = 0>
    Item* find(First&& first, Second&& second) noexcept
    {
        Item* pNext = stepFind(std::forward<First>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return pNext->stepFind(std::forward<Second>(second));
    }

    template<typename First, typename Second, detail::enable_if_t<
        detail::IsPathStep<First>::value && detail::IsPathStep<Second>::value, int> = 0>
    const Item* find(First&& first, Second&& second) const noexcept
    {
        const Item* pNext = stepFind(std::forward<First>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return pNext->stepFind(std::forward<Second>(second));
    }

    template<typename First, typename Second, typename Third, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Third, Rest...>::value, int> = 0>
    Item* find(First&& first, Second&& second, Third&& third, Rest&&... rest) noexcept
    {
        Item* pNext = stepFind(std::forward<First>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return pNext->find(
            std::forward<Second>(second),
            std::forward<Third>(third),
            std::forward<Rest>(rest)...
        );
    }

    template<typename First, typename Second, typename Third, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Third, Rest...>::value, int> = 0>
    const Item* find(First&& first, Second&& second, Third&& third, Rest&&... rest) const noexcept
    {
        const Item* pNext = stepFind(std::forward<First>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return pNext->find(
            std::forward<Second>(second),
            std::forward<Third>(third),
            std::forward<Rest>(rest)...
        );
    }

    // Mutating variadic path helper: creates missing intermediate tables and returns Item&.
    template<typename Step, detail::enable_if_t<detail::IsPathStep<Step>::value, int> = 0>
    Item& ensure(Step&& step)
    {
        return stepSubscriptMutable(std::forward<Step>(step));
    }

    template<typename First, typename Second, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Rest...>::value, int> = 0>
    Item& ensure(First&& first, Second&& second, Rest&&... rest)
    {
        Item& next = stepSubscriptMutable(std::forward<First>(first));
        return next.ensure(std::forward<Second>(second), std::forward<Rest>(rest)...);
    }

#if TOMLKIT11_HAS_EXCEPTIONS
    TOMLKIT11_INLINE Item& at(StringView key);
    TOMLKIT11_INLINE const Item& at(StringView key) const;

    TOMLKIT11_INLINE Item& at(size_t index);
    TOMLKIT11_INLINE const Item& at(size_t index) const;

    template<typename First, typename Second, detail::enable_if_t<
        detail::IsPathStep<First>::value && detail::IsPathStep<Second>::value, int> = 0>
    Item& at(First&& first, Second&& second)
    {
        Item& next = stepAt(std::forward<First>(first));
        return next.stepAt(std::forward<Second>(second));
    }

    template<typename First, typename Second, detail::enable_if_t<
        detail::IsPathStep<First>::value && detail::IsPathStep<Second>::value, int> = 0>
    const Item& at(First&& first, Second&& second) const
    {
        const Item& next = stepAt(std::forward<First>(first));
        return next.stepAt(std::forward<Second>(second));
    }

    template<typename First, typename Second, typename Third, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Third, Rest...>::value, int> = 0>
    Item& at(First&& first, Second&& second, Third&& third, Rest&&... rest)
    {
        Item& next = stepAt(std::forward<First>(first));
        return next.at(
            std::forward<Second>(second),
            std::forward<Third>(third),
            std::forward<Rest>(rest)...
        );
    }

    template<typename First, typename Second, typename Third, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Third, Rest...>::value, int> = 0>
    const Item& at(First&& first, Second&& second, Third&& third, Rest&&... rest) const
    {
        const Item& next = stepAt(std::forward<First>(first));
        return next.at(
            std::forward<Second>(second),
            std::forward<Third>(third),
            std::forward<Rest>(rest)...
        );
    }
#endif // TOMLKIT11_HAS_EXCEPTIONS

    // Single-dimension subscripts: mutable key auto-vivifies; const key and array index check bounds.
    TOMLKIT11_INLINE Item& operator[](StringView key);
    Item& operator[](const char* pKey)
    {
        return (*this)[pKey != nullptr ? StringView(pKey) : StringView("")];
    }
    Item& operator[](const std::string& key)
    {
        return (*this)[StringView(key)];
    }

    TOMLKIT11_INLINE const Item& operator[](StringView key) const;
    const Item& operator[](const char* pKey) const
    {
        return (*this)[pKey != nullptr ? StringView(pKey) : StringView("")];
    }
    const Item& operator[](const std::string& key) const
    {
        return (*this)[StringView(key)];
    }

    template<typename Idx, detail::enable_if_t<detail::IsPathIndexLike<Idx>::value, int> = 0>
    Item& operator[](Idx index)
    {
        if (detail::isNegativeIndexValue(index)) {
            detail::throwOrAbortNegativeArrayIndex((int64_t)index);
        }
        return arrayElementAtMutable((size_t)index);
    }

    template<typename Idx, detail::enable_if_t<detail::IsPathIndexLike<Idx>::value, int> = 0>
    const Item& operator[](Idx index) const
    {
        if (detail::isNegativeIndexValue(index)) {
            detail::throwOrAbortNegativeArrayIndex((int64_t)index);
        }
        return arrayElementAtConst((size_t)index);
    }

#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    // C++23 multidimensional subscript: doc["workspace", "profile", "release", "opt"] = 3;
    template<typename First, typename Second, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Rest...>::value, int> = 0>
    Item& operator[](First&& first, Second&& second, Rest&&... rest)
    {
        return ensure(
            std::forward<First>(first),
            std::forward<Second>(second),
            std::forward<Rest>(rest)...
        );
    }

    template<typename First, typename Second, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Rest...>::value, int> = 0>
    const Item& operator[](First&& first, Second&& second, Rest&&... rest) const
    {
        return constSubscriptChain(
            std::forward<First>(first),
            std::forward<Second>(second),
            std::forward<Rest>(rest)...
        );
    }
#endif // TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT

    template<typename T>
    auto valueOr(StringView key, T&& fallback) const -> detail::NormalizedFallbackType<T>
    {
        using OutT = detail::NormalizedFallbackType<T>;
        if (const Item* pFound = find(key)) {
            Optional<OutT> opt = pFound->template tryGet<OutT>();
            if (opt.has_value()) {
                return std::move(*opt);
            }
        }
        return OutT(std::forward<T>(fallback));
    }

    // Array and Table modifiers
    TOMLKIT11_INLINE void push_back(const Item& elem);
    TOMLKIT11_INLINE void push_back(Item&& elem);

    template<typename... Args>
    Item& emplace_back(Args&&... args)
    {
        push_back(Item(std::forward<Args>(args)...));
        return back();
    }

    TOMLKIT11_INLINE Item& front();
    TOMLKIT11_INLINE const Item& front() const;
    TOMLKIT11_INLINE Item& back();
    TOMLKIT11_INLINE const Item& back() const;

    TOMLKIT11_INLINE bool erase(StringView key);
    TOMLKIT11_INLINE void erase(size_t index);

    TOMLKIT11_INLINE std::pair<Item&, bool> insertOrAssign(StringView key, Item val);

    // Iterators (Array iterates elements directly; Table iterates via tableItems()).
    TOMLKIT11_INLINE iterator begin();
    TOMLKIT11_INLINE iterator end();
    TOMLKIT11_INLINE const_iterator begin() const;
    TOMLKIT11_INLINE const_iterator end() const;
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    TOMLKIT11_INLINE TableItemsRange<false> tableItems();
    TOMLKIT11_INLINE TableItemsRange<true> tableItems() const;

    // ==================== Comments, Key Metadata & Formatting Styles ====================
    Decor& decor() noexcept { return m_decor; }
    const Decor& decor() const noexcept { return m_decor; }

    TOMLKIT11_INLINE Optional<std::string> inlineComment() const;
    TOMLKIT11_INLINE Item& setInlineComment(StringView comment);
    TOMLKIT11_INLINE Item& clearInlineComment();

    TOMLKIT11_INLINE std::vector<std::string> leadingComments() const;
    TOMLKIT11_INLINE Item& addLeadingComment(StringView comment, StringView newline = "\n");
    TOMLKIT11_INLINE Item& clearLeadingComments();

    TOMLKIT11_INLINE Key* findKey(StringView key) noexcept;
    TOMLKIT11_INLINE const Key* findKey(StringView key) const noexcept;

#if TOMLKIT11_HAS_EXCEPTIONS
    TOMLKIT11_INLINE Key& keyAt(StringView key);
    TOMLKIT11_INLINE const Key& keyAt(StringView key) const;
#endif

    StringStyle stringStyle() const noexcept { return m_stringStyle; }
    TOMLKIT11_INLINE Item& setStringStyle(StringStyle style) noexcept;

    IntegerRadix integerRadix() const noexcept { return m_intRadix; }
    TOMLKIT11_INLINE Item& setIntegerRadix(IntegerRadix radix) noexcept;

    TOMLKIT11_INLINE TableStyle tableStyle() const noexcept;
    TOMLKIT11_INLINE Item& setTableStyle(TableStyle style);

    TOMLKIT11_INLINE ArrayStyle arrayStyle() const noexcept;
    TOMLKIT11_INLINE Item& setArrayStyle(ArrayStyle style);

    TOMLKIT11_INLINE Item& setMultilineArray(bool multiline);
    TOMLKIT11_INLINE Item& setMultilineInlineTable(bool multiline);

    // Serialization shortcuts (defined in detail/emitter.ipp).
    TOMLKIT11_INLINE std::string toToml(FormatOptions options = FormatOptions()) const;
    std::string dump(FormatOptions options = FormatOptions()) const { return toToml(options); }

    // Semantic equality (ignores comments, indentation, radix, and raw formatting).
    friend TOMLKIT11_INLINE bool operator==(const Item& lhs, const Item& rhs) noexcept;

    // Internal Parser / Emitter CST storage access.
    const std::string& rawRepr() const noexcept { return m_rawRepr; }
    void setRawRepr(std::string raw) { m_rawRepr = std::move(raw); }
    void clearRawRepr() noexcept { m_rawRepr.clear(); }

    TOMLKIT11_INLINE ArrayStorage* arrayStorage() noexcept;
    TOMLKIT11_INLINE const ArrayStorage* arrayStorage() const noexcept;

    TOMLKIT11_INLINE TableStorage* tableStorage() noexcept;
    TOMLKIT11_INLINE const TableStorage* tableStorage() const noexcept;

    void setOwningKey(Key* pKey) noexcept { m_pOwningKey = pKey; }
    Key* owningKey() noexcept { return m_pOwningKey; }
    const Key* owningKey() const noexcept { return m_pOwningKey; }

private:
    template<typename T>
    friend struct detail::TryGetHelper;

    union Storage
    {
        unsigned char m_empty;
        bool m_bool;
        int64_t m_int;
        double m_float;
        std::string m_string;
        LocalDate m_localDate;
        LocalTime m_localTime;
        LocalDateTime m_localDateTime;
        OffsetDateTime m_offsetDateTime;
        std::unique_ptr<ArrayStorage> m_array;
        std::unique_ptr<TableStorage> m_table;

        Storage() noexcept
            : m_empty(0)
        { }

        ~Storage() noexcept
        { }
    };

    TOMLKIT11_INLINE void destroyPayload() noexcept;
    TOMLKIT11_INLINE void copyPayloadFrom(const Item& other);
    TOMLKIT11_INLINE void movePayloadFrom(Item&& other) noexcept;
    TOMLKIT11_INLINE void setArrayPayload(std::unique_ptr<ArrayStorage> pArr) noexcept;
    TOMLKIT11_INLINE void setTablePayload(std::unique_ptr<TableStorage> pTbl) noexcept;

    TOMLKIT11_INLINE void ensureKind(ItemKind expected) const;
    TOMLKIT11_INLINE void assignPreservingDecor(Item&& replacement);

    TOMLKIT11_INLINE Item& arrayElementAtMutable(size_t index);
    TOMLKIT11_INLINE const Item& arrayElementAtConst(size_t index) const;
    TOMLKIT11_INLINE const Item& tableElementAtConst(StringView key) const;

    template<typename Step, detail::enable_if_t<detail::IsPathKeyLike<Step>::value, int> = 0>
    Item* stepFind(Step&& step) noexcept
    {
        return find(StringView(step));
    }

    template<typename Step, detail::enable_if_t<detail::IsPathIndexLike<Step>::value, int> = 0>
    Item* stepFind(Step&& step) noexcept
    {
        if (detail::isNegativeIndexValue(step)) {
            return nullptr;
        }
        return find((size_t)step);
    }

    template<typename Step, detail::enable_if_t<detail::IsPathKeyLike<Step>::value, int> = 0>
    const Item* stepFind(Step&& step) const noexcept
    {
        return find(StringView(step));
    }

    template<typename Step, detail::enable_if_t<detail::IsPathIndexLike<Step>::value, int> = 0>
    const Item* stepFind(Step&& step) const noexcept
    {
        if (detail::isNegativeIndexValue(step)) {
            return nullptr;
        }
        return find((size_t)step);
    }

#if TOMLKIT11_HAS_EXCEPTIONS
    template<typename Step, detail::enable_if_t<detail::IsPathKeyLike<Step>::value, int> = 0>
    Item& stepAt(Step&& step)
    {
        return at(StringView(step));
    }

    template<typename Step, detail::enable_if_t<detail::IsPathIndexLike<Step>::value, int> = 0>
    Item& stepAt(Step&& step)
    {
        if (detail::isNegativeIndexValue(step)) {
            detail::throwOrAbortNegativeArrayIndex((int64_t)step);
        }
        return at((size_t)step);
    }

    template<typename Step, detail::enable_if_t<detail::IsPathKeyLike<Step>::value, int> = 0>
    const Item& stepAt(Step&& step) const
    {
        return at(StringView(step));
    }

    template<typename Step, detail::enable_if_t<detail::IsPathIndexLike<Step>::value, int> = 0>
    const Item& stepAt(Step&& step) const
    {
        if (detail::isNegativeIndexValue(step)) {
            detail::throwOrAbortNegativeArrayIndex((int64_t)step);
        }
        return at((size_t)step);
    }
#endif // TOMLKIT11_HAS_EXCEPTIONS

    template<typename Step, detail::enable_if_t<detail::IsPathKeyLike<Step>::value, int> = 0>
    Item& stepSubscriptMutable(Step&& step)
    {
        return (*this)[StringView(step)];
    }

    template<typename Step, detail::enable_if_t<detail::IsPathIndexLike<Step>::value, int> = 0>
    Item& stepSubscriptMutable(Step&& step)
    {
        if (detail::isNegativeIndexValue(step)) {
            detail::throwOrAbortNegativeArrayIndex((int64_t)step);
        }
        return arrayElementAtMutable((size_t)step);
    }

    template<typename Step, detail::enable_if_t<detail::IsPathKeyLike<Step>::value, int> = 0>
    const Item& stepSubscriptConst(Step&& step) const
    {
        return tableElementAtConst(StringView(step));
    }

    template<typename Step, detail::enable_if_t<detail::IsPathIndexLike<Step>::value, int> = 0>
    const Item& stepSubscriptConst(Step&& step) const
    {
        if (detail::isNegativeIndexValue(step)) {
            detail::throwOrAbortNegativeArrayIndex((int64_t)step);
        }
        return arrayElementAtConst((size_t)step);
    }

    template<typename Step, detail::enable_if_t<detail::IsPathStep<Step>::value, int> = 0>
    const Item& constSubscriptChain(Step&& step) const
    {
        return stepSubscriptConst(std::forward<Step>(step));
    }

    template<typename First, typename Second, typename... Rest, detail::enable_if_t<
        detail::AllPathSteps<First, Second, Rest...>::value, int> = 0>
    const Item& constSubscriptChain(First&& first, Second&& second, Rest&&... rest) const
    {
        const Item& next = stepSubscriptConst(std::forward<First>(first));
        return next.constSubscriptChain(std::forward<Second>(second), std::forward<Rest>(rest)...);
    }

    ItemKind m_kind = ItemKind::Empty;
    Storage m_storage;
    Decor m_decor{};
    std::string m_rawRepr;
    StringStyle m_stringStyle = StringStyle::Basic;
    IntegerRadix m_intRadix = IntegerRadix::Decimal;
    Key* m_pOwningKey = nullptr;
};

inline bool operator!=(const Item& lhs, const Item& rhs) noexcept
{
    return !(lhs == rhs);
}

namespace detail
{
    template<typename T>
    struct IsOptionalValueTrait : std::false_type
    { };

    template<typename U>
    struct IsOptionalValueTrait<Optional<U>> : std::true_type
    { };

    struct TryGetTagItem {};
    struct TryGetTagBool {};
    struct TryGetTagIntegral {};
    struct TryGetTagFloat {};
    struct TryGetTagString {};
    struct TryGetTagStringView {};
    struct TryGetTagLocalDate {};
    struct TryGetTagLocalTime {};
    struct TryGetTagLocalDateTime {};
    struct TryGetTagOffsetDateTime {};
    struct TryGetTagCustom {};
    struct TryGetTagSequence {};
    struct TryGetTagMap {};
    struct TryGetTagUnsupported {};

    template<typename T, typename = void>
    struct HasPushBack : std::false_type
    { };

    template<typename T>
    struct HasPushBack<T, void_t<
        typename T::value_type,
        decltype(std::declval<T&>().push_back(std::declval<typename T::value_type>()))
    >> : std::integral_constant<bool, std::is_default_constructible<T>::value>
    { };

    template<typename T, typename = void>
    struct HasMapEmplace : std::false_type
    { };

    template<typename T>
    struct HasMapEmplace<T, void_t<
        typename T::key_type,
        typename T::mapped_type,
        decltype(std::declval<T&>().emplace(
            std::declval<typename T::key_type>(),
            std::declval<typename T::mapped_type>()
        ))
    >> : std::integral_constant<bool,
        std::is_default_constructible<T>::value
        && std::is_constructible<typename T::key_type, std::string>::value>
    { };

    template<typename CleanT>
    struct SelectTryGetTag
    {
        using type = conditional_t<std::is_same<CleanT, Item>::value, TryGetTagItem,
            conditional_t<std::is_same<CleanT, bool>::value, TryGetTagBool,
            conditional_t<std::is_integral<CleanT>::value && !std::is_same<CleanT, char>::value, TryGetTagIntegral,
            conditional_t<std::is_floating_point<CleanT>::value, TryGetTagFloat,
            conditional_t<std::is_same<CleanT, std::string>::value, TryGetTagString,
            conditional_t<std::is_same<CleanT, StringView>::value, TryGetTagStringView,
            conditional_t<std::is_same<CleanT, LocalDate>::value, TryGetTagLocalDate,
            conditional_t<std::is_same<CleanT, LocalTime>::value, TryGetTagLocalTime,
            conditional_t<std::is_same<CleanT, LocalDateTime>::value, TryGetTagLocalDateTime,
            conditional_t<std::is_same<CleanT, OffsetDateTime>::value, TryGetTagOffsetDateTime,
            conditional_t<IsCustomTomlFrom<CleanT>::value, TryGetTagCustom,
            conditional_t<HasPushBack<CleanT>::value, TryGetTagSequence,
            conditional_t<HasMapEmplace<CleanT>::value, TryGetTagMap,
            TryGetTagUnsupported>>>>>>>>>>>>>;
    };

    template<typename CleanT>
    inline enable_if_t<(sizeof(CleanT) >= sizeof(int64_t)), bool>
    fitsSignedInt64(int64_t, std::true_type /*isSigned*/) noexcept
    {
        return true;
    }

    template<typename CleanT>
    inline enable_if_t<(sizeof(CleanT) < sizeof(int64_t)), bool>
    fitsSignedInt64(int64_t v, std::true_type /*isSigned*/) noexcept
    {
        return v >= (int64_t)(std::numeric_limits<CleanT>::min)()
            && v <= (int64_t)(std::numeric_limits<CleanT>::max)();
    }

    template<typename CleanT>
    inline enable_if_t<(sizeof(CleanT) >= sizeof(uint64_t)), bool>
    fitsSignedInt64(int64_t v, std::false_type /*isSigned*/) noexcept
    {
        return v >= 0;
    }

    template<typename CleanT>
    inline enable_if_t<(sizeof(CleanT) < sizeof(uint64_t)), bool>
    fitsSignedInt64(int64_t v, std::false_type /*isSigned*/) noexcept
    {
        return v >= 0 && (uint64_t)v <= (uint64_t)(std::numeric_limits<CleanT>::max)();
    }

    template<typename T>
    struct TryGetHelper
    {
        using CleanT = remove_cvref_t<T>;

        static Optional<T> eval(const Item& item)
        {
            return evalTag(item, typename SelectTryGetTag<CleanT>::type());
        }

    private:
        static Optional<T> evalTag(const Item& item, TryGetTagItem)
        {
            return Optional<T>(item);
        }

        static Optional<T> evalTag(const Item& item, TryGetTagBool) noexcept
        {
            if (const bool* pVal = item.ifBoolean()) {
                return Optional<T>(*pVal);
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagIntegral) noexcept
        {
            const int64_t* pVal = item.ifInteger();
            if (pVal == nullptr) {
                return kNullopt;
            }
            if (!fitsSignedInt64<CleanT>(*pVal, std::is_signed<CleanT>())) {
                return kNullopt;
            }
            return Optional<T>((CleanT)(*pVal));
        }

        static Optional<T> evalTag(const Item& item, TryGetTagFloat) noexcept
        {
            if (const double* pVal = item.ifFloat()) {
                return Optional<T>((CleanT)(*pVal));
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagString)
        {
            if (const std::string* pVal = item.ifString()) {
                return Optional<T>(*pVal);
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagStringView) noexcept
        {
            if (const std::string* pVal = item.ifString()) {
                return Optional<T>(StringView(*pVal));
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagLocalDate) noexcept
        {
            if (const LocalDate* pVal = item.ifLocalDate()) {
                return Optional<T>(*pVal);
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagLocalTime) noexcept
        {
            if (const LocalTime* pVal = item.ifLocalTime()) {
                return Optional<T>(*pVal);
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagLocalDateTime) noexcept
        {
            if (const LocalDateTime* pVal = item.ifLocalDateTime()) {
                return Optional<T>(*pVal);
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagOffsetDateTime) noexcept
        {
            if (const OffsetDateTime* pVal = item.ifOffsetDateTime()) {
                return Optional<T>(*pVal);
            }
            return kNullopt;
        }

        static Optional<T> evalTag(const Item& item, TryGetTagCustom)
        {
#if TOMLKIT11_HAS_EXCEPTIONS
            try {
                return Optional<T>(Serializer<CleanT>::fromToml(item));
            }
            catch (...) {
                return kNullopt;
            }
#else
            return Optional<T>(Serializer<CleanT>::fromToml(item));
#endif
        }

        static Optional<T> evalTag(const Item& item, TryGetTagSequence)
        {
            const ArrayStorage* pArr = item.arrayStorage();
            if (pArr == nullptr) {
                return kNullopt;
            }
            CleanT out;
            for (size_t i = 0; i < pArr->elements.size(); ++i) {
                const Item* pElem = pArr->elements[i].pValue();
                if (pElem == nullptr) {
                    return kNullopt;
                }
                Optional<typename CleanT::value_type> elemOpt =
                    pElem->template tryGet<typename CleanT::value_type>();
                if (!elemOpt.has_value()) {
                    return kNullopt;
                }
                out.push_back(std::move(*elemOpt));
            }
            return Optional<T>(std::move(out));
        }

        static Optional<T> evalTag(const Item& item, TryGetTagMap)
        {
            const TableStorage* pTbl = item.tableStorage();
            if (pTbl == nullptr) {
                return kNullopt;
            }
            CleanT out;
            for (size_t i = 0; i < pTbl->entries.size(); ++i) {
                const TableEntry* pEntry = pTbl->entries[i].get();
                if (pEntry == nullptr || pEntry->value == nullptr) {
                    return kNullopt;
                }
                Optional<typename CleanT::mapped_type> valOpt =
                    pEntry->value->template tryGet<typename CleanT::mapped_type>();
                if (!valOpt.has_value()) {
                    return kNullopt;
                }
                out.emplace(typename CleanT::key_type(pEntry->key.name()), std::move(*valOpt));
            }
            return Optional<T>(std::move(out));
        }

        static Optional<T> evalTag(const Item&, TryGetTagUnsupported)
        {
            static_assert(AlwaysFalse<T>::value, "TOMLKIT11_UNSUPPORTED_GET_TYPE");
            return kNullopt;
        }
    };
} // namespace detail

template<typename T>
Optional<T> Item::tryGet() const
{
    return detail::TryGetHelper<T>::eval(*this);
}

#if TOMLKIT11_HAS_EXCEPTIONS
template<typename T>
T Item::get() const
{
    Optional<T> opt = tryGet<T>();
    if (opt.has_value()) {
        return std::move(*opt);
    }
    detail::throwOrAbortCannotConvertKind(kindName());
}

template<typename T>
void Item::getTo(T& dst) const
{
    dst = get<T>();
}
#endif // TOMLKIT11_HAS_EXCEPTIONS

namespace detail
{
    template<typename Step, enable_if_t<IsPathKeyLike<Step>::value, int> = 0>
    inline const Item* findChildNoexcept(const Item& current, Step&& step) noexcept
    {
        return current.find(StringView(step));
    }

    template<typename Step, enable_if_t<IsPathIndexLike<Step>::value, int> = 0>
    inline const Item* findChildNoexcept(const Item& current, Step&& step) noexcept
    {
        if (isNegativeIndexValue(step)) {
            return nullptr;
        }
        return current.find((size_t)step);
    }

    template<typename LastStep, enable_if_t<IsPathStep<LastStep>::value, int> = 0>
    inline const Item* walkPathNoexcept(const Item& current, LastStep&& last) noexcept
    {
        return findChildNoexcept(current, std::forward<LastStep>(last));
    }

    template<typename FirstStep, typename SecondStep, typename... RestSteps, enable_if_t<
        AllPathSteps<FirstStep, SecondStep, RestSteps...>::value, int> = 0>
    inline const Item* walkPathNoexcept(
        const Item& current,
        FirstStep&& first,
        SecondStep&& second,
        RestSteps&&... rest
    ) noexcept
    {
        const Item* pNext = findChildNoexcept(current, std::forward<FirstStep>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return walkPathNoexcept(
            *pNext,
            std::forward<SecondStep>(second),
            std::forward<RestSteps>(rest)...
        );
    }

#if TOMLKIT11_HAS_EXCEPTIONS
    template<typename Step, enable_if_t<IsPathKeyLike<Step>::value, int> = 0>
    inline const Item* findChildStrictContainer(const Item& current, Step&& step)
    {
        const StringView keySv(step);
        if (!current.isTable()) {
            throwOrAbortCannotLookupKeyOnKind(keySv, current.kindName());
        }
        return current.find(keySv);
    }

    template<typename Step, enable_if_t<IsPathIndexLike<Step>::value, int> = 0>
    inline const Item* findChildStrictContainer(const Item& current, Step&& step)
    {
        if (!current.isArray()) {
            throwOrAbortCannotIndexOnKind(current.kindName());
        }
        if (isNegativeIndexValue(step)) {
            return nullptr;
        }
        return current.find((size_t)step);
    }

    template<typename LastStep, enable_if_t<IsPathStep<LastStep>::value, int> = 0>
    inline const Item* walkPathStrictContainer(const Item& current, LastStep&& last)
    {
        return findChildStrictContainer(current, std::forward<LastStep>(last));
    }

    template<typename FirstStep, typename SecondStep, typename... RestSteps, enable_if_t<
        AllPathSteps<FirstStep, SecondStep, RestSteps...>::value, int> = 0>
    inline const Item* walkPathStrictContainer(
        const Item& current,
        FirstStep&& first,
        SecondStep&& second,
        RestSteps&&... rest
    )
    {
        const Item* pNext = findChildStrictContainer(current, std::forward<FirstStep>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return walkPathStrictContainer(
            *pNext,
            std::forward<SecondStep>(second),
            std::forward<RestSteps>(rest)...
        );
    }

    template<typename Step, enable_if_t<IsPathKeyLike<Step>::value, int> = 0>
    inline const Item& stepAtConst(const Item& current, Step&& step)
    {
        return current.at(StringView(step));
    }

    template<typename Step, enable_if_t<IsPathIndexLike<Step>::value, int> = 0>
    inline const Item& stepAtConst(const Item& current, Step&& step)
    {
        if (isNegativeIndexValue(step)) {
            throwOrAbortNegativeArrayIndex((int64_t)step);
        }
        return current.at((size_t)step);
    }
#endif // TOMLKIT11_HAS_EXCEPTIONS

    template<typename... Args>
    struct LastType;

    template<typename T>
    struct LastType<T>
    {
        using type = T;
    };

    template<typename Head, typename... Tail>
    struct LastType<Head, Tail...>
    {
        using type = typename LastType<Tail...>::type;
    };

    template<typename... Args>
    using LastTypeT = typename LastType<Args...>::type;

    template<typename... Args>
    struct FindOrValidArgs : std::false_type
    { };

    template<typename LastStep, typename FallbackT>
    struct FindOrValidArgs<LastStep, FallbackT> : IsPathStep<LastStep>
    { };

    template<typename FirstStep, typename Second, typename Third, typename... Rest>
    struct FindOrValidArgs<FirstStep, Second, Third, Rest...>
        : std::integral_constant<bool,
            IsPathStep<FirstStep>::value && FindOrValidArgs<Second, Third, Rest...>::value>
    { };

    template<typename... Args>
    struct FindOrHelper;

    template<typename LastStep, typename FallbackT>
    struct FindOrHelper<LastStep, FallbackT>
    {
        static auto eval(const Item& current, LastStep&& step, FallbackT&& fallback)
            -> NormalizedFallbackType<FallbackT>
        {
            using OutT = NormalizedFallbackType<FallbackT>;
            const Item* pFound = findChildNoexcept(current, std::forward<LastStep>(step));
            if (pFound != nullptr) {
                Optional<OutT> opt = pFound->template tryGet<OutT>();
                if (opt.has_value()) {
                    return std::move(*opt);
                }
            }
            return OutT(std::forward<FallbackT>(fallback));
        }
    };

    template<typename FirstStep, typename Second, typename Third, typename... Rest>
    struct FindOrHelper<FirstStep, Second, Third, Rest...>
    {
        static auto eval(
            const Item& current,
            FirstStep&& first,
            Second&& second,
            Third&& third,
            Rest&&... rest
        ) -> NormalizedFallbackType<LastTypeT<Third, Rest...>>
        {
            const Item* pNext = findChildNoexcept(current, std::forward<FirstStep>(first));
            if (pNext == nullptr) {
                return extractFallback(
                    std::forward<Second>(second),
                    std::forward<Third>(third),
                    std::forward<Rest>(rest)...
                );
            }
            return FindOrHelper<Second, Third, Rest...>::eval(
                *pNext,
                std::forward<Second>(second),
                std::forward<Third>(third),
                std::forward<Rest>(rest)...
            );
        }

    private:
        template<typename Last>
        static auto extractFallback(Last&& last) -> NormalizedFallbackType<Last>
        {
            return NormalizedFallbackType<Last>(std::forward<Last>(last));
        }

        template<typename Head, typename Next, typename... Tail>
        static auto extractFallback(Head&&, Next&& next, Tail&&... tail)
            -> NormalizedFallbackType<LastTypeT<Next, Tail...>>
        {
            return extractFallback(std::forward<Next>(next), std::forward<Tail>(tail)...);
        }
    };
} // namespace detail

#if TOMLKIT11_HAS_EXCEPTIONS
// 1. Required strongly-typed lookup: throws OutOfRangeError on missing path, TypeError on mismatch.
template<typename T, typename Step, detail::enable_if_t<detail::IsPathStep<Step>::value, int> = 0>
inline T find(const Item& root, Step&& step)
{
    static_assert(
        !detail::IsOptionalValueTrait<detail::remove_cvref_t<T>>::value,
        "Use tomlkit::findOptional<T>(...) (throws on type mismatch) or tomlkit::tryFind<T>(...) (noexcept) instead of find<Optional<T>>"
    );
    return detail::stepAtConst(root, std::forward<Step>(step)).template get<T>();
}

template<typename T, typename First, typename Second, typename... Rest, detail::enable_if_t<
    detail::AllPathSteps<First, Second, Rest...>::value, int> = 0>
inline T find(const Item& root, First&& first, Second&& second, Rest&&... rest)
{
    static_assert(
        !detail::IsOptionalValueTrait<detail::remove_cvref_t<T>>::value,
        "Use tomlkit::findOptional<T>(...) (throws on type mismatch) or tomlkit::tryFind<T>(...) (noexcept) instead of find<Optional<T>>"
    );
    return root.at(
        std::forward<First>(first),
        std::forward<Second>(second),
        std::forward<Rest>(rest)...
    ).template get<T>();
}

// 2. Optional strongly-typed lookup: returns kNullopt on missing path, throws TypeError on type mismatch.
template<typename T, typename... Steps, detail::enable_if_t<
    (sizeof...(Steps) >= 1) && detail::AllPathSteps<Steps...>::value, int> = 0>
inline Optional<T> findOptional(const Item& root, Steps&&... steps)
{
    static_assert(
        !detail::IsOptionalValueTrait<detail::remove_cvref_t<T>>::value,
        "Pass the inner value type T directly to tomlkit::findOptional<T>(...)"
    );
    if (root.isEmpty()) {
        return kNullopt;
    }
    const Item* pFound = detail::walkPathStrictContainer(root, std::forward<Steps>(steps)...);
    if (pFound == nullptr) {
        return kNullopt;
    }
    return Optional<T>(pFound->template get<T>());
}
#endif // TOMLKIT11_HAS_EXCEPTIONS

// 3. Non-throwing probing lookup: missing path or type mismatch both return kNullopt (noexcept).
template<typename T, typename... Steps, detail::enable_if_t<
    (sizeof...(Steps) >= 1) && detail::AllPathSteps<Steps...>::value, int> = 0>
inline Optional<T> tryFind(const Item& root, Steps&&... steps) noexcept
{
    static_assert(
        !detail::IsOptionalValueTrait<detail::remove_cvref_t<T>>::value,
        "Pass the inner value type T directly to tomlkit::tryFind<T>(...)"
    );
    const Item* pFound = detail::walkPathNoexcept(root, std::forward<Steps>(steps)...);
    if (pFound == nullptr) {
        return kNullopt;
    }
#if TOMLKIT11_HAS_EXCEPTIONS
    try {
        return pFound->template tryGet<T>();
    }
    catch (...) {
        return kNullopt;
    }
#else
    return pFound->template tryGet<T>();
#endif
}

// 4. Forgiving fallback lookup: tomlkit::findOr(doc, "GUIConfig", "apiLayoutMode", "default")
template<typename... Args, detail::enable_if_t<detail::FindOrValidArgs<Args...>::value, int> = 0>
inline auto findOr(const Item& root, Args&&... args)
    -> detail::NormalizedFallbackType<detail::LastTypeT<Args...>>
{
    return detail::FindOrHelper<Args...>::eval(root, std::forward<Args>(args)...);
}

// 5. Forgiving lookup returning default-constructed T / empty Array / empty Table.
template<typename T, typename... Steps, detail::enable_if_t<
    (sizeof...(Steps) >= 1)
    && detail::AllPathSteps<Steps...>::value
    && std::is_default_constructible<T>::value, int> = 0>
inline T findOrDefault(const Item& root, Steps&&... steps)
{
    Optional<T> opt = tryFind<T>(root, std::forward<Steps>(steps)...);
    if (opt.has_value()) {
        return std::move(*opt);
    }
    return T();
}

template<typename... Steps, detail::enable_if_t<
    (sizeof...(Steps) >= 1) && detail::AllPathSteps<Steps...>::value, int> = 0>
inline Item findArrayOrEmpty(const Item& root, Steps&&... steps)
{
    const Item* pFound = detail::walkPathNoexcept(root, std::forward<Steps>(steps)...);
    if (pFound != nullptr && pFound->isArray()) {
        return *pFound;
    }
    return Item::array();
}

template<typename... Steps, detail::enable_if_t<
    (sizeof...(Steps) >= 1) && detail::AllPathSteps<Steps...>::value, int> = 0>
inline Item findTableOrEmpty(const Item& root, Steps&&... steps)
{
    const Item* pFound = detail::walkPathNoexcept(root, std::forward<Steps>(steps)...);
    if (pFound != nullptr && pFound->isTable()) {
        return *pFound;
    }
    return Item::table();
}

TOMLKIT11_NAMESPACE_END

namespace std
{
    template<bool IsConst>
    struct tuple_size<tomlkit::TableKeyValuePair<IsConst>>
        : std::integral_constant<std::size_t, 2>
    { };

    template<bool IsConst>
    struct tuple_element<0, tomlkit::TableKeyValuePair<IsConst>>
    {
        using type = const tomlkit::Key&;
    };

    template<bool IsConst>
    struct tuple_element<1, tomlkit::TableKeyValuePair<IsConst>>
    {
        using type = typename tomlkit::TableKeyValuePair<IsConst>::ItemRef;
    };
} // namespace std

#endif // TOMLKIT11_ITEM_HPP
