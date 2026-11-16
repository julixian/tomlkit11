#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

TOMLKIT11_INLINE size_t Item::size() const noexcept
{
    if (const ArrayStorage* pArr = arrayStorage()) {
        return pArr->elements.size();
    }
    if (const TableStorage* pTbl = tableStorage()) {
        return pTbl->entries.size();
    }
    if (isEmpty()) {
        return 0;
    }
    return 1;
}

TOMLKIT11_INLINE bool Item::empty() const noexcept
{
    if (const ArrayStorage* pArr = arrayStorage()) {
        return pArr->elements.empty();
    }
    if (const TableStorage* pTbl = tableStorage()) {
        return pTbl->entries.empty();
    }
    return isEmpty();
}

TOMLKIT11_INLINE void Item::clear()
{
    m_rawRepr.clear();
    if (ArrayStorage* pArr = arrayStorage()) {
        pArr->elements.clear();
        pArr->trailing.clear();
        return;
    }
    if (TableStorage* pTbl = tableStorage()) {
        pTbl->entries.clear();
        pTbl->trailing.clear();
        pTbl->invalidateIndex();
        return;
    }
    destroyPayload();
}

TOMLKIT11_INLINE bool Item::contains(StringView key) const noexcept
{
    return find(key) != nullptr;
}

TOMLKIT11_INLINE Item* Item::find(StringView key) noexcept
{
    TableStorage* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return nullptr;
    }
    const size_t idx = pTbl->findIndex(key);
    return (idx == StringView::npos) ? nullptr : pTbl->entries[idx]->value.get();
}

TOMLKIT11_INLINE const Item* Item::find(StringView key) const noexcept
{
    const TableStorage* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return nullptr;
    }
    const size_t idx = pTbl->findIndex(key);
    return (idx == StringView::npos) ? nullptr : pTbl->entries[idx]->value.get();
}

TOMLKIT11_INLINE Item* Item::find(size_t index) noexcept
{
    ArrayStorage* pArr = arrayStorage();
    if (pArr == nullptr || index >= pArr->elements.size()) {
        return nullptr;
    }
    return pArr->elements[index].value.get();
}

TOMLKIT11_INLINE const Item* Item::find(size_t index) const noexcept
{
    const ArrayStorage* pArr = arrayStorage();
    if (pArr == nullptr || index >= pArr->elements.size()) {
        return nullptr;
    }
    return pArr->elements[index].value.get();
}

TOMLKIT11_INLINE Item& Item::arrayElementAtMutable(size_t index)
{
    if (!isArray()) {
        detail::throwOrAbortCannotIndexOnKind(kindName());
    }
    if (Item* pFound = find(index)) {
        return *pFound;
    }
    detail::throwOrAbortOutOfRange(
        "Array index " + detail::uintToString((uint64_t)index)
        + " out of bounds (size = " + detail::uintToString((uint64_t)size()) + ")"
    );
}

TOMLKIT11_INLINE const Item& Item::arrayElementAtConst(size_t index) const
{
    if (!isArray()) {
        detail::throwOrAbortCannotIndexOnKind(kindName());
    }
    if (const Item* pFound = find(index)) {
        return *pFound;
    }
    detail::throwOrAbortOutOfRange(
        "Array index " + detail::uintToString((uint64_t)index)
        + " out of bounds (size = " + detail::uintToString((uint64_t)size()) + ")"
    );
}

TOMLKIT11_INLINE const Item& Item::tableElementAtConst(StringView key) const
{
    if (!isTable()) {
        detail::throwOrAbortCannotLookupKeyOnKind(key, kindName());
    }
    if (const Item* pFound = find(key)) {
        return *pFound;
    }
    detail::throwOrAbortOutOfRange(
        "Key '" + detail::toString(key) + "' not found in TOML table"
    );
}

#if TOMLKIT11_HAS_EXCEPTIONS
TOMLKIT11_INLINE Item& Item::at(StringView key)
{
    if (!isTable()) {
        detail::throwOrAbortCannotLookupKeyOnKind(key, kindName());
    }
    if (Item* pFound = find(key)) {
        return *pFound;
    }
    detail::throwOrAbortOutOfRange(
        "Key '" + detail::toString(key) + "' not found in TOML table"
    );
}

TOMLKIT11_INLINE const Item& Item::at(StringView key) const
{
    return tableElementAtConst(key);
}

