#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

namespace detail
{
    inline uint64_t hashStringView(StringView sv) noexcept
    {
        uint64_t h = 14695981039346656037ULL;
        for (size_t i = 0; i < sv.size(); ++i) {
            h ^= (uint64_t)(uint8_t)sv[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    inline size_t nextPowerOfTwo(size_t n) noexcept
    {
        size_t p = 1;
        const size_t kMaxHalf = ((size_t)-1) / 2;
        while (p < n && p <= kMaxHalf) {
            p <<= 1;
        }
        return p;
    }

    TOMLKIT11_INLINE void throwOrAbortNegativeArrayIndex(int64_t index)
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw OutOfRangeError("Negative array index " + intToString(index));
#else
        (void)index;
        TOMLKIT11_ABORT();
#endif
    }

    TOMLKIT11_INLINE void throwOrAbortTypeError(StringView msg)
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw TypeError(toString(msg));
#else
        (void)msg;
        TOMLKIT11_ABORT();
#endif
    }

    TOMLKIT11_INLINE void throwOrAbortOutOfRange(StringView msg)
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw OutOfRangeError(toString(msg));
#else
        (void)msg;
        TOMLKIT11_ABORT();
#endif
    }

    TOMLKIT11_INLINE void throwOrAbortCannotConvertKind(StringView kindName)
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw TypeError("Cannot convert TOML item of kind '" + toString(kindName) + "' to requested C++ type");
#else
        (void)kindName;
        TOMLKIT11_ABORT();
#endif
    }

    TOMLKIT11_INLINE void throwOrAbortCannotLookupKeyOnKind(StringView key, StringView kindName)
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw TypeError(
            "Cannot lookup key '" + toString(key) + "' on non-table TOML item of kind '" + toString(kindName) + "'"
        );
#else
        (void)key;
        (void)kindName;
        TOMLKIT11_ABORT();
#endif
    }

    TOMLKIT11_INLINE void throwOrAbortCannotIndexOnKind(StringView kindName)
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw TypeError("Cannot index on non-array TOML item of kind '" + toString(kindName) + "'");
#else
        (void)kindName;
        TOMLKIT11_ABORT();
