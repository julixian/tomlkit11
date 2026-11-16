#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <locale.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <windows.h>
#else
#    include <locale.h>
#    if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#        include <xlocale.h>
#    endif
#    include <sys/stat.h>
#    include <sys/types.h>
#endif

TOMLKIT11_NAMESPACE_BEGIN

namespace
{

struct FlattenedBodyEntry
{
    std::vector<KeySegment> fallbackDottedPrefix;
    const TableEntry* pEntry;
    uint32_t primaryOrder;
    uint32_t secondaryOrder;

    FlattenedBodyEntry(
        std::vector<KeySegment> prefix,
        const TableEntry* pEnt,
        uint32_t primary,
        uint32_t secondary
    )
        : fallbackDottedPrefix(std::move(prefix))
        , pEntry(pEnt)
        , primaryOrder(primary)
        , secondaryOrder(secondary)
    { }
};

struct SectionTask
{
    const TableStorage* pTable;
    bool isAotElement;
    std::vector<KeySegment> fallbackPath;
    uint32_t anchorOrder;
    uint32_t discoveryOrder;

    SectionTask(
        const TableStorage* pTbl,
        bool isAot,
        std::vector<KeySegment> path,
        uint32_t anchor,
        uint32_t discovery
    )
        : pTable(pTbl)
        , isAotElement(isAot)
        , fallbackPath(std::move(path))
        , anchorOrder(anchor)
        , discoveryOrder(discovery)
    { }
};

inline bool isSectionLevelChild(const Item& child) noexcept
{
    if (const ArrayStorage* pArr = child.arrayStorage()) {
        return pArr->style == ArrayStyle::ArrayOfTables;
    }
    if (const TableStorage* pTbl = child.tableStorage()) {
        return pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ImplicitSuper
            || pTbl->style == TableStyle::ArrayOfTablesElement
            || pTbl->style == TableStyle::DocumentRoot;
    }
    return false;
}

inline bool headerKeyPathMatches(
    const std::vector<KeySegment>& recorded,
    const std::vector<KeySegment>& actual
) noexcept
{
    if (recorded.size() != actual.size()) {
        return false;
    }
    for (size_t i = 0; i < recorded.size(); ++i) {
        if (recorded[i].name != actual[i].name) {
            return false;
        }
    }
    return true;
}

inline void appendRadixUint(std::string& out, uint64_t value, unsigned base)
{
    static const char kDigits[] = "0123456789abcdef";
    char buf[68];
    size_t pos = sizeof(buf);
    do {
        buf[--pos] = kDigits[value % base];
        value /= base;
    }
    while (value != 0u);
    out.append(buf + pos, sizeof(buf) - pos);
}

#if defined(_WIN32)
struct EmitterCLocaleHolder
{
    _locale_t loc;

    EmitterCLocaleHolder() noexcept
        : loc(::_create_locale(LC_NUMERIC, "C"))
    { }

    ~EmitterCLocaleHolder()
    {
        if (loc != nullptr) {
            ::_free_locale(loc);
        }
    }

    EmitterCLocaleHolder(const EmitterCLocaleHolder&) = delete;
    EmitterCLocaleHolder& operator=(const EmitterCLocaleHolder&) = delete;
};

inline int formatFloatWithPass(char* pBuf, size_t bufSize, int pass, double v, _locale_t loc) noexcept
{
#    if defined(_MSC_VER)
    switch (pass)
    {
    case 0:
        return ::_snprintf_s_l(pBuf, bufSize, _TRUNCATE, "%.15g", loc, v);
    case 1:
        return ::_snprintf_s_l(pBuf, bufSize, _TRUNCATE, "%.16g", loc, v);
    default:
        return ::_snprintf_s_l(pBuf, bufSize, _TRUNCATE, "%.17g", loc, v);
    }
#    else
    switch (pass)
    {
    case 0:
        return ::_snprintf_l(pBuf, bufSize, "%.15g", loc, v);
    case 1:
        return ::_snprintf_l(pBuf, bufSize, "%.16g", loc, v);
    default:
        return ::_snprintf_l(pBuf, bufSize, "%.17g", loc, v);
    }
#    endif
}
#endif

inline int formatFloatStd(char* pBuf, size_t bufSize, int pass, double v) noexcept
{
    switch (pass)
    {
    case 0:
        return std::snprintf(pBuf, bufSize, "%.15g", v);
    case 1:
        return std::snprintf(pBuf, bufSize, "%.16g", v);
    default:
        return std::snprintf(pBuf, bufSize, "%.17g", v);
    }
}

inline void normalizeDecimalSeparatorToDot(std::string& text)
{
    const std::lconv* pConv = std::localeconv();
    if (pConv != nullptr && pConv->decimal_point != nullptr && pConv->decimal_point[0] != '\0') {
        const std::string decStr(pConv->decimal_point);
        if (decStr != ".") {
            const size_t pos = text.find(decStr);
            if (pos != std::string::npos) {
                text.replace(pos, decStr.size(), ".");
            }
        }
    }
}

inline bool strtodFallbackLocale(const std::string& dotText, double& outVal) noexcept
{
    const std::lconv* pConv = std::localeconv();
    if (pConv != nullptr && pConv->decimal_point != nullptr && pConv->decimal_point[0] != '\0') {
        if (pConv->decimal_point[0] != '.' || pConv->decimal_point[1] != '\0') {
            const size_t dotPos = dotText.find('.');
            if (dotPos != std::string::npos) {
                std::string localized = dotText;
                localized.replace(dotPos, 1, pConv->decimal_point);
                char* pEnd = nullptr;
                errno = 0;
                const double val = std::strtod(localized.c_str(), &pEnd);
                if (pEnd == localized.c_str() + localized.size()) {
                    outVal = val;
                    return true;
                }
                return false;
            }
        }
    }
    char* pEnd = nullptr;
    errno = 0;
    const double val = std::strtod(dotText.c_str(), &pEnd);
    if (pEnd == dotText.c_str() + dotText.size()) {
        outVal = val;
        return true;
    }
    return false;
}

#if !defined(_WIN32) && defined(LC_NUMERIC_MASK) && (defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
struct PosixCLocaleHolder
{
    locale_t loc;

    PosixCLocaleHolder() noexcept
        : loc(::newlocale(LC_NUMERIC_MASK, "C", (locale_t)0))
    { }

    ~PosixCLocaleHolder()
    {
        if (loc != (locale_t)0) {
            ::freelocale(loc);
        }
    }

    PosixCLocaleHolder(const PosixCLocaleHolder&) = delete;
    PosixCLocaleHolder& operator=(const PosixCLocaleHolder&) = delete;
};

struct ScopedPosixCLocale
{
    locale_t prev;
    bool active;

    explicit ScopedPosixCLocale(locale_t cLoc) noexcept
        : prev((locale_t)0)
        , active(false)
    {
        if (cLoc != (locale_t)0) {
            prev = ::uselocale(cLoc);
            active = true;
        }
    }

    ~ScopedPosixCLocale()
    {
        if (active) {
            ::uselocale(prev);
        }
    }