TOMLKIT11_INLINE Item& Item::at(size_t index)
{
    return arrayElementAtMutable(index);
}

TOMLKIT11_INLINE const Item& Item::at(size_t index) const
{
    return arrayElementAtConst(index);
}
#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_INLINE Item& Item::operator[](StringView key)
{
    if (isEmpty()) {
        setTablePayload(detail::makeUnique<TableStorage>(TableStyle::Standard));
        m_rawRepr.clear();
    }
    ensureKind(ItemKind::Table);

    TableStorage* pTbl = tableStorage();
    const size_t idx = pTbl->findIndex(key);
    if (idx != StringView::npos) {
        return *pTbl->entries[idx]->value;
    }

    Key newKey(detail::toString(key), KeyStyle::Bare);
    Item newVal;

    if (pTbl->style == TableStyle::Inline) {
        // Complete comma and spacing on the previous entry when appending to an inline table.
        if (!pTbl->entries.empty()) {
            TableEntry& prevEntry = *pTbl->entries.back();
            if (!prevEntry.hasComma) {
                prevEntry.hasComma = true;
                if (pTbl->multilineInline) {
                    prevEntry.afterComma = "\n";
                }
                else if (prevEntry.afterComma.empty()) {
                    prevEntry.afterComma = " ";
                }
            }
        }
        else if (!pTbl->multilineInline) {
            newKey.leaf().decor.prefix = " ";
            if (pTbl->trailing.empty()) {
                pTbl->trailing = " ";
            }
        }

        if (pTbl->multilineInline) {
            newKey.leaf().decor.prefix = "  ";
        }
        newKey.leaf().decor.suffix = " ";
        newVal.decor().prefix = " ";
    }
    else {
        newKey.leaf().decor.suffix = " ";
        newVal.decor().prefix = " ";
        newVal.decor().suffix = "\n";
    }

    const uint32_t order = pTbl->nextEntryOrder++;
    pTbl->entries.push_back(detail::makeUnique<TableEntry>(
        std::move(newKey),
        std::move(newVal),
        order
    ));
    pTbl->invalidateIndex();
    return *pTbl->entries.back()->value;
}

TOMLKIT11_INLINE const Item& Item::operator[](StringView key) const
{
    return tableElementAtConst(key);
}

TOMLKIT11_INLINE std::pair<Item&, bool> Item::insertOrAssign(StringView key, Item val)
{
    if (isEmpty()) {
        setTablePayload(detail::makeUnique<TableStorage>(TableStyle::Standard));
        m_rawRepr.clear();
    }
    ensureKind(ItemKind::Table);

    TableStorage* pTbl = tableStorage();
    const size_t idx = pTbl->findIndex(key);
    if (idx != StringView::npos) {
        *pTbl->entries[idx]->value = std::move(val);
        return std::pair<Item&, bool>(*pTbl->entries[idx]->value, false);
    }

    Item& ref = (*this)[key];
    ref = std::move(val);
    return std::pair<Item&, bool>(ref, true);
}

TOMLKIT11_INLINE void Item::push_back(const Item& elem)
{
    // Copy first so self-referential arr.push_back(arr[0]) stays safe across vector reallocation.
    Item copy(elem);
    push_back(std::move(copy));
}