#endif
    }

    TOMLKIT11_INLINE void throwOrAbortUnsignedOverflow()
    {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw OutOfRangeError("Unsigned integer exceeds TOML 64-bit signed integer maximum");
#else
        TOMLKIT11_ABORT();
#endif
    }

    inline uint32_t extractSectionAnchor(const Item& item) noexcept
    {
        if (const TableStorage* pTbl = item.tableStorage()) {
            if (pTbl->style != TableStyle::Inline) {
                return pTbl->sectionOrder;
            }
        }
        else if (const ArrayStorage* pArr = item.arrayStorage()) {
            if (pArr->style == ArrayStyle::ArrayOfTables) {
                for (size_t i = 0; i < pArr->elements.size(); ++i) {
                    const Item* pElem = pArr->elements[i].pValue();
                    if (pElem != nullptr) {
                        if (const TableStorage* pElemTbl = pElem->tableStorage()) {
                            if (pElemTbl->sectionOrder > 0) {
                                return pElemTbl->sectionOrder;
                            }
                        }
                    }
                }
            }
        }
        return 0;
    }

    inline bool isPureWhitespace(StringView text) noexcept
    {
        for (size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                return false;
            }
        }
        return !text.empty();
    }

    inline std::string extractLeadingSectionBlankPrefix(const Item& item)
    {
        if (const TableStorage* pTbl = item.tableStorage()) {
            if (pTbl->style != TableStyle::Inline && isPureWhitespace(StringView(pTbl->headerDecor.prefix))) {
                return pTbl->headerDecor.prefix;
            }
        }
        else if (const ArrayStorage* pArr = item.arrayStorage()) {
            if (pArr->style == ArrayStyle::ArrayOfTables && !pArr->elements.empty()) {
                const Item* pFront = pArr->elements.front().pValue();
                if (pFront != nullptr) {
                    if (const TableStorage* pElemTbl = pFront->tableStorage()) {
                        if (isPureWhitespace(StringView(pElemTbl->headerDecor.prefix))) {
                            return pElemTbl->headerDecor.prefix;
                        }
                    }
                }
            }
        }
        return std::string();
    }

    inline void resetSubtreeSectionOrder(Item& item) noexcept
    {
        if (TableStorage* pTbl = item.tableStorage()) {
            pTbl->sectionOrder = 0;
            for (size_t i = 0; i < pTbl->entries.size(); ++i) {
                if (pTbl->entries[i]->value != nullptr) {
                    resetSubtreeSectionOrder(*pTbl->entries[i]->value);
                }
            }
        }
        else if (ArrayStorage* pArr = item.arrayStorage()) {
            for (size_t i = 0; i < pArr->elements.size(); ++i) {
                if (pArr->elements[i].value != nullptr) {
                    resetSubtreeSectionOrder(*pArr->elements[i].value);
                }
            }
        }
    }

    inline void applyImmediateSectionAnchor(
        Item& item,
        uint32_t anchor,
        StringView fallbackBlankPrefix
    )
    {
        if (TableStorage* pTbl = item.tableStorage()) {
            if (pTbl->style != TableStyle::Inline) {
                if (anchor > 0) {
                    pTbl->sectionOrder = anchor;
                }
                if (pTbl->headerDecor.prefix.empty() && !fallbackBlankPrefix.empty()) {
                    pTbl->headerDecor.prefix = toString(fallbackBlankPrefix);
                }
            }
        }
        else if (ArrayStorage* pArr = item.arrayStorage()) {
            if (pArr->style == ArrayStyle::ArrayOfTables) {
                for (size_t i = 0; i < pArr->elements.size(); ++i) {
                    if (pArr->elements[i].value == nullptr) {
                        continue;
                    }
                    if (TableStorage* pElemTbl = pArr->elements[i].value->tableStorage()) {
                        if (anchor > 0) {
                            pElemTbl->sectionOrder = anchor;
                        }
                        if (i == 0 && pElemTbl->headerDecor.prefix.empty() && !fallbackBlankPrefix.empty()) {
                            pElemTbl->headerDecor.prefix = toString(fallbackBlankPrefix);
                        }
                    }
                }
            }
        }
    }
} // namespace detail

// ==================== ArrayElement ====================

TOMLKIT11_INLINE Item* ArrayElement::pValue() noexcept
{
    return value.get();
}

TOMLKIT11_INLINE const Item* ArrayElement::pValue() const noexcept
{
    return value.get();
}

TOMLKIT11_INLINE ArrayElement::ArrayElement()
    : value(detail::makeUnique<Item>())
{ }

TOMLKIT11_INLINE ArrayElement::ArrayElement(Item val)
    : value(detail::makeUnique<Item>(std::move(val)))
{ }

TOMLKIT11_INLINE ArrayElement::ArrayElement(const ArrayElement& other)
    : value(other.value ? detail::makeUnique<Item>(*other.value) : detail::makeUnique<Item>())
    , beforeComma(other.beforeComma)
    , afterComma(other.afterComma)
    , hasComma(other.hasComma)
{ }

TOMLKIT11_INLINE ArrayElement::ArrayElement(ArrayElement&& other) noexcept
    : value(std::move(other.value))
    , beforeComma(std::move(other.beforeComma))
    , afterComma(std::move(other.afterComma))
    , hasComma(other.hasComma)
{
    other.hasComma = false;
}

TOMLKIT11_INLINE ArrayElement& ArrayElement::operator=(const ArrayElement& other)
{
    if (this != &other) {
        value = other.value ? detail::makeUnique<Item>(*other.value) : detail::makeUnique<Item>();
        beforeComma = other.beforeComma;
        afterComma = other.afterComma;
        hasComma = other.hasComma;
    }
    return *this;
}

TOMLKIT11_INLINE ArrayElement& ArrayElement::operator=(ArrayElement&& other) noexcept
{
    if (this != &other) {
        value = std::move(other.value);
        beforeComma = std::move(other.beforeComma);
        afterComma = std::move(other.afterComma);
        hasComma = other.hasComma;
        other.hasComma = false;
    }
    return *this;
}

TOMLKIT11_INLINE ArrayElement::~ArrayElement() = default;