    ScopedPosixCLocale(const ScopedPosixCLocale&) = delete;
    ScopedPosixCLocale& operator=(const ScopedPosixCLocale&) = delete;
};
#endif

inline bool formatAndVerifyFiniteFloat(int pass, double v, std::string& outStr)
{
    char buf[64];
#if defined(_WIN32)
    static const EmitterCLocaleHolder cLocale;
    if (cLocale.loc != nullptr) {
        const int len = formatFloatWithPass(buf, sizeof(buf), pass, v, cLocale.loc);
        if (len <= 0 || (size_t)len >= sizeof(buf)) {
            return false;
        }
        outStr.assign(buf, (size_t)len);
        if (pass < 2) {
            char* pEnd = nullptr;
            errno = 0;
            const double parsed = ::_strtod_l(outStr.c_str(), &pEnd, cLocale.loc);
            return pEnd == outStr.c_str() + outStr.size() && parsed == v;
        }
        return true;
    }
#elif defined(LC_NUMERIC_MASK) && (defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
    static const PosixCLocaleHolder cLocale;
    if (cLocale.loc != (locale_t)0) {
        ScopedPosixCLocale guard(cLocale.loc);
        const int len = formatFloatStd(buf, sizeof(buf), pass, v);
        if (len <= 0 || (size_t)len >= sizeof(buf)) {
            return false;
        }
        outStr.assign(buf, (size_t)len);
        if (pass < 2) {
            char* pEnd = nullptr;
            errno = 0;
            const double parsed = std::strtod(outStr.c_str(), &pEnd);
            return pEnd == outStr.c_str() + outStr.size() && parsed == v;
        }
        return true;
    }
#endif

    const int len = formatFloatStd(buf, sizeof(buf), pass, v);
    if (len <= 0 || (size_t)len >= sizeof(buf)) {
        return false;
    }
    outStr.assign(buf, (size_t)len);
    normalizeDecimalSeparatorToDot(outStr);
    if (pass < 2) {
        double parsed = 0.0;
        return strtodFallbackLocale(outStr, parsed) && parsed == v;
    }
    return true;
}

class EmitterImpl
{
public:
    explicit EmitterImpl(FormatOptions options) noexcept
        : m_options(options)
    { }

    bool emitTopLevel(const Item& item, std::string& out, ParseError& outErr) const
    {
        if (!item.isTable()) {
            out.append(item.decor().prefix);
            if (!emitValue(out, item, outErr)) {
                return false;
            }
            out.append(item.decor().suffix);
            return true;
        }

        const TableStorage* const pRoot = item.tableStorage();
        if (pRoot == nullptr) {
            setTypeError(outErr, "Cannot serialize null TOML table");
            return false;
        }

        if (pRoot->style == TableStyle::Inline) {
            out.append(item.decor().prefix);
            if (!emitInlineTable(out, pRoot, outErr)) {
                return false;
            }
            out.append(item.decor().suffix);
            return true;
        }

        if (pRoot->hasBom) {
            out.append("\xEF\xBB\xBF");
        }

        const std::vector<KeySegment> emptyPath;

        // If a single non-root Standard table with a header path is passed, emit its own header first.
        if (pRoot->style == TableStyle::Standard && !pRoot->headerKeyPath.empty()) {
            emitSectionHeader(out, *pRoot, false, pRoot->headerKeyPath);
            if (!emitSectionBody(out, *pRoot, pRoot->headerKeyPath, outErr)) {
                return false;
            }
        }
        else {
            out.append(pRoot->headerDecor.prefix);
            if (!emitSectionBody(out, *pRoot, emptyPath, outErr)) {
                return false;
            }
        }

        std::vector<SectionTask> sections;
        uint32_t discoveryCounter = 1;
        std::vector<KeySegment> curPath;
        collectSections(
            *pRoot,
            curPath,
            (std::numeric_limits<uint32_t>::max)() - 1u,
            discoveryCounter,
            sections
        );

        std::stable_sort(sections.begin(), sections.end(), [](const SectionTask& a, const SectionTask& b)
            {
                if (a.anchorOrder != b.anchorOrder) {
                    return a.anchorOrder < b.anchorOrder;
                }
                return a.discoveryOrder < b.discoveryOrder;
            });

        for (size_t i = 0; i < sections.size(); ++i) {
            const SectionTask& task = sections[i];
            const bool isNewlyCreated = (task.pTable->sectionOrder == 0);
            if (!out.empty() && out.back() != '\n') {
                detail::appendSv(out, m_options.newline);
            }
            if (isNewlyCreated && task.pTable->headerDecor.prefix.empty() && !out.empty()) {
                const StringView outSv(out);
                if (!detail::svEndsWith(outSv, "\n\n") && !detail::svEndsWith(outSv, "\r\n\r\n")) {
                    detail::appendSv(out, m_options.newline);
                }
            }

            const std::vector<KeySegment>& pathToUse = headerKeyPathMatches(task.pTable->headerKeyPath, task.fallbackPath)
                ? task.pTable->headerKeyPath
                : task.fallbackPath;

            emitSectionHeader(out, *task.pTable, task.isAotElement, pathToUse);
            if (!emitSectionBody(out, *task.pTable, pathToUse, outErr)) {
                return false;
            }
        }

        if (!pRoot->trailing.empty()) {
            if (!out.empty() && out.back() != '\n'
                && pRoot->trailing[0] != '\n'
                && pRoot->trailing[0] != '\r')
            {
                detail::appendSv(out, m_options.newline);
            }
            out.append(pRoot->trailing);
        }

        return true;
    }

private:
    static void setTypeError(ParseError& outErr, const std::string& msg)
    {
        outErr = ParseError(
            ErrorCategory::TypeMismatch,
            SourceLocation(),
            msg,
            std::string(),
            "TOML type error: " + msg
        );
    }

    static bool hasBodyEntries(const TableStorage& tbl) noexcept
    {
        for (size_t i = 0; i < tbl.entries.size(); ++i) {
            const TableEntry* pEntry = tbl.entries[i].get();
            if (pEntry == nullptr || pEntry->value == nullptr) {
                continue;
            }
            const Item& val = *pEntry->value;
            if (isSectionLevelChild(val)) {
                continue;
            }
            const TableStorage* pSubTbl = val.tableStorage();
            if (pSubTbl != nullptr && pSubTbl->style == TableStyle::Dotted) {
                if (hasBodyEntries(*pSubTbl)) {
                    return true;
                }
                continue;
            }
            return true;
        }
        return false;
    }

    static uint32_t maxParsedEntryOrderInSubtree(const TableStorage& tbl, uint32_t fallbackOrder) noexcept
    {
        uint32_t best = fallbackOrder;
        for (size_t i = 0; i < tbl.entries.size(); ++i) {
            const TableEntry* pEntry = tbl.entries[i].get();
            if (pEntry == nullptr || pEntry->value == nullptr) {
                continue;
            }
            const Item& val = *pEntry->value;
            if (isSectionLevelChild(val)) {
                continue;
            }
            const TableStorage* pSubTbl = val.tableStorage();
            if (pSubTbl != nullptr && pSubTbl->style == TableStyle::Dotted) {
                const uint32_t subBest = maxParsedEntryOrderInSubtree(*pSubTbl, pEntry->entryOrder);
                if (subBest > best) {
                    best = subBest;
                }
            }
            else if (pEntry->entryOrder > best) {
                best = pEntry->entryOrder;
            }
        }
        return best;
    }