TOMLKIT11_INLINE void Item::push_back(Item&& elem)
{
    if (isEmpty()) {
        setArrayPayload(detail::makeUnique<ArrayStorage>());
        m_rawRepr.clear();
    }
    ensureKind(ItemKind::Array);

    ArrayStorage* pArr = arrayStorage();
    if (pArr->style == ArrayStyle::ArrayOfTables) {
        if (elem.isEmpty()) {
            elem = Item::table(TableStyle::ArrayOfTablesElement);
        }
        else if (elem.isTable()) {
            elem.tableStorage()->style = TableStyle::ArrayOfTablesElement;
        }
        else {
            detail::throwOrAbortTypeError("Array-of-Tables can only contain Table elements");
        }
        pArr->elements.emplace_back(std::move(elem));
        return;
    }

    // Table elements inside a standard array must serialize as inline tables `{ ... }`.
    TableStorage* pElemTbl = elem.tableStorage();
    if (pElemTbl != nullptr
        && pElemTbl->style == TableStyle::Standard
        && pElemTbl->sectionOrder == 0)
    {
        pElemTbl->style = TableStyle::Inline;
        if (!pElemTbl->multilineInline) {
            for (size_t i = 0; i < pElemTbl->entries.size(); ++i) {
                TableEntry& te = *pElemTbl->entries[i];
                if (i == 0 && te.key.leaf().decor.prefix.empty()) {
                    te.key.leaf().decor.prefix = " ";
                }
                if (te.value->decor().suffix == "\n") {
                    te.value->decor().suffix.clear();
                }
                if (i + 1 < pElemTbl->entries.size()) {
                    te.hasComma = true;
                    if (te.afterComma.empty()) {
                        te.afterComma = " ";
                    }
                }
            }
            if (!pElemTbl->entries.empty() && pElemTbl->trailing.empty()) {
                pElemTbl->trailing = " ";
            }
        }
    }

    if (!pArr->elements.empty()) {
        ArrayElement& prev = pArr->elements.back();
        if (!prev.hasComma) {
            prev.hasComma = true;
            if (pArr->multiline || detail::svContains(StringView(prev.value->decor().prefix), '\n')) {
                if (!prev.beforeComma.empty()
                    && detail::svContains(StringView(prev.beforeComma), '\n'))
                {
                    prev.afterComma = std::move(prev.beforeComma);
                    prev.beforeComma.clear();
                }
                else {
                    prev.afterComma = "\n";
                }
            }
            else if (prev.afterComma.empty()) {
                prev.afterComma = " ";
            }
        }
    }

    const bool isMulti = pArr->multiline
        || (!pArr->elements.empty()
            && (detail::svContains(StringView(pArr->elements.front().value->decor().prefix), '\n')
                || detail::svContains(StringView(pArr->elements.front().afterComma), '\n')));

    if (isMulti && elem.decor().prefix.empty()) {
        if (pArr->elements.empty()) {
            elem.decor().prefix = "\n  ";
            if (pArr->trailing.empty()) {
                pArr->trailing = "\n";
            }
        }
        else {
            elem.decor().prefix = "  ";
        }
    }

    ArrayElement wrapped(std::move(elem));
    if (isMulti) {
        wrapped.hasComma = true;
        wrapped.afterComma = "\n";
    }
    pArr->elements.push_back(std::move(wrapped));
}

TOMLKIT11_INLINE Item& Item::front()
{
    return arrayElementAtMutable(0);
}

TOMLKIT11_INLINE const Item& Item::front() const
{
    return arrayElementAtConst(0);
}

TOMLKIT11_INLINE Item& Item::back()
{
    ensureKind(ItemKind::Array);
    const size_t n = size();
    if (n == 0) {
        detail::throwOrAbortOutOfRange("Called back() on empty TOML array");
    }
    return arrayElementAtMutable(n - 1);
}

TOMLKIT11_INLINE const Item& Item::back() const
{
    ensureKind(ItemKind::Array);
    const size_t n = size();
    if (n == 0) {
        detail::throwOrAbortOutOfRange("Called back() on empty TOML array");
    }
    return arrayElementAtConst(n - 1);
}

TOMLKIT11_INLINE bool Item::erase(StringView key)
{
    TableStorage* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return false;
    }
    const size_t idx = pTbl->findIndex(key);
    if (idx == StringView::npos) {
        return false;
    }

    pTbl->entries.erase(pTbl->entries.begin() + (ptrdiff_t)idx);
    pTbl->invalidateIndex();

    if (pTbl->style == TableStyle::Inline && !pTbl->multilineInline && !pTbl->entries.empty()) {
        TableEntry& lastEntry = *pTbl->entries.back();
        if (lastEntry.hasComma
            && !detail::svContains(StringView(lastEntry.afterComma), '\n')
            && !detail::svContains(StringView(lastEntry.afterComma), '#'))
        {
            lastEntry.hasComma = false;
            if (pTbl->trailing.empty()) {
                pTbl->trailing = std::move(lastEntry.afterComma);
            }
            lastEntry.afterComma.clear();
        }
    }

    return true;
}

TOMLKIT11_INLINE void Item::erase(size_t index)
{
    ensureKind(ItemKind::Array);
    ArrayStorage* pArr = arrayStorage();
    if (index >= pArr->elements.size()) {
        detail::throwOrAbortOutOfRange(
            "Array erase index " + detail::uintToString((uint64_t)index)
            + " out of bounds (size = " + detail::uintToString((uint64_t)pArr->elements.size()) + ")"
        );
    }

    pArr->elements.erase(pArr->elements.begin() + (ptrdiff_t)index);

    if (!pArr->multiline && !pArr->elements.empty()) {
        ArrayElement& lastElem = pArr->elements.back();
        if (lastElem.hasComma
            && !detail::svContains(StringView(lastElem.afterComma), '\n')
            && !detail::svContains(StringView(lastElem.afterComma), '#'))
        {
            lastElem.hasComma = false;
            lastElem.afterComma.clear();
        }
    }
}