// ==================== ArrayStorage ====================

TOMLKIT11_INLINE ArrayStorage::ArrayStorage() = default;

TOMLKIT11_INLINE ArrayStorage::ArrayStorage(const ArrayStorage& other)
    : style(other.style)
    , multiline(other.multiline)
    , hasExplicitLayout(other.hasExplicitLayout)
    , elements(other.elements)
    , trailing(other.trailing)
{ }

TOMLKIT11_INLINE ArrayStorage::ArrayStorage(ArrayStorage&& other) noexcept
    : style(other.style)
    , multiline(other.multiline)
    , hasExplicitLayout(other.hasExplicitLayout)
    , elements(std::move(other.elements))
    , trailing(std::move(other.trailing))
{ }

TOMLKIT11_INLINE ArrayStorage& ArrayStorage::operator=(const ArrayStorage& other)
{
    if (this != &other) {
        style = other.style;
        multiline = other.multiline;
        hasExplicitLayout = other.hasExplicitLayout;
        elements = other.elements;
        trailing = other.trailing;
    }
    return *this;
}

TOMLKIT11_INLINE ArrayStorage& ArrayStorage::operator=(ArrayStorage&& other) noexcept
{
    if (this != &other) {
        style = other.style;
        multiline = other.multiline;
        hasExplicitLayout = other.hasExplicitLayout;
        elements = std::move(other.elements);
        trailing = std::move(other.trailing);
    }
    return *this;
}

TOMLKIT11_INLINE ArrayStorage::~ArrayStorage() = default;

// ==================== TableEntry ====================

TOMLKIT11_INLINE TableEntry::TableEntry(Key k, Item v, uint32_t order)
    : key(std::move(k))
    , value(detail::makeUnique<Item>(std::move(v)))
    , entryOrder(order)
{
    relinkOwner();
}

TOMLKIT11_INLINE TableEntry::TableEntry(const TableEntry& other)
    : key(other.key)
    , value(other.value ? detail::makeUnique<Item>(*other.value) : detail::makeUnique<Item>())
    , entryOrder(other.entryOrder)
    , beforeComma(other.beforeComma)
    , afterComma(other.afterComma)
    , hasComma(other.hasComma)
{
    relinkOwner();
}

TOMLKIT11_INLINE TableEntry::TableEntry(TableEntry&& other) noexcept
    : key(std::move(other.key))
    , value(std::move(other.value))
    , entryOrder(other.entryOrder)
    , beforeComma(std::move(other.beforeComma))
    , afterComma(std::move(other.afterComma))
    , hasComma(other.hasComma)
{
    other.hasComma = false;
    relinkOwner();
}

TOMLKIT11_INLINE TableEntry& TableEntry::operator=(const TableEntry& other)
{
    if (this != &other) {
        key = other.key;
        value = other.value ? detail::makeUnique<Item>(*other.value) : detail::makeUnique<Item>();
        entryOrder = other.entryOrder;
        beforeComma = other.beforeComma;
        afterComma = other.afterComma;
        hasComma = other.hasComma;
        relinkOwner();
    }
    return *this;
}

TOMLKIT11_INLINE TableEntry& TableEntry::operator=(TableEntry&& other) noexcept
{
    if (this != &other) {
        key = std::move(other.key);
        value = std::move(other.value);
        entryOrder = other.entryOrder;
        beforeComma = std::move(other.beforeComma);
        afterComma = std::move(other.afterComma);
        hasComma = other.hasComma;
        other.hasComma = false;
        relinkOwner();
    }
    return *this;
}

TOMLKIT11_INLINE void TableEntry::relinkOwner() noexcept
{
    if (value != nullptr) {
        value->setOwningKey(&key);
    }
}

TOMLKIT11_INLINE TableEntry::~TableEntry() = default;

// ==================== TableStorage ====================

TOMLKIT11_INLINE TableStorage::TableStorage() = default;

TOMLKIT11_INLINE TableStorage::TableStorage(TableStyle tblStyle)
    : style(tblStyle)
{ }