    void collectBodyEntries(
        const TableStorage& tbl,
        std::vector<KeySegment>& prefixAccum,
        uint32_t inheritedPrimaryOrder,
        uint32_t& seqCounter,
        std::vector<FlattenedBodyEntry>& outEntries,
        bool insideInlineTable = false
    ) const
    {
        for (size_t i = 0; i < tbl.entries.size(); ++i) {
            const TableEntry* pEntry = tbl.entries[i].get();
            if (pEntry == nullptr || pEntry->value == nullptr) {
                continue;
            }
            const Item& val = *pEntry->value;
            if (!insideInlineTable && isSectionLevelChild(val)) {
                continue;
            }

            const TableStorage* pSubTbl = val.tableStorage();
            if (pSubTbl != nullptr && pSubTbl->style == TableStyle::Dotted) {
                prefixAccum.push_back(pEntry->key.leaf());
                const uint32_t subAnchor = maxParsedEntryOrderInSubtree(*pSubTbl, pEntry->entryOrder);
                collectBodyEntries(*pSubTbl, prefixAccum, subAnchor, seqCounter, outEntries, insideInlineTable);
                prefixAccum.pop_back();
                continue;
            }

            // Parsed leaves store their line order in entryOrder; programmatically appended
            // leaves under a dotted table have empty dottedPrefix and attach at the dotted group end.
            const bool isProgrammaticDottedChild = !prefixAccum.empty() && pEntry->key.dottedPrefix().empty();
            const uint32_t primary = isProgrammaticDottedChild ? inheritedPrimaryOrder : pEntry->entryOrder;

            outEntries.push_back(FlattenedBodyEntry(
                prefixAccum,
                pEntry,
                primary,
                seqCounter++
            ));
        }
    }

    static uint32_t maxParsedSectionOrderInSubtree(const TableStorage& tbl) noexcept
    {
        uint32_t maxOrd = tbl.sectionOrder;
        for (size_t i = 0; i < tbl.entries.size(); ++i) {
            const TableEntry* pEntry = tbl.entries[i].get();
            if (pEntry == nullptr || pEntry->value == nullptr) {
                continue;
            }
            const Item& val = *pEntry->value;
            const TableStorage* pSubTbl = val.tableStorage();
            if (pSubTbl != nullptr) {
                if (pSubTbl->style != TableStyle::Inline) {
                    const uint32_t subOrd = maxParsedSectionOrderInSubtree(*pSubTbl);
                    if (subOrd > maxOrd) {
                        maxOrd = subOrd;
                    }
                }
            }
            else {
                const ArrayStorage* pArr = val.arrayStorage();
                if (pArr != nullptr && pArr->style == ArrayStyle::ArrayOfTables) {
                    for (size_t j = 0; j < pArr->elements.size(); ++j) {
                        const ArrayElement& elem = pArr->elements[j];
                        if (elem.value == nullptr) {
                            continue;
                        }
                        if (const TableStorage* pElemTbl = elem.value->tableStorage()) {
                            const uint32_t elemOrd = maxParsedSectionOrderInSubtree(*pElemTbl);
                            if (elemOrd > maxOrd) {
                                maxOrd = elemOrd;
                            }
                        }
                    }
                }
            }
        }
        return maxOrd;
    }

    void collectSections(
        const TableStorage& parentTbl,
        std::vector<KeySegment>& currentPath,
        uint32_t parentAnchorOrder,
        uint32_t& discoveryCounter,
        std::vector<SectionTask>& outSections
    ) const
    {
        uint32_t runningSiblingAnchor = parentAnchorOrder;
        for (size_t i = 0; i < parentTbl.entries.size(); ++i) {
            const TableEntry* pEntry = parentTbl.entries[i].get();
            if (pEntry == nullptr || pEntry->value == nullptr) {
                continue;
            }
            const Item& val = *pEntry->value;

            const TableStorage* pSubTbl = val.tableStorage();
            if (pSubTbl != nullptr) {
                if (pSubTbl->style == TableStyle::Inline) {
                    continue;
                }

                currentPath.emplace_back(pEntry->key.leaf().name, pEntry->key.leaf().style);

                if (pSubTbl->style == TableStyle::Dotted) {
                    // Dotted tables emit no [header], yet can host nested [[...]] sub-sections.
                    collectSections(*pSubTbl, currentPath, runningSiblingAnchor, discoveryCounter, outSections);
                }
                else if (pSubTbl->style == TableStyle::ImplicitSuper && !hasBodyEntries(*pSubTbl)) {
                    // Pure implicit super-tables with no direct keys emit no [header].
                    const uint32_t subAnchor = maxParsedSectionOrderInSubtree(*pSubTbl);
                    const uint32_t nextAnchor = subAnchor > 0 ? subAnchor : runningSiblingAnchor;
                    collectSections(*pSubTbl, currentPath, nextAnchor, discoveryCounter, outSections);
                    if (subAnchor > 0) {
                        runningSiblingAnchor = subAnchor;
                    }
                }
                else {
                    const uint32_t myAnchor = (pSubTbl->sectionOrder > 0) ? pSubTbl->sectionOrder : runningSiblingAnchor;
                    const uint32_t myDisc = discoveryCounter++;
                    if (pSubTbl->sectionOrder > 0) {
                        runningSiblingAnchor = pSubTbl->sectionOrder;
                    }

                    outSections.push_back(SectionTask(
                        pSubTbl,
                        false,
                        currentPath,
                        myAnchor,
                        myDisc
                    ));

                    collectSections(*pSubTbl, currentPath, myAnchor, discoveryCounter, outSections);
                }

                currentPath.pop_back();
            }
            else {
                const ArrayStorage* pArr = val.arrayStorage();
                if (pArr != nullptr && pArr->style == ArrayStyle::ArrayOfTables) {
                    currentPath.emplace_back(pEntry->key.leaf().name, pEntry->key.leaf().style);
                    uint32_t runningAnchor = runningSiblingAnchor;

                    for (size_t j = 0; j < pArr->elements.size(); ++j) {
                        const ArrayElement& elem = pArr->elements[j];
                        if (elem.value == nullptr) {
                            continue;
                        }
                        const TableStorage* pElemTbl = elem.value->tableStorage();
                        if (pElemTbl == nullptr) {
                            continue;
                        }

                        const uint32_t myAnchor = (pElemTbl->sectionOrder > 0) ? pElemTbl->sectionOrder : runningAnchor;
                        const uint32_t myDisc = discoveryCounter++;
                        runningAnchor = myAnchor;
                        if (pElemTbl->sectionOrder > 0) {
                            runningSiblingAnchor = pElemTbl->sectionOrder;
                        }

                        outSections.push_back(SectionTask(
                            pElemTbl,
                            true,
                            currentPath,
                            myAnchor,
                            myDisc
                        ));

                        collectSections(*pElemTbl, currentPath, myAnchor, discoveryCounter, outSections);
                    }

                    currentPath.pop_back();
                }
            }
        }
    }