TOMLKIT11_INLINE Item::iterator Item::begin()
{
    ensureKind(ItemKind::Array);
    return iterator(arrayStorage()->elements.begin());
}

TOMLKIT11_INLINE Item::iterator Item::end()
{
    ensureKind(ItemKind::Array);
    return iterator(arrayStorage()->elements.end());
}

TOMLKIT11_INLINE Item::const_iterator Item::begin() const
{
    ensureKind(ItemKind::Array);
    return const_iterator(arrayStorage()->elements.begin());
}

TOMLKIT11_INLINE Item::const_iterator Item::end() const
{
    ensureKind(ItemKind::Array);
    return const_iterator(arrayStorage()->elements.end());
}

TOMLKIT11_INLINE TableItemsRange<false> Item::tableItems()
{
    ensureKind(ItemKind::Table);
    return TableItemsRange<false>(&tableStorage()->entries);
}

TOMLKIT11_INLINE TableItemsRange<true> Item::tableItems() const
{
    ensureKind(ItemKind::Table);
    return TableItemsRange<true>(&tableStorage()->entries);
}

// ==================== Comments & Style API ====================

TOMLKIT11_INLINE Optional<std::string> Item::inlineComment() const
{
    if (const TableStorage* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::Standard || pTbl->style == TableStyle::ArrayOfTablesElement) {
            return pTbl->headerDecor.inlineComment();
        }
    }
    return m_decor.inlineComment();
}

TOMLKIT11_INLINE Item& Item::setInlineComment(StringView comment)
{
    if (TableStorage* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::Standard || pTbl->style == TableStyle::ArrayOfTablesElement) {
            if (pTbl->headerDecor.suffix.empty()) {
                pTbl->headerDecor.suffix = "\n";
            }
            pTbl->headerDecor.setInlineComment(comment);
            return *this;
        }
    }
    m_decor.setInlineComment(comment);
    return *this;
}

TOMLKIT11_INLINE Item& Item::clearInlineComment()
{
    if (TableStorage* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::Standard || pTbl->style == TableStyle::ArrayOfTablesElement) {
            pTbl->headerDecor.clearInlineComment();
            return *this;
        }
    }
    m_decor.clearInlineComment();
    return *this;
}

TOMLKIT11_INLINE std::vector<std::string> Item::leadingComments() const
{
    if (const TableStorage* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::DocumentRoot
            || pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ArrayOfTablesElement)
        {
            return pTbl->headerDecor.leadingComments();
        }
    }
    if (m_pOwningKey != nullptr) {
        return m_pOwningKey->leadingComments();
    }
    return m_decor.leadingComments();
}

TOMLKIT11_INLINE Item& Item::addLeadingComment(StringView comment, StringView newline)
{
    if (TableStorage* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::DocumentRoot
            || pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ArrayOfTablesElement)
        {
            pTbl->headerDecor.addLeadingComment(comment, newline);
            return *this;
        }
    }
    if (m_pOwningKey != nullptr) {
        m_pOwningKey->addLeadingComment(comment, newline);
        return *this;
    }
    m_decor.addLeadingComment(comment, newline);
    return *this;
}

TOMLKIT11_INLINE Item& Item::clearLeadingComments()
{
    if (TableStorage* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::DocumentRoot
            || pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ArrayOfTablesElement)
        {
            pTbl->headerDecor.clearLeadingComments();
            return *this;
        }
    }
    if (m_pOwningKey != nullptr) {
        m_pOwningKey->clearLeadingComments();
        return *this;
    }
    m_decor.clearLeadingComments();
    return *this;
}

TOMLKIT11_INLINE Key* Item::findKey(StringView key) noexcept
{
    TableStorage* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return nullptr;
    }
    const size_t idx = pTbl->findIndex(key);
    return (idx == StringView::npos) ? nullptr : &pTbl->entries[idx]->key;
}