TOMLKIT11_INLINE TableStorage::TableStorage(const TableStorage& other)
    : style(other.style)
    , hasBom(other.hasBom)
    , multilineInline(other.multilineInline)
    , isSealed(other.isSealed)
    , isExplicitlyDefined(other.isExplicitlyDefined)
    , dottedKeyScopeId(other.dottedKeyScopeId)
    , sectionOrder(other.sectionOrder)
    , nextEntryOrder(other.nextEntryOrder)
    , headerDecor(other.headerDecor)
    , headerKeyPath(other.headerKeyPath)
    , trailing(other.trailing)
    , hashDirty(true)
{
    entries.reserve(other.entries.size());
    for (size_t i = 0; i < other.entries.size(); ++i) {
        entries.push_back(detail::makeUnique<TableEntry>(*other.entries[i]));
    }
}

TOMLKIT11_INLINE TableStorage::TableStorage(TableStorage&& other) noexcept
    : style(other.style)
    , hasBom(other.hasBom)
    , multilineInline(other.multilineInline)
    , isSealed(other.isSealed)
    , isExplicitlyDefined(other.isExplicitlyDefined)
    , dottedKeyScopeId(other.dottedKeyScopeId)
    , sectionOrder(other.sectionOrder)
    , nextEntryOrder(other.nextEntryOrder)
    , headerDecor(std::move(other.headerDecor))
    , headerKeyPath(std::move(other.headerKeyPath))
    , entries(std::move(other.entries))
    , trailing(std::move(other.trailing))
    , hashSlots(std::move(other.hashSlots))
    , hashDirty(other.hashDirty)
{
    other.hashDirty = true;
}

TOMLKIT11_INLINE TableStorage& TableStorage::operator=(const TableStorage& other)
{
    if (this != &other) {
        TableStorage copy(other);
        *this = std::move(copy);
    }
    return *this;
}

TOMLKIT11_INLINE TableStorage& TableStorage::operator=(TableStorage&& other) noexcept
{
    if (this != &other) {
        style = other.style;
        hasBom = other.hasBom;
        multilineInline = other.multilineInline;
        isSealed = other.isSealed;
        isExplicitlyDefined = other.isExplicitlyDefined;
        dottedKeyScopeId = other.dottedKeyScopeId;
        sectionOrder = other.sectionOrder;
        nextEntryOrder = other.nextEntryOrder;
        headerDecor = std::move(other.headerDecor);
        headerKeyPath = std::move(other.headerKeyPath);
        entries = std::move(other.entries);
        trailing = std::move(other.trailing);
        hashSlots = std::move(other.hashSlots);
        hashDirty = other.hashDirty;
        other.hashDirty = true;
    }
    return *this;
}

TOMLKIT11_INLINE TableStorage::~TableStorage() = default;

TOMLKIT11_INLINE void TableStorage::rebuildIndexIfNeeded() const
{
    if (!hashDirty) {
        return;
    }
    hashDirty = false;
    if (entries.size() <= kHashIndexThreshold) {
        hashSlots.clear();
        return;
    }

    const size_t slotCount = detail::nextPowerOfTwo(entries.size() * 2);
    hashSlots.assign(slotCount, 0);
    const size_t mask = slotCount - 1;

    for (size_t i = 0; i < entries.size(); ++i) {
        size_t slot = (size_t)detail::hashStringView(StringView(entries[i]->key.name())) & mask;
        while (hashSlots[slot] != 0) {
            slot = (slot + 1) & mask;
        }
        hashSlots[slot] = i + 1;
    }
}

TOMLKIT11_INLINE size_t TableStorage::findIndex(StringView keyName) const noexcept
{
    if (entries.size() <= kHashIndexThreshold) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (StringView(entries[i]->key.name()) == keyName) {
                return i;
            }
        }
        return StringView::npos;
    }

    rebuildIndexIfNeeded();
    const size_t mask = hashSlots.size() - 1;
    size_t slot = (size_t)detail::hashStringView(keyName) & mask;
    while (hashSlots[slot] != 0) {
        const size_t idx = hashSlots[slot] - 1;
        if (StringView(entries[idx]->key.name()) == keyName) {
            return idx;
        }
        slot = (slot + 1) & mask;
    }
    return StringView::npos;
}

// ==================== Item Payload Management ====================