    void emitKeyPath(std::string& out, const std::vector<KeySegment>& segments, bool includeFirstPrefix) const
    {
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i > 0) {
                out.push_back('.');
            }
            if (i > 0 || includeFirstPrefix) {
                out.append(segments[i].decor.prefix);
            }
            out.append(formatKeySegment(segments[i], m_options.spec));
            out.append(segments[i].decor.suffix);
        }
    }

    void emitEntryKey(std::string& out, const FlattenedBodyEntry& flatEntry) const
    {
        const Key& k = flatEntry.pEntry->key;
        if (!k.dottedPrefix().empty()) {
            emitKeyPath(out, k.dottedPrefix(), true);
            out.push_back('.');
            out.append(k.leaf().decor.prefix);
            out.append(formatKeySegment(k.leaf(), m_options.spec));
            out.append(k.leaf().decor.suffix);
            return;
        }

        if (!flatEntry.fallbackDottedPrefix.empty()) {
            out.append(k.leaf().decor.prefix);
            for (size_t i = 0; i < flatEntry.fallbackDottedPrefix.size(); ++i) {
                const KeySegment& prefixSeg = flatEntry.fallbackDottedPrefix[i];
                KeySegment cleanSeg(prefixSeg.name, prefixSeg.style);
                out.append(formatKeySegment(cleanSeg, m_options.spec));
                out.push_back('.');
            }
            out.append(formatKeySegment(k.leaf(), m_options.spec));
            out.append(k.leaf().decor.suffix);
            return;
        }

        out.append(k.leaf().decor.prefix);
        out.append(formatKeySegment(k.leaf(), m_options.spec));
        out.append(k.leaf().decor.suffix);
    }

    void emitSectionHeader(
        std::string& out,
        const TableStorage& sec,
        bool isAot,
        const std::vector<KeySegment>& keyPath
    ) const
    {
        out.append(sec.headerDecor.prefix);
        out.append(isAot ? "[[" : "[");
        emitKeyPath(out, keyPath, true);
        out.append(isAot ? "]]" : "]");
        if (!sec.headerDecor.suffix.empty()) {
            out.append(sec.headerDecor.suffix);
        }
        else if (sec.sectionOrder == 0 || hasBodyEntries(sec)) {
            detail::appendSv(out, m_options.newline);
        }
    }

    std::string formatDiagnosticKeyPath(
        const std::vector<KeySegment>& sectionPath,
        const FlattenedBodyEntry& fe
    ) const
    {
        std::string pathStr;
        const auto appendSegFunc = [&](const KeySegment& seg)
            {
                if (!pathStr.empty()) {
                    pathStr.push_back('.');
                }
                pathStr.append(formatKeySegment(seg, m_options.spec));
            };

        for (size_t i = 0; i < sectionPath.size(); ++i) {
            appendSegFunc(sectionPath[i]);
        }
        const Key& k = fe.pEntry->key;
        if (!k.dottedPrefix().empty()) {
            for (size_t i = 0; i < k.dottedPrefix().size(); ++i) {
                appendSegFunc(k.dottedPrefix()[i]);
            }
        }
        else {
            for (size_t i = 0; i < fe.fallbackDottedPrefix.size(); ++i) {
                appendSegFunc(fe.fallbackDottedPrefix[i]);
            }
        }
        appendSegFunc(k.leaf());
        return pathStr;
    }

    bool emitSectionBody(
        std::string& out,
        const TableStorage& sec,
        const std::vector<KeySegment>& sectionPath,
        ParseError& outErr
    ) const
    {
        std::vector<FlattenedBodyEntry> bodyEntries;
        std::vector<KeySegment> prefixAccum;
        uint32_t seqCounter = 1;
        collectBodyEntries(sec, prefixAccum, 0, seqCounter, bodyEntries);

        std::stable_sort(bodyEntries.begin(), bodyEntries.end(), [](const FlattenedBodyEntry& a, const FlattenedBodyEntry& b)
            {
                if (a.primaryOrder != b.primaryOrder) {
                    return a.primaryOrder < b.primaryOrder;
                }
                return a.secondaryOrder < b.secondaryOrder;
            });

        for (size_t i = 0; i < bodyEntries.size(); ++i) {
            if (i > 0 && !out.empty() && out.back() != '\n') {
                detail::appendSv(out, m_options.newline);
            }

            const FlattenedBodyEntry& fe = bodyEntries[i];
            if (fe.pEntry == nullptr || fe.pEntry->value == nullptr) {
                setTypeError(outErr, "Cannot serialize null TOML table entry");
                return false;
            }
            const Item& val = *fe.pEntry->value;
            if (val.isEmpty()) {
                setTypeError(
                    outErr,
                    "Cannot serialize uninitialized 'empty' TOML item at key '"
                    + formatDiagnosticKeyPath(sectionPath, fe) + "'"
                );
                return false;
            }

            emitEntryKey(out, fe);
            out.push_back('=');
            out.append(val.decor().prefix);
            if (!emitValue(out, val, outErr)) {
                return false;
            }
            out.append(val.decor().suffix);
        }

        return true;
    }

    bool emitValue(
        std::string& out,
        const Item& val,
        ParseError& outErr,
        uint32_t indentDepth = 0,
        bool disallowAutoWrap = false
    ) const
    {
        switch (val.kind())
        {
        case ItemKind::Empty:
            setTypeError(outErr, "Cannot serialize uninitialized 'empty' TOML item");
            return false;
        case ItemKind::Array:
            {
                const ArrayStorage* pArr = val.arrayStorage();
                if (pArr == nullptr) {
                    setTypeError(outErr, "Cannot serialize null TOML array");
                    return false;
                }
                return emitStandardArray(out, *pArr, outErr, indentDepth, disallowAutoWrap);
            }
        case ItemKind::Table:
            return emitInlineTable(out, val.tableStorage(), outErr);
        default:
            break;
        }

        // Unmodified scalar literals replay verbatim when compatible with the target spec version.
        if (!val.rawRepr().empty()) {
            bool canReplayRaw = true;
            if (m_options.spec == SpecVersion::v1_0) {
                if (const LocalTime* pLt = val.ifLocalTime()) {
                    if (!pLt->hasSeconds) {
                        canReplayRaw = false;
                    }
                }
                else if (const LocalDateTime* pLdt = val.ifLocalDateTime()) {
                    if (!pLdt->time.hasSeconds) {
                        canReplayRaw = false;
                    }
                }
                else if (const OffsetDateTime* pOdt = val.ifOffsetDateTime()) {
                    if (!pOdt->time.hasSeconds) {
                        canReplayRaw = false;
                    }
                }
                else if (val.isString()) {
                    const StringView rawSv(val.rawRepr());
                    if (detail::svContains(rawSv, "\\e") || detail::svContains(rawSv, "\\x")) {
                        canReplayRaw = false;
                    }
                }
            }
            if (canReplayRaw) {
                out.append(val.rawRepr());
                return true;
            }
        }

        switch (val.kind())
        {
        case ItemKind::Boolean:
            {
                const bool* pBool = val.ifBoolean();
                if (pBool == nullptr) {
                    setTypeError(outErr, "Invalid boolean item payload");
                    return false;
                }
                out.append(*pBool ? "true" : "false");
                return true;
            }
        case ItemKind::Integer:
            {
                const int64_t* pInt = val.ifInteger();
                if (pInt == nullptr) {
                    setTypeError(outErr, "Invalid integer item payload");
                    return false;
                }
                emitInteger(out, *pInt, val.integerRadix());
                return true;
            }
        case ItemKind::Float:
            {
                const double* pFloat = val.ifFloat();
                if (pFloat == nullptr) {
                    setTypeError(outErr, "Invalid float item payload");
                    return false;
                }
                emitFloat(out, *pFloat);
                return true;
            }
        case ItemKind::String:
            {
                const std::string* pStr = val.ifString();
                if (pStr == nullptr) {
                    setTypeError(outErr, "Invalid string item payload");
                    return false;
                }
                emitString(out, StringView(*pStr), val.stringStyle());
                return true;
            }
        case ItemKind::LocalDate:
            {
                const LocalDate* pDate = val.ifLocalDate();
                if (pDate == nullptr) {
                    setTypeError(outErr, "Invalid local_date item payload");
                    return false;
                }
                out.append(pDate->toString());
                return true;
            }
        case ItemKind::LocalTime:
            {
                const LocalTime* pTime = val.ifLocalTime();
                if (pTime == nullptr) {
                    setTypeError(outErr, "Invalid local_time item payload");
                    return false;
                }
                out.append(pTime->toString(m_options.spec == SpecVersion::v1_0));
                return true;
            }
        case ItemKind::LocalDateTime:
            {
                const LocalDateTime* pDt = val.ifLocalDateTime();
                if (pDt == nullptr) {
                    setTypeError(outErr, "Invalid local_date_time item payload");
                    return false;
                }
                out.append(pDt->toString(m_options.spec == SpecVersion::v1_0));
                return true;
            }
        case ItemKind::OffsetDateTime:
            {
                const OffsetDateTime* pOdt = val.ifOffsetDateTime();
                if (pOdt == nullptr) {
                    setTypeError(outErr, "Invalid offset_date_time item payload");
                    return false;
                }
                out.append(pOdt->toString(m_options.spec == SpecVersion::v1_0));
                return true;
            }
        default:
            setTypeError(outErr, "Unsupported TOML item kind");
            return false;
        }
    }

    static void emitInteger(std::string& out, int64_t v, IntegerRadix radix)
    {
        if (v >= 0) {
            switch (radix)
            {
            case IntegerRadix::Hexadecimal:
                out.append("0x");
                appendRadixUint(out, (uint64_t)v, 16u);
                return;
            case IntegerRadix::Octal:
                out.append("0o");
                appendRadixUint(out, (uint64_t)v, 8u);
                return;
            case IntegerRadix::Binary:
                out.append("0b");
                appendRadixUint(out, (uint64_t)v, 2u);
                return;
            case IntegerRadix::Decimal:
                break;
            }
        }
        detail::appendInt(out, v);
    }

    static void emitFloat(std::string& out, double v)
    {
        if (std::isnan(v)) {
            out.append(std::signbit(v) ? "-nan" : "nan");
            return;
        }
        if (std::isinf(v)) {
            out.append(v < 0.0 ? "-inf" : "inf");
            return;
        }
        if (v == 0.0) {
            out.append(std::signbit(v) ? "-0.0" : "0.0");
            return;
        }

        std::string formatted;
        for (int pass = 0; pass < 3; ++pass) {
            if (formatAndVerifyFiniteFloat(pass, v, formatted)) {
                break;
            }
        }
        if (formatted.empty()) {
            out.append("0.0");
            return;
        }

        out.append(formatted);
        if (formatted.find('.') == std::string::npos
            && formatted.find('e') == std::string::npos
            && formatted.find('E') == std::string::npos)
        {
            out.append(".0");
        }
    }

    void emitString(std::string& out, StringView s, StringStyle style) const
    {
        if (style == StringStyle::Literal) {
            bool validLiteral = !detail::svContains(s, '\'')
                && !detail::svContains(s, '\n')
                && !detail::svContains(s, '\r');
            if (validLiteral) {
                for (size_t i = 0; i < s.size(); ++i) {
                    const char ch = s[i];
                    const uint8_t b = (uint8_t)ch;
                    if ((b < 0x20u && ch != '\t') || b == 0x7Fu) {
                        validLiteral = false;
                        break;
                    }
                }
            }
            if (validLiteral) {
                out.push_back('\'');
                detail::appendSv(out, s);
                out.push_back('\'');
                return;
            }
            style = StringStyle::Basic;
        }
        else if (style == StringStyle::MultilineLiteral) {
            bool validMlLiteral = !detail::svContains(s, "'''")
                && !detail::svEndsWith(s, '\'')
                && !detail::svContains(s, '\r');
            if (validMlLiteral) {
                for (size_t i = 0; i < s.size(); ++i) {
                    const char ch = s[i];
                    const uint8_t b = (uint8_t)ch;
                    if ((b < 0x20u && ch != '\t' && ch != '\n') || b == 0x7Fu) {
                        validMlLiteral = false;
                        break;
                    }
                }
            }
            if (validMlLiteral) {
                out.append("'''");
                if (detail::svStartsWith(s, '\n')) {
                    out.push_back('\n');
                }
                detail::appendSv(out, s);
                out.append("'''");
                return;
            }
            style = StringStyle::MultilineBasic;
        }

        if (style == StringStyle::MultilineBasic) {
            out.append("\"\"\"");
            if (detail::svStartsWith(s, '\n')) {
                out.push_back('\n');
            }
            out.append(escapeBasicString(s, true, m_options.spec));
            out.append("\"\"\"");
            return;
        }

        out.push_back('"');
        out.append(escapeBasicString(s, false, m_options.spec));
        out.push_back('"');
    }

    bool emitStandardArray(
        std::string& out,
        const ArrayStorage& arr,
        ParseError& outErr,
        uint32_t indentDepth = 0,
        bool disallowAutoWrap = false
    ) const
    {
        const size_t n = arr.elements.size();

        bool canAutoWrap = !disallowAutoWrap
            && m_options.maxLineWidth > 0
            && !arr.hasExplicitLayout
            && !arr.multiline
            && n > 0
            && arr.trailing.empty();
        if (canAutoWrap) {
            for (size_t i = 0; i < n; ++i) {
                const ArrayElement& elem = arr.elements[i];
                if (elem.value == nullptr) {
                    canAutoWrap = false;
                    break;
                }
                if (!elem.value->decor().prefix.empty()
                    || !elem.beforeComma.empty()
                    || (!elem.afterComma.empty() && elem.afterComma != " "))
                {
                    canAutoWrap = false;
                    break;
                }
            }
        }

        if (canAutoWrap) {
            std::string singleLine;
            if (!emitStandardArray(singleLine, arr, outErr, indentDepth, true)) {
                return false;
            }

            const size_t lastNl = out.rfind('\n');
            const size_t currentCol = (lastNl == std::string::npos)
                ? out.size()
                : (out.size() - lastNl - 1);

            if (singleLine.find('\n') == std::string::npos
                && currentCol + singleLine.size() <= m_options.maxLineWidth)
            {
                out.append(singleLine);
                return true;
            }

            out.push_back('[');
            detail::appendSv(out, m_options.newline);
            for (size_t i = 0; i < n; ++i) {
                const ArrayElement& elem = arr.elements[i];
                if (elem.value == nullptr || elem.value->isEmpty()) {
                    setTypeError(
                        outErr,
                        "Cannot serialize uninitialized 'empty' TOML item at array index "
                        + detail::uintToString((uint64_t)i)
                    );
                    return false;
                }
                for (uint32_t d = 0; d < indentDepth + 1; ++d) {
                    detail::appendSv(out, m_options.indent);
                }
                if (!emitValue(out, *elem.value, outErr, indentDepth + 1, false)) {
                    return false;
                }
                out.push_back(',');
                detail::appendSv(out, m_options.newline);
            }
            for (uint32_t d = 0; d < indentDepth; ++d) {
                detail::appendSv(out, m_options.indent);
            }
            out.push_back(']');
            return true;
        }

        out.push_back('[');
        for (size_t i = 0; i < n; ++i) {
            const ArrayElement& elem = arr.elements[i];
            if (elem.value == nullptr || elem.value->isEmpty()) {
                setTypeError(
                    outErr,
                    "Cannot serialize uninitialized 'empty' TOML item at array index "
                    + detail::uintToString((uint64_t)i)
                );
                return false;
            }
            out.append(elem.value->decor().prefix);
            if (!emitValue(out, *elem.value, outErr, indentDepth, disallowAutoWrap)) {
                return false;
            }
            out.append(elem.beforeComma);

            if (i + 1 < n) {
                out.push_back(',');
                if (elem.hasComma) {
                    out.append(elem.afterComma);
                }
                else if (elem.afterComma.empty()
                    && arr.elements[i + 1].value != nullptr
                    && arr.elements[i + 1].value->decor().prefix.empty())
                {
                    out.push_back(' ');
                }
            }
            else if (elem.hasComma) {
                out.push_back(',');
                out.append(elem.afterComma);
            }
        }
        out.append(arr.trailing);
        out.push_back(']');
        return true;
    }

    bool emitInlineTable(std::string& out, const TableStorage* pTbl, ParseError& outErr) const
    {
        out.push_back('{');
        if (pTbl == nullptr) {
            out.push_back('}');
            return true;
        }

        std::vector<FlattenedBodyEntry> entries;
        std::vector<KeySegment> prefixAccum;
        uint32_t seqCounter = 1;
        collectBodyEntries(*pTbl, prefixAccum, 0, seqCounter, entries, true);

        std::stable_sort(entries.begin(), entries.end(), [](const FlattenedBodyEntry& a, const FlattenedBodyEntry& b)
            {
                if (a.primaryOrder != b.primaryOrder) {
                    return a.primaryOrder < b.primaryOrder;
                }
                return a.secondaryOrder < b.secondaryOrder;
            });

        const std::vector<KeySegment> emptySectionPath;
        const size_t n = entries.size();
        for (size_t i = 0; i < n; ++i) {
            const FlattenedBodyEntry& fe = entries[i];
            const TableEntry& entry = *fe.pEntry;
            if (entry.value == nullptr || entry.value->isEmpty()) {
                setTypeError(
                    outErr,
                    "Cannot serialize uninitialized 'empty' TOML item at key '"
                    + formatDiagnosticKeyPath(emptySectionPath, fe) + "'"
                );
                return false;
            }
            const Item& val = *entry.value;

            // Downgrade multiline inline tables to single-line whitespace in v1.0 mode.
            if (m_options.spec == SpecVersion::v1_0 && pTbl->multilineInline) {
                if (i == 0) {
                    out.push_back(' ');
                }
                if (!entry.key.dottedPrefix().empty()) {
                    for (size_t j = 0; j < entry.key.dottedPrefix().size(); ++j) {
                        out.append(formatKeySegment(entry.key.dottedPrefix()[j], m_options.spec));
                        out.push_back('.');
                    }
                }
                else {
                    for (size_t j = 0; j < fe.fallbackDottedPrefix.size(); ++j) {
                        out.append(formatKeySegment(fe.fallbackDottedPrefix[j], m_options.spec));
                        out.push_back('.');
                    }
                }
                out.append(formatKeySegment(entry.key.leaf(), m_options.spec));
                out.append(" = ");
                if (!emitValue(out, val, outErr, 0, true)) {
                    return false;
                }
                if (i + 1 < n) {
                    out.append(", ");
                }
                else {
                    out.push_back(' ');
                }
                continue;
            }

            emitEntryKey(out, fe);
            out.push_back('=');
            out.append(val.decor().prefix);
            if (!emitValue(out, val, outErr, 0, !pTbl->multilineInline)) {
                return false;
            }
            out.append(entry.beforeComma);

            if (i + 1 < n) {
                out.push_back(',');
                if (entry.hasComma) {
                    out.append(entry.afterComma);
                }
                else {
                    const Decor& nextLead = entries[i + 1].pEntry->key.lineLeadDecor();
                    if (entry.afterComma.empty() && nextLead.prefix.empty()) {
                        out.push_back(' ');
                    }
                }
            }
            else if (entry.hasComma && m_options.spec != SpecVersion::v1_0) {
                out.push_back(',');
                out.append(entry.afterComma);
            }
        }

        if (!(m_options.spec == SpecVersion::v1_0 && pTbl->multilineInline)) {
            out.append(pTbl->trailing);
        }
        out.push_back('}');
        return true;
    }

    FormatOptions m_options;
};