TOMLKIT11_INLINE const Key* Item::findKey(StringView key) const noexcept
{
    const TableStorage* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return nullptr;
    }
    const size_t idx = pTbl->findIndex(key);
    return (idx == StringView::npos) ? nullptr : &pTbl->entries[idx]->key;
}

#if TOMLKIT11_HAS_EXCEPTIONS
TOMLKIT11_INLINE Key& Item::keyAt(StringView key)
{
    ensureKind(ItemKind::Table);
    TableStorage* pTbl = tableStorage();
    const size_t idx = pTbl->findIndex(key);
    if (idx == StringView::npos) {
        detail::throwOrAbortOutOfRange(
            "Key '" + detail::toString(key) + "' not found in TOML table"
        );
    }
    return pTbl->entries[idx]->key;
}

TOMLKIT11_INLINE const Key& Item::keyAt(StringView key) const
{
    ensureKind(ItemKind::Table);
    const TableStorage* pTbl = tableStorage();
    const size_t idx = pTbl->findIndex(key);
    if (idx == StringView::npos) {
        detail::throwOrAbortOutOfRange(
            "Key '" + detail::toString(key) + "' not found in TOML table"
        );
    }
    return pTbl->entries[idx]->key;
}
#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_INLINE Item& Item::setStringStyle(StringStyle style) noexcept
{
    m_stringStyle = style;
    m_rawRepr.clear();
    return *this;
}

TOMLKIT11_INLINE Item& Item::setIntegerRadix(IntegerRadix radix) noexcept
{
    m_intRadix = radix;
    m_rawRepr.clear();
    return *this;
}

TOMLKIT11_INLINE TableStyle Item::tableStyle() const noexcept
{
    if (const TableStorage* pTbl = tableStorage()) {
        return pTbl->style;
    }
    return TableStyle::Standard;
}

TOMLKIT11_INLINE Item& Item::setTableStyle(TableStyle style)
{
    ensureKind(ItemKind::Table);
    tableStorage()->style = style;
    return *this;
}

TOMLKIT11_INLINE ArrayStyle Item::arrayStyle() const noexcept
{
    if (const ArrayStorage* pArr = arrayStorage()) {
        return pArr->style;
    }
    return ArrayStyle::Standard;
}

TOMLKIT11_INLINE Item& Item::setArrayStyle(ArrayStyle style)
{
    ensureKind(ItemKind::Array);
    arrayStorage()->style = style;
    return *this;
}

TOMLKIT11_INLINE Item& Item::setMultilineArray(bool multiline)
{
    ensureKind(ItemKind::Array);
    ArrayStorage* pArr = arrayStorage();
    pArr->multiline = multiline;
    pArr->hasExplicitLayout = true;

    if (pArr->style == ArrayStyle::Standard && !pArr->elements.empty()) {
        // Rearrange whitespace only if no comments exist in the array, preserving existing comments.
        bool hasAnyComments = detail::svContains(StringView(pArr->trailing), '#');
        for (size_t i = 0; i < pArr->elements.size(); ++i) {
            const ArrayElement& el = pArr->elements[i];
            if (detail::svContains(StringView(el.value->decor().prefix), '#')
                || detail::svContains(StringView(el.beforeComma), '#')
                || detail::svContains(StringView(el.afterComma), '#'))
            {
                hasAnyComments = true;
                break;
            }
        }

        if (!hasAnyComments) {
            if (multiline) {
                for (size_t i = 0; i < pArr->elements.size(); ++i) {
                    ArrayElement& el = pArr->elements[i];
                    el.value->decor().prefix = (i == 0) ? "\n  " : "  ";
                    el.beforeComma.clear();
                    el.hasComma = true;
                    el.afterComma = "\n";
                }
                pArr->trailing.clear();
            }
            else {
                for (size_t i = 0; i < pArr->elements.size(); ++i) {
                    ArrayElement& el = pArr->elements[i];
                    el.value->decor().prefix.clear();
                    el.beforeComma.clear();
                    el.hasComma = (i + 1 < pArr->elements.size());
                    el.afterComma = (i + 1 < pArr->elements.size()) ? " " : "";
                }
                pArr->trailing.clear();
            }
        }
    }

    return *this;
}

TOMLKIT11_INLINE Item& Item::setMultilineInlineTable(bool multiline)
{
    ensureKind(ItemKind::Table);
    TableStorage* pTbl = tableStorage();
    pTbl->style = TableStyle::Inline;
    pTbl->multilineInline = multiline;
    return *this;
}

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