TOMLKIT11_INLINE void Item::destroyPayload() noexcept
{
    switch (m_kind)
    {
    case ItemKind::Empty:
    case ItemKind::Boolean:
    case ItemKind::Integer:
    case ItemKind::Float:
    case ItemKind::LocalDate:
    case ItemKind::LocalTime:
    case ItemKind::LocalDateTime:
    case ItemKind::OffsetDateTime:
        break;
    case ItemKind::String:
        detail::destroyAt(&m_storage.m_string);
        break;
    case ItemKind::Array:
        detail::destroyAt(&m_storage.m_array);
        break;
    case ItemKind::Table:
        detail::destroyAt(&m_storage.m_table);
        break;
    }
    m_kind = ItemKind::Empty;
}

TOMLKIT11_INLINE void Item::copyPayloadFrom(const Item& other)
{
    switch (other.m_kind)
    {
    case ItemKind::Empty:
        m_kind = ItemKind::Empty;
        break;
    case ItemKind::Boolean:
        m_storage.m_bool = other.m_storage.m_bool;
        m_kind = ItemKind::Boolean;
        break;
    case ItemKind::Integer:
        m_storage.m_int = other.m_storage.m_int;
        m_kind = ItemKind::Integer;
        break;
    case ItemKind::Float:
        m_storage.m_float = other.m_storage.m_float;
        m_kind = ItemKind::Float;
        break;
    case ItemKind::String:
        new (&m_storage.m_string) std::string(other.m_storage.m_string);
        m_kind = ItemKind::String;
        break;
    case ItemKind::LocalDate:
        m_storage.m_localDate = other.m_storage.m_localDate;
        m_kind = ItemKind::LocalDate;
        break;
    case ItemKind::LocalTime:
        m_storage.m_localTime = other.m_storage.m_localTime;
        m_kind = ItemKind::LocalTime;
        break;
    case ItemKind::LocalDateTime:
        m_storage.m_localDateTime = other.m_storage.m_localDateTime;
        m_kind = ItemKind::LocalDateTime;
        break;
    case ItemKind::OffsetDateTime:
        m_storage.m_offsetDateTime = other.m_storage.m_offsetDateTime;
        m_kind = ItemKind::OffsetDateTime;
        break;
    case ItemKind::Array:
    {
        std::unique_ptr<ArrayStorage> pArrCopy = other.m_storage.m_array
            ? detail::makeUnique<ArrayStorage>(*other.m_storage.m_array)
            : detail::makeUnique<ArrayStorage>();
        new (&m_storage.m_array) std::unique_ptr<ArrayStorage>(std::move(pArrCopy));
        m_kind = ItemKind::Array;
        break;
    }
    case ItemKind::Table:
    {
        std::unique_ptr<TableStorage> pTblCopy = other.m_storage.m_table
            ? detail::makeUnique<TableStorage>(*other.m_storage.m_table)
            : detail::makeUnique<TableStorage>();
        new (&m_storage.m_table) std::unique_ptr<TableStorage>(std::move(pTblCopy));
        m_kind = ItemKind::Table;
        break;
    }
    }
}

TOMLKIT11_INLINE void Item::movePayloadFrom(Item&& other) noexcept
{
    switch (other.m_kind)
    {
    case ItemKind::Empty:
        m_kind = ItemKind::Empty;
        break;
    case ItemKind::Boolean:
        m_storage.m_bool = other.m_storage.m_bool;
        m_kind = ItemKind::Boolean;
        break;
    case ItemKind::Integer:
        m_storage.m_int = other.m_storage.m_int;
        m_kind = ItemKind::Integer;
        break;
    case ItemKind::Float:
        m_storage.m_float = other.m_storage.m_float;
        m_kind = ItemKind::Float;
        break;
    case ItemKind::String:
        new (&m_storage.m_string) std::string(std::move(other.m_storage.m_string));
        m_kind = ItemKind::String;
        break;
    case ItemKind::LocalDate:
        m_storage.m_localDate = other.m_storage.m_localDate;
        m_kind = ItemKind::LocalDate;
        break;
    case ItemKind::LocalTime:
        m_storage.m_localTime = other.m_storage.m_localTime;
        m_kind = ItemKind::LocalTime;
        break;
    case ItemKind::LocalDateTime:
        m_storage.m_localDateTime = other.m_storage.m_localDateTime;
        m_kind = ItemKind::LocalDateTime;
        break;
    case ItemKind::OffsetDateTime:
        m_storage.m_offsetDateTime = other.m_storage.m_offsetDateTime;
        m_kind = ItemKind::OffsetDateTime;
        break;
    case ItemKind::Array:
        new (&m_storage.m_array) std::unique_ptr<ArrayStorage>(std::move(other.m_storage.m_array));
        m_kind = ItemKind::Array;
        break;
    case ItemKind::Table:
        new (&m_storage.m_table) std::unique_ptr<TableStorage>(std::move(other.m_storage.m_table));
        m_kind = ItemKind::Table;
        break;
    }
    other.destroyPayload();
}