#ifdef _WIN32
inline bool isWinPathSep(wchar_t ch) noexcept
{
    return ch == L'\\' || ch == L'/';
}

inline size_t findWinRootPrefixEnd(const std::wstring& dirPath) noexcept
{
    const size_t len = dirPath.size();
    if (len >= 8 && dirPath.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
        size_t pos = 8;
        while (pos < len && !isWinPathSep(dirPath[pos])) {
            ++pos;
        }
        if (pos < len) {
            ++pos;
        }
        while (pos < len && !isWinPathSep(dirPath[pos])) {
            ++pos;
        }
        if (pos < len) {
            ++pos;
        }
        return pos;
    }
    if (len >= 4 && dirPath.compare(0, 4, L"\\\\?\\") == 0) {
        if (len >= 6 && dirPath[5] == L':') {
            return (len >= 7 && isWinPathSep(dirPath[6])) ? 7 : 6;
        }
        return 4;
    }
    if (len >= 2 && isWinPathSep(dirPath[0]) && isWinPathSep(dirPath[1])) {
        size_t pos = 2;
        while (pos < len && !isWinPathSep(dirPath[pos])) {
            ++pos;
        }
        if (pos < len) {
            ++pos;
        }
        while (pos < len && !isWinPathSep(dirPath[pos])) {
            ++pos;
        }
        if (pos < len) {
            ++pos;
        }
        return pos;
    }
    if (len >= 2 && dirPath[1] == L':') {
        return (len >= 3 && isWinPathSep(dirPath[2])) ? 3 : 2;
    }
    if (len >= 1 && isWinPathSep(dirPath[0])) {
        return 1;
    }
    return 0;
}

inline bool createDirectoriesRecursiveWin32(const std::wstring& dirPath, std::string& outErrUtf8)
{
    if (dirPath.empty()) {
        return true;
    }

    const DWORD existingAttrs = ::GetFileAttributesW(dirPath.c_str());
    if (existingAttrs != INVALID_FILE_ATTRIBUTES) {
        if ((existingAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return true;
        }
        outErrUtf8 = formatErrorCodeUtf8(std::error_code((int)ERROR_ALREADY_EXISTS, std::system_category()));
        return false;
    }

    const size_t prefixEnd = findWinRootPrefixEnd(dirPath);
    if (dirPath.size() <= prefixEnd) {
        return true;
    }

    for (size_t pos = prefixEnd; pos <= dirPath.size(); ++pos) {
        const bool atEnd = (pos == dirPath.size());
        if (!atEnd && !isWinPathSep(dirPath[pos])) {
            continue;
        }
        if (pos <= prefixEnd || isWinPathSep(dirPath[pos - 1])) {
            continue;
        }

        const std::wstring sub = dirPath.substr(0, pos);
        const DWORD subAttrs = ::GetFileAttributesW(sub.c_str());
        if (subAttrs != INVALID_FILE_ATTRIBUTES) {
            if ((subAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                outErrUtf8 = formatErrorCodeUtf8(std::error_code((int)ERROR_ALREADY_EXISTS, std::system_category()));
                return false;
            }
            continue;
        }

        if (!::CreateDirectoryW(sub.c_str(), nullptr)) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_ALREADY_EXISTS) {
                const DWORD retryAttrs = ::GetFileAttributesW(sub.c_str());
                if (retryAttrs != INVALID_FILE_ATTRIBUTES && (retryAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    continue;
                }
            }
            outErrUtf8 = formatErrorCodeUtf8(std::error_code((int)err, std::system_category()));
            return false;
        }
    }

    return true;
}

inline Result<void, ParseError> trySaveWidePathWin32(
    const std::wstring& targetPath,
    const std::string& targetPathUtf8,
    const Item& doc,
    FormatOptions options
)
{
    if (targetPath.empty()) {
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to open TOML file '' for atomic write",
            "",
            "TOML I/O error: failed to open ''"
        ));
    }
    if (targetPath.find(L'\0') != std::wstring::npos) {
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Invalid TOML file path containing null character '" + targetPathUtf8 + "'",
            targetPathUtf8,
            "TOML I/O error: invalid path '" + targetPathUtf8 + "'"
        ));
    }

    Result<std::string, ParseError> formatted = tryFormat(doc, options);
    if (!formatted.has_value()) {
        ParseError err = formatted.error();
        err.sourcePath = targetPathUtf8;
        return makeUnexpected(std::move(err));
    }
    const std::string& content = *formatted;

    const size_t slashPos = targetPath.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos) {
        std::wstring parentDir = (slashPos == 2 && targetPath[1] == L':')
            ? targetPath.substr(0, 3)
            : targetPath.substr(0, slashPos);
        while (parentDir.size() > 1 && isWinPathSep(parentDir.back())) {
            if (parentDir.size() == 3 && parentDir[1] == L':') {
                break;
            }
            parentDir.pop_back();
        }
        if (!parentDir.empty()) {
            std::string dirErr;
            if (!createDirectoriesRecursiveWin32(parentDir, dirErr)) {
                const std::string parentUtf8 = wideToUtf8(WideStringView(parentDir.data(), parentDir.size()));
                return makeUnexpected(ParseError(
                    ErrorCategory::IoError,
                    SourceLocation(),
                    "Failed to create parent directory '" + parentUtf8 + "': " + dirErr,
                    targetPathUtf8,
                    "TOML I/O error: failed to create parent directory '" + parentUtf8 + "'"
                ));
            }
        }
    }

    const std::wstring tempPath = targetPath + L".__TKP_TEMP__";
    const std::string tempPathUtf8 = wideToUtf8(WideStringView(tempPath.data(), tempPath.size()));

    std::FILE* pFile = nullptr;