TOMLKIT11_INLINE void Item::setArrayPayload(std::unique_ptr<ArrayStorage> pArr) noexcept
{
    destroyPayload();
    new (&m_storage.m_array) std::unique_ptr<ArrayStorage>(std::move(pArr));
    m_kind = ItemKind::Array;
}

TOMLKIT11_INLINE void Item::setTablePayload(std::unique_ptr<TableStorage> pTbl) noexcept
{
    destroyPayload();
    new (&m_storage.m_table) std::unique_ptr<TableStorage>(std::move(pTbl));
    m_kind = ItemKind::Table;
}

// ==================== Item Core ====================

TOMLKIT11_INLINE Item::Item() noexcept
    : m_kind(ItemKind::Empty)
    , m_storage()
{ }

TOMLKIT11_INLINE Item::~Item()
{
    destroyPayload();
}

TOMLKIT11_INLINE Item::Item(const Item& other)
    : m_kind(ItemKind::Empty)
    , m_storage()
    , m_decor(other.m_decor)
    , m_rawRepr(other.m_rawRepr)
    , m_stringStyle(other.m_stringStyle)
    , m_intRadix(other.m_intRadix)
    , m_pOwningKey(nullptr)
{
    copyPayloadFrom(other);
}

TOMLKIT11_INLINE Item::Item(Item&& other) noexcept
    : m_kind(ItemKind::Empty)
    , m_storage()
    , m_decor(std::move(other.m_decor))
    , m_rawRepr(std::move(other.m_rawRepr))
    , m_stringStyle(other.m_stringStyle)
    , m_intRadix(other.m_intRadix)
    , m_pOwningKey(nullptr)
{
    movePayloadFrom(std::move(other));
}

TOMLKIT11_INLINE Item& Item::operator=(const Item& other)
{
    if (this != &other) {
        Item copy(other);
        assignPreservingDecor(std::move(copy));
    }
    return *this;
}

TOMLKIT11_INLINE Item& Item::operator=(Item&& other) noexcept
{
    if (this != &other) {
        assignPreservingDecor(std::move(other));
    }
    return *this;
}

TOMLKIT11_INLINE void Item::assignPreservingDecor(Item&& replacement)
{
    if (this == &replacement) {
        return;
    }

    Key* const pSavedOwner = m_pOwningKey;
    const bool lhsHasDecor = !m_decor.prefix.empty() || !m_decor.suffix.empty();
    const bool rhsHasDecor = !replacement.m_decor.prefix.empty() || !replacement.m_decor.suffix.empty();

    if (pSavedOwner != nullptr) {
        const uint32_t oldAnchor = detail::extractSectionAnchor(*this);
        const std::string oldBlankPrefix = detail::extractLeadingSectionBlankPrefix(*this);
        detail::resetSubtreeSectionOrder(replacement);
        detail::applyImmediateSectionAnchor(
            replacement,
            oldAnchor,
            StringView(oldBlankPrefix)
        );
    }

    destroyPayload();
    movePayloadFrom(std::move(replacement));
    m_rawRepr = std::move(replacement.m_rawRepr);
    m_stringStyle = replacement.m_stringStyle;
    m_intRadix = replacement.m_intRadix;
    m_pOwningKey = pSavedOwner;

    if (rhsHasDecor || !lhsHasDecor) {
        m_decor = std::move(replacement.m_decor);
    }
}