#    if defined(_MSC_VER)
    errno = 0;
    const errno_t openRc = ::_wfopen_s(&pFile, tempPath.c_str(), L"wb");
    if (openRc != 0 || pFile == nullptr) {
        const int err = (openRc != 0) ? (int)openRc : errno;
        const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to open temporary file '" + tempPathUtf8 + "' for atomic write: " + errUtf8,
            targetPathUtf8,
            "TOML I/O error: failed to open '" + tempPathUtf8 + "'"
        ));
    }
#    else
    errno = 0;
    pFile = ::_wfopen(tempPath.c_str(), L"wb");
    if (pFile == nullptr) {
        const int err = errno;
        const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to open temporary file '" + tempPathUtf8 + "' for atomic write: " + errUtf8,
            targetPathUtf8,
            "TOML I/O error: failed to open '" + tempPathUtf8 + "'"
        ));
    }
#    endif

    size_t written = 0;
    while (written < content.size()) {
        const size_t n = std::fwrite(content.data() + written, 1, content.size() - written, pFile);
        if (n == 0) {
            break;
        }
        written += n;
    }

    const bool writeOk = (written == content.size()) && (std::ferror(pFile) == 0);
    const bool flushOk = writeOk && (std::fflush(pFile) == 0);
    const bool closeOk = (std::fclose(pFile) == 0);
    if (!flushOk || !closeOk) {
        ::DeleteFileW(tempPath.c_str());
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed while writing temporary file '" + tempPathUtf8 + "'",
            targetPathUtf8,
            "TOML I/O error: write failed on '" + tempPathUtf8 + "'"
        ));
    }

    if (!::MoveFileExW(
        tempPath.c_str(),
        targetPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    )) {
        const DWORD err = ::GetLastError();
        ::DeleteFileW(tempPath.c_str());
        const std::string errUtf8 = formatErrorCodeUtf8(std::error_code((int)err, std::system_category()));
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to atomically rename temporary file onto '" + targetPathUtf8 + "': " + errUtf8,
            targetPathUtf8,
            "TOML I/O error: rename failed onto '" + targetPathUtf8 + "'"
        ));
    }

    return Result<void, ParseError>();
}
#else
inline bool createDirectoriesRecursivePosix(const std::string& dirPath, std::string& outErrUtf8)
{
    if (dirPath.empty()) {
        return true;
    }

    struct stat st;
    if (::stat(dirPath.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        outErrUtf8 = formatErrorCodeUtf8(std::error_code(EEXIST, std::generic_category()));
        return false;
    }

    for (size_t pos = 1; pos <= dirPath.size(); ++pos) {
        const bool atEnd = (pos == dirPath.size());
        if (!atEnd && dirPath[pos] != '/') {
            continue;
        }
        if (dirPath[pos - 1] == '/') {
            continue;
        }

        const std::string sub = dirPath.substr(0, pos);
        if (::stat(sub.c_str(), &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                outErrUtf8 = formatErrorCodeUtf8(std::error_code(ENOTDIR, std::generic_category()));
                return false;
            }
            continue;
        }

        if (::mkdir(sub.c_str(), 0777) != 0) {
            const int err = errno;
            if (err == EEXIST && ::stat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                continue;
            }
            outErrUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
            return false;
        }
    }

    return true;
}

inline Result<void, ParseError> trySaveUtf8PathPosix(
    const std::string& targetPath,
    const Item& doc,
    FormatOptions options
)
{
    if (targetPath.empty()) {
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to open TOML file '' for atomic write",
            "",
            "TOML I/O error: failed to open ''"
        ));
    }
    if (targetPath.find('\0') != std::string::npos) {
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Invalid TOML file path containing null character '" + targetPath + "'",
            targetPath,
            "TOML I/O error: invalid path '" + targetPath + "'"
        ));
    }

    Result<std::string, ParseError> formatted = tryFormat(doc, options);
    if (!formatted.has_value()) {
        ParseError err = formatted.error();
        err.sourcePath = targetPath;
        return makeUnexpected(std::move(err));
    }
    const std::string& content = *formatted;

    const size_t slashPos = targetPath.rfind('/');
    if (slashPos != std::string::npos && slashPos > 0) {
        std::string parentDir = targetPath.substr(0, slashPos);
        while (parentDir.size() > 1 && parentDir.back() == '/') {
            parentDir.pop_back();
        }
        if (!parentDir.empty()) {
            std::string dirErr;
            if (!createDirectoriesRecursivePosix(parentDir, dirErr)) {
                return makeUnexpected(ParseError(
                    ErrorCategory::IoError,
                    SourceLocation(),
                    "Failed to create parent directory '" + parentDir + "': " + dirErr,
                    targetPath,
                    "TOML I/O error: failed to create parent directory '" + parentDir + "'"
                ));
            }
        }
    }

    const std::string tempPath = targetPath + ".__TKP_TEMP__";
    errno = 0;
    std::FILE* pFile = std::fopen(tempPath.c_str(), "wb");
    if (pFile == nullptr) {
        const int err = errno;
        const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to open temporary file '" + tempPath + "' for atomic write: " + errUtf8,
            targetPath,
            "TOML I/O error: failed to open '" + tempPath + "'"
        ));
    }

    size_t written = 0;
    while (written < content.size()) {
        const size_t n = std::fwrite(content.data() + written, 1, content.size() - written, pFile);
        if (n == 0) {
            break;
        }
        written += n;
    }

    const bool writeOk = (written == content.size()) && (std::ferror(pFile) == 0);
    const bool flushOk = writeOk && (std::fflush(pFile) == 0);
    const bool closeOk = (std::fclose(pFile) == 0);
    if (!flushOk || !closeOk) {
        std::remove(tempPath.c_str());
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed while writing temporary file '" + tempPath + "'",
            targetPath,
            "TOML I/O error: write failed on '" + tempPath + "'"
        ));
    }

    if (std::rename(tempPath.c_str(), targetPath.c_str()) != 0) {
        const int err = errno;
        std::remove(tempPath.c_str());
        const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to atomically rename temporary file onto '" + targetPath + "': " + errUtf8,
            targetPath,
            "TOML I/O error: rename failed onto '" + targetPath + "'"
        ));
    }

    return Result<void, ParseError>();
}
#endif // _WIN32

#if TOMLKIT11_HAS_EXCEPTIONS
[[noreturn]] inline void throwSaveAtomicError(const ParseError& err)
{
    if (err.category == ErrorCategory::TypeMismatch) {
        throw TypeError(err.message);
    }
    throw Exception(err.message);
}
#endif

} // namespace

TOMLKIT11_INLINE Result<std::string, ParseError> tryFormat(
    const Item& item,
    FormatOptions options
)
{
    EmitterImpl emitter(options);
    std::string out;
    ParseError err;
    if (!emitter.emitTopLevel(item, out, err)) {
        return makeUnexpected(std::move(err));
    }
    return Result<std::string, ParseError>(std::move(out));
}

TOMLKIT11_INLINE std::string format(
    const Item& item,
    FormatOptions options
)
{
    Result<std::string, ParseError> res = tryFormat(item, options);
    if (!res.has_value()) {
#if TOMLKIT11_HAS_EXCEPTIONS
        throw TypeError(res.error().message);
#else
        TOMLKIT11_ABORT();
#endif
    }
    return std::move(*res);
}

TOMLKIT11_INLINE std::string Item::toToml(FormatOptions options) const
{
    return format(*this, options);
}

TOMLKIT11_INLINE Result<void, ParseError> trySaveFileAtomic(
    StringView utf8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    const std::string utf8Str = detail::toString(utf8FilePath);
#ifdef _WIN32
    if (utf8Str.empty()) {
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Failed to open TOML file '' for atomic write",
            "",
            "TOML I/O error: failed to open ''"
        ));
    }
    if (utf8Str.find('\0') != std::string::npos) {
        return makeUnexpected(ParseError(
            ErrorCategory::IoError,
            SourceLocation(),
            "Invalid TOML file path containing null character '" + utf8Str + "'",
            utf8Str,
            "TOML I/O error: invalid path '" + utf8Str + "'"
        ));
    }
    const std::wstring widePath = utf8ToWide(utf8FilePath);
    return trySaveWidePathWin32(widePath, utf8Str, doc, options);
#else
    return trySaveUtf8PathPosix(utf8Str, doc, options);
#endif
}

TOMLKIT11_INLINE Result<void, ParseError> trySaveFileAtomic(
    WideStringView wideFilePath,
    const Item& doc,
    FormatOptions options
)
{
    const std::string utf8Str = wideToUtf8(wideFilePath);
#ifdef _WIN32
    const std::wstring widePath(wideFilePath.data(), wideFilePath.size());
    return trySaveWidePathWin32(widePath, utf8Str, doc, options);
#else
    return trySaveUtf8PathPosix(utf8Str, doc, options);
#endif
}

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE Result<void, ParseError> trySaveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    const StringView sv((const char*)u8FilePath.data(), u8FilePath.size());
    return trySaveFileAtomic(sv, doc, options);
}
#endif

#if TOMLKIT11_HAS_FILESYSTEM
namespace detail
{
    TOMLKIT11_INLINE Result<void, ParseError> trySavePathAtomicImpl(
        const std::filesystem::path& filePath,
        const Item& doc,
        FormatOptions options
    )
    {
#    ifdef _WIN32
        const std::wstring& native = filePath.native();
        return trySaveFileAtomic(WideStringView(native.data(), native.size()), doc, options);
#    else
        const std::string& native = filePath.native();
        return trySaveFileAtomic(StringView(native.data(), native.size()), doc, options);
#    endif
    }

#    if TOMLKIT11_HAS_EXCEPTIONS
    TOMLKIT11_INLINE void savePathAtomicImpl(
        const std::filesystem::path& filePath,
        const Item& doc,
        FormatOptions options
    )
    {
        const Result<void, ParseError> res = trySavePathAtomicImpl(filePath, doc, options);
        if (!res.has_value()) {
            throwSaveAtomicError(res.error());
        }
    }
#    endif
} // namespace detail
#endif // TOMLKIT11_HAS_FILESYSTEM

#if TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_INLINE void saveFileAtomic(
    StringView utf8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    const Result<void, ParseError> res = trySaveFileAtomic(utf8FilePath, doc, options);
    if (!res.has_value()) {
        throwSaveAtomicError(res.error());
    }
}

TOMLKIT11_INLINE void saveFileAtomic(
    WideStringView wideFilePath,
    const Item& doc,
    FormatOptions options
)
{
    const Result<void, ParseError> res = trySaveFileAtomic(wideFilePath, doc, options);
    if (!res.has_value()) {
        throwSaveAtomicError(res.error());
    }
}

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE void saveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    const Result<void, ParseError> res = trySaveFileAtomic(u8FilePath, doc, options);
    if (!res.has_value()) {
        throwSaveAtomicError(res.error());
    }
}
#endif

#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