TOMLKIT11_INLINE Item::Item(bool val) noexcept
    : m_kind(ItemKind::Boolean)
    , m_storage()
{
    m_storage.m_bool = val;
}

TOMLKIT11_INLINE Item::Item(const char* pStr)
    : m_kind(ItemKind::Empty)
    , m_storage()
{
    new (&m_storage.m_string) std::string(pStr != nullptr ? pStr : "");
    m_kind = ItemKind::String;
}

TOMLKIT11_INLINE Item::Item(StringView sv)
    : m_kind(ItemKind::Empty)
    , m_storage()
{
    new (&m_storage.m_string) std::string(detail::toString(sv));
    m_kind = ItemKind::String;
}

TOMLKIT11_INLINE Item::Item(std::string str)
    : m_kind(ItemKind::Empty)
    , m_storage()
{
    new (&m_storage.m_string) std::string(std::move(str));
    m_kind = ItemKind::String;
}

TOMLKIT11_INLINE Item::Item(LocalDate val) noexcept
    : m_kind(ItemKind::LocalDate)
    , m_storage()
{
    m_storage.m_localDate = val;
}

TOMLKIT11_INLINE Item::Item(LocalTime val) noexcept
    : m_kind(ItemKind::LocalTime)
    , m_storage()
{
    m_storage.m_localTime = val;
}

TOMLKIT11_INLINE Item::Item(LocalDateTime val) noexcept
    : m_kind(ItemKind::LocalDateTime)
    , m_storage()
{
    m_storage.m_localDateTime = val;
}

TOMLKIT11_INLINE Item::Item(OffsetDateTime val) noexcept
    : m_kind(ItemKind::OffsetDateTime)
    , m_storage()
{
    m_storage.m_offsetDateTime = val;
}

TOMLKIT11_INLINE Item::Item(std::initializer_list<Item> arrayElements)
    : m_kind(ItemKind::Empty)
    , m_storage()
{
    std::unique_ptr<ArrayStorage> pArr = detail::makeUnique<ArrayStorage>();
    pArr->elements.reserve(arrayElements.size());
    for (std::initializer_list<Item>::const_iterator it = arrayElements.begin();
        it != arrayElements.end();
        ++it)
    {
        pArr->elements.emplace_back(*it);
    }
    setArrayPayload(std::move(pArr));
}

TOMLKIT11_INLINE Item Item::document()
{
    return table(TableStyle::DocumentRoot);
}

TOMLKIT11_INLINE Item Item::array()
{
    Item item;
    item.setArrayPayload(detail::makeUnique<ArrayStorage>());
    return item;
}

TOMLKIT11_INLINE Item Item::array(std::initializer_list<Item> init)
{
    return Item(init);
}

TOMLKIT11_INLINE Item Item::arrayOfTables()
{
    Item item;
    std::unique_ptr<ArrayStorage> pArr = detail::makeUnique<ArrayStorage>();
    pArr->style = ArrayStyle::ArrayOfTables;
    item.setArrayPayload(std::move(pArr));
    return item;
}

TOMLKIT11_INLINE Item Item::table(TableStyle style)
{
    Item item;
    item.setTablePayload(detail::makeUnique<TableStorage>(style));
    return item;
}

TOMLKIT11_INLINE Item Item::table(
    std::initializer_list<std::pair<StringView, Item>> init,
    TableStyle style
)
{
    Item item = table(style);
    for (std::initializer_list<std::pair<StringView, Item>>::const_iterator it = init.begin();
        it != init.end();
        ++it)
    {
        item[it->first] = it->second;
    }
    return item;
}

TOMLKIT11_INLINE Item Item::inlineTable()
{
    return table(TableStyle::Inline);
}

TOMLKIT11_INLINE Item Item::inlineTable(std::initializer_list<std::pair<StringView, Item>> init)
{
    return table(init, TableStyle::Inline);
}

TOMLKIT11_INLINE bool Item::isInlineTable() const noexcept
{
    if (const TableStorage* pTbl = tableStorage()) {
        return pTbl->style == TableStyle::Inline;
    }
    return false;
}

TOMLKIT11_INLINE bool Item::isArrayOfTables() const noexcept
{
    if (const ArrayStorage* pArr = arrayStorage()) {
        return pArr->style == ArrayStyle::ArrayOfTables;
    }
    return false;
}

TOMLKIT11_INLINE void Item::ensureKind(ItemKind expected) const
{
    if (m_kind != expected) {
        detail::throwOrAbortTypeError(
            "TOML type mismatch: expected '"
            + detail::toString(itemKindName(expected))
            + "', but actual kind is '"
            + detail::toString(kindName())
            + "'"
        );
    }
}

#if TOMLKIT11_HAS_EXCEPTIONS
TOMLKIT11_INLINE double Item::asNumber() const
{
    if (isFloat()) {
        return m_storage.m_float;
    }
    if (isInteger()) {
        return (double)m_storage.m_int;
    }
    detail::throwOrAbortTypeError(
        "TOML type mismatch: expected numeric kind, got '" + detail::toString(kindName()) + "'"
    );
}
#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_INLINE ArrayStorage* Item::arrayStorage() noexcept
{
    return (m_kind == ItemKind::Array) ? m_storage.m_array.get() : nullptr;
}

TOMLKIT11_INLINE const ArrayStorage* Item::arrayStorage() const noexcept
{
    return (m_kind == ItemKind::Array) ? m_storage.m_array.get() : nullptr;
}

TOMLKIT11_INLINE TableStorage* Item::tableStorage() noexcept
{
    return (m_kind == ItemKind::Table) ? m_storage.m_table.get() : nullptr;
}

TOMLKIT11_INLINE const TableStorage* Item::tableStorage() const noexcept
{
    return (m_kind == ItemKind::Table) ? m_storage.m_table.get() : nullptr;
}

TOMLKIT11_INLINE bool operator==(const Item& lhs, const Item& rhs) noexcept
{
    if (lhs.m_kind != rhs.m_kind) {
        return false;
    }

    switch (lhs.m_kind)
    {
    case ItemKind::Empty:
        return true;
    case ItemKind::Boolean:
        return lhs.m_storage.m_bool == rhs.m_storage.m_bool;
    case ItemKind::Integer:
        return lhs.m_storage.m_int == rhs.m_storage.m_int;
    case ItemKind::Float:
    {
        const double a = lhs.m_storage.m_float;
        const double b = rhs.m_storage.m_float;
        if (std::isnan(a) && std::isnan(b)) {
            return true;
        }
        return a == b;
    }
    case ItemKind::String:
        return lhs.m_storage.m_string == rhs.m_storage.m_string;
    case ItemKind::LocalDate:
        return lhs.m_storage.m_localDate == rhs.m_storage.m_localDate;
    case ItemKind::LocalTime:
        return lhs.m_storage.m_localTime == rhs.m_storage.m_localTime;
    case ItemKind::LocalDateTime:
        return lhs.m_storage.m_localDateTime == rhs.m_storage.m_localDateTime;
    case ItemKind::OffsetDateTime:
        return lhs.m_storage.m_offsetDateTime == rhs.m_storage.m_offsetDateTime;
    case ItemKind::Array:
    {
        const ArrayStorage* pA = lhs.arrayStorage();
        const ArrayStorage* pB = rhs.arrayStorage();
        if (pA == nullptr || pB == nullptr || pA->elements.size() != pB->elements.size()) {
            return false;
        }
        for (size_t i = 0; i < pA->elements.size(); ++i) {
            if (*pA->elements[i].value != *pB->elements[i].value) {
                return false;
            }
        }
        return true;
    }
    case ItemKind::Table:
    {
        const TableStorage* pA = lhs.tableStorage();
        const TableStorage* pB = rhs.tableStorage();
        if (pA == nullptr || pB == nullptr || pA->entries.size() != pB->entries.size()) {
            return false;
        }
        for (size_t i = 0; i < pA->entries.size(); ++i) {
            const TableEntry* pEntry = pA->entries[i].get();
            const Item* pOtherVal = rhs.find(StringView(pEntry->key.name()));
            if (pOtherVal == nullptr || *pEntry->value != *pOtherVal) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
