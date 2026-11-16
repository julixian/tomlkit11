#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#ifndef TOMLKIT11_DETAIL_PARSER_IPP
#define TOMLKIT11_DETAIL_PARSER_IPP

#include "../lexer.hpp"
#include "../parser.hpp"

#include <string>
#include <utility>
#include <vector>

TOMLKIT11_NAMESPACE_BEGIN

namespace detail
{

namespace parser_detail
{

inline void sealTableRecursively(Item& item) noexcept
{
    if (TableStorage* pTbl = item.tableStorage()) {
        pTbl->isSealed = true;
        for (size_t i = 0; i < pTbl->entries.size(); ++i) {
            sealTableRecursively(*pTbl->entries[i]->value);
        }
    }
    else if (ArrayStorage* pArr = item.arrayStorage()) {
        for (size_t i = 0; i < pArr->elements.size(); ++i) {
            sealTableRecursively(*pArr->elements[i].value);
        }
    }
}

inline Item scalarPayloadToItem(const ScalarPayload& payload)
{
    switch (payload.kind)
    {
    case ScalarPayload::Kind::Boolean:        return Item(payload.asBoolean());
    case ScalarPayload::Kind::Integer:        return Item(payload.asInteger());
    case ScalarPayload::Kind::Float:          return Item(payload.asFloat());
    case ScalarPayload::Kind::LocalDate:      return Item(payload.asLocalDate());
    case ScalarPayload::Kind::LocalTime:      return Item(payload.asLocalTime());
    case ScalarPayload::Kind::LocalDateTime:  return Item(payload.asLocalDateTime());
    case ScalarPayload::Kind::OffsetDateTime: return Item(payload.asOffsetDateTime());
    default:                                  break;
    }
    TOMLKIT11_ABORT();
}

class ParserImpl
{
public:
    ParserImpl(StringView source, ParseOptions options, StringView sourcePath) noexcept
        : m_lexer(source, options, sourcePath)
        , m_options(options)
    { }

    Result<Item, ParseError> parseDocument()
    {
        Item doc = Item::document();
        TableStorage* const pRoot = doc.tableStorage();
        pRoot->isExplicitlyDefined = true;
        pRoot->dottedKeyScopeId = 1;

        const Result<bool, ParseError> bomRes = m_lexer.validateUtf8AndConsumeBom();
        if (!bomRes) {
            return makeUnexpected(bomRes.error());
        }
        pRoot->hasBom = *bomRes;

        TableStorage* pCurrentSection = pRoot;
        uint32_t currentScopeId = 1;

        while (!m_lexer.isEof()) {
            const Result<StringView, ParseError> leadTriviaRes = m_lexer.consumeWsCommentNewlines();
            if (!leadTriviaRes) {
                return makeUnexpected(leadTriviaRes.error());
            }

            if (m_lexer.isEof()) {
                if (pRoot->entries.empty() && m_nextSectionOrder == 1) {
                    pRoot->headerDecor.prefix = toString(*leadTriviaRes);
                }
                else {
                    pRoot->trailing = toString(*leadTriviaRes);
                }
                break;
            }

            if (m_lexer.peek() == '[') {
                Result<TableStorage*, ParseError> secRes = parseSectionHeader(pRoot, *leadTriviaRes);
                if (!secRes) {
                    return makeUnexpected(std::move(secRes.error()));
                }
                pCurrentSection = *secRes;
                currentScopeId = m_nextScopeId++;
            }
            else {
                Result<void, ParseError> kvRes = parseSectionKeyValue(
                    pCurrentSection,
                    currentScopeId,
                    *leadTriviaRes
                );
                if (!kvRes) {
                    return makeUnexpected(std::move(kvRes.error()));
                }
            }
        }

        return doc;
    }

private:
    Result<TableStorage*, ParseError> parseSectionHeader(TableStorage* pRoot, StringView leadTrivia)
    {
        const size_t headerOffset = m_lexer.position();
        m_lexer.advance(); // consume first '['

        bool isAot = false;
        if (m_lexer.peek() == '[') {
            isAot = true;
            m_lexer.advance(); // consume second '['
        }

        const StringView afterOpenWs = m_lexer.consumeHorizontalWs();
        if (m_lexer.peek() == ']') {
            return makeUnexpected(m_lexer.errorAt(
                ErrorCategory::InvalidKey,
                headerOffset,
                (uint32_t)(m_lexer.position() - headerOffset + 1),
                "Empty table header '[]' is not allowed"
            ));
        }

        Result<std::vector<KeySegment>, ParseError> keySegsRes = m_lexer.lexDottedKey();
        if (!keySegsRes) {
            return makeUnexpected(std::move(keySegsRes.error()));
        }
        std::vector<KeySegment> keySegs = std::move(*keySegsRes);
        keySegs.front().decor.prefix = toString(afterOpenWs);

        if (isAot) {
            if (m_lexer.peek(0) != ']' || m_lexer.peek(1) != ']') {
                return makeUnexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    "Expected closing ']]' for array-of-tables header"
                ));
            }
            m_lexer.advance();
            m_lexer.advance();
        }
        else {
            if (m_lexer.peek() != ']') {
                return makeUnexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    "Expected closing ']' for table header"
                ));
            }
            m_lexer.advance();
        }

        const Result<StringView, ParseError> lineTrailRes = m_lexer.consumeLineTrail();
        if (!lineTrailRes) {
            return makeUnexpected(lineTrailRes.error());
        }

        // Walk or create implicit parent tables along the first N-1 segments.
        TableStorage* pCur = pRoot;
        for (size_t i = 0; i + 1 < keySegs.size(); ++i) {
            const StringView segName = keySegs[i].name;
            const size_t foundIdx = pCur->findIndex(segName);
            if (foundIdx == StringView::npos) {
                Item implicitTbl = Item::table(TableStyle::ImplicitSuper);
                TableStorage* const pSub = implicitTbl.tableStorage();
                pSub->isExplicitlyDefined = false;
                pSub->dottedKeyScopeId = 0;

                const uint32_t order = pCur->nextEntryOrder++;
                pCur->entries.push_back(makeUnique<TableEntry>(
                    Key(toString(segName), keySegs[i].style),
                    std::move(implicitTbl),
                    order
                ));
                pCur->invalidateIndex();
                pCur = pSub;
            }
            else {
                Item& existing = *pCur->entries[foundIdx]->value;
                if (TableStorage* pSubTbl = existing.tableStorage()) {
                    if (pSubTbl->isSealed) {
                        return makeUnexpected(m_lexer.errorAt(
                            ErrorCategory::InlineTableImmutable,
                            headerOffset,
                            (uint32_t)(m_lexer.position() - headerOffset),
                            "Cannot extend inline table '" + toString(segName) + "' via section header"
                        ));
                    }
                    pCur = pSubTbl;
                }
                else if (ArrayStorage* pArr = existing.arrayStorage()) {
                    if (pArr->style != ArrayStyle::ArrayOfTables || pArr->elements.empty()) {
                        return makeUnexpected(m_lexer.errorAt(
                            ErrorCategory::StaticArrayConflict,
                            headerOffset,
                            (uint32_t)(m_lexer.position() - headerOffset),
                            "Cannot append sub-table under static array '" + toString(segName) + "'"
                        ));
                    }
                    pCur = pArr->elements.back().value->tableStorage();
                }
                else {
                    return makeUnexpected(m_lexer.errorAt(
                        ErrorCategory::TableRedefinition,
                        headerOffset,
                        (uint32_t)(m_lexer.position() - headerOffset),
                        "Cannot redefine non-table key '" + toString(segName) + "' as super-table"
                    ));
                }
            }
        }

        const KeySegment& leafSeg = keySegs.back();
        const size_t leafIdx = pCur->findIndex(leafSeg.name);

        if (!isAot) {
            TableStorage* pTarget = nullptr;
            if (leafIdx == StringView::npos) {
                Item stdTbl = Item::table(TableStyle::Standard);
                pTarget = stdTbl.tableStorage();
                const uint32_t order = pCur->nextEntryOrder++;
                pCur->entries.push_back(makeUnique<TableEntry>(
                    Key(leafSeg.name, leafSeg.style),
                    std::move(stdTbl),
                    order
                ));
                pCur->invalidateIndex();
            }
            else {
                Item& existing = *pCur->entries[leafIdx]->value;
                TableStorage* const pExistingTbl = existing.tableStorage();
                if (pExistingTbl == nullptr || pExistingTbl->isSealed || pExistingTbl->isExplicitlyDefined) {
                    return makeUnexpected(m_lexer.errorAt(
                        ErrorCategory::TableRedefinition,
                        headerOffset,
                        (uint32_t)(m_lexer.position() - headerOffset),
                        "Table '" + leafSeg.name + "' cannot be defined more than once"
                    ));
                }
                pExistingTbl->style = TableStyle::Standard;
                pTarget = pExistingTbl;
            }

            pTarget->isExplicitlyDefined = true;
            pTarget->dottedKeyScopeId = 0;
            pTarget->sectionOrder = m_nextSectionOrder++;
            pTarget->headerDecor.prefix = toString(leadTrivia);
            pTarget->headerDecor.suffix = toString(*lineTrailRes);
            pTarget->headerKeyPath = std::move(keySegs);
            return pTarget;
        }

        // [[array_of_tables]]
        ArrayStorage* pAot = nullptr;
        if (leafIdx == StringView::npos) {
            Item aotItem = Item::arrayOfTables();
            pAot = aotItem.arrayStorage();
            const uint32_t order = pCur->nextEntryOrder++;
            pCur->entries.push_back(makeUnique<TableEntry>(
                Key(leafSeg.name, leafSeg.style),
                std::move(aotItem),
                order
            ));
            pCur->invalidateIndex();
        }
        else {
            Item& existing = *pCur->entries[leafIdx]->value;
            pAot = existing.arrayStorage();
            if (pAot == nullptr || pAot->style != ArrayStyle::ArrayOfTables) {
                return makeUnexpected(m_lexer.errorAt(
                    ErrorCategory::StaticArrayConflict,
                    headerOffset,
                    (uint32_t)(m_lexer.position() - headerOffset),
                    "Cannot define array-of-tables '[[" + leafSeg.name + "]]'; key is already defined as "
                        + toString(existing.kindName())
                ));
            }
        }

        Item elemTbl = Item::table(TableStyle::ArrayOfTablesElement);
        TableStorage* const pTarget = elemTbl.tableStorage();
        pTarget->isExplicitlyDefined = true;
        pTarget->dottedKeyScopeId = 0;
        pTarget->sectionOrder = m_nextSectionOrder++;
        pTarget->headerDecor.prefix = toString(leadTrivia);
        pTarget->headerDecor.suffix = toString(*lineTrailRes);
        pTarget->headerKeyPath = std::move(keySegs);

        pAot->elements.emplace_back(std::move(elemTbl));
        return pTarget;
    }

    Result<TableEntry*, ParseError> insertDottedKeyValue(
        TableStorage* pSectionTable,
        uint32_t scopeId,
        std::vector<KeySegment> keySegs,
        Item value,
        size_t statementOffset
    )
    {
        const uint32_t sectionBodyOrder = pSectionTable->nextEntryOrder++;
        TableStorage* pCur = pSectionTable;

        for (size_t i = 0; i + 1 < keySegs.size(); ++i) {
            const StringView segName = keySegs[i].name;
            const size_t foundIdx = pCur->findIndex(segName);

            if (foundIdx == StringView::npos) {
                Item dottedTbl = Item::table(TableStyle::Dotted);
                TableStorage* const pSub = dottedTbl.tableStorage();
                pSub->isExplicitlyDefined = true;
                pSub->dottedKeyScopeId = scopeId;

                pCur->entries.push_back(makeUnique<TableEntry>(
                    Key(toString(segName), keySegs[i].style),
                    std::move(dottedTbl),
                    sectionBodyOrder
                ));
                pCur->invalidateIndex();
                pCur = pSub;
            }
            else {
                Item& existing = *pCur->entries[foundIdx]->value;
                TableStorage* const pSubTbl = existing.tableStorage();
                if (pSubTbl == nullptr) {
                    return makeUnexpected(m_lexer.errorAt(
                        ErrorCategory::DottedKeyConflict,
                        statementOffset,
                        1,
                        "Cannot use dotted key through non-table key '" + toString(segName) + "'"
                    ));
                }
                if (pSubTbl->isSealed) {
                    return makeUnexpected(m_lexer.errorAt(
                        ErrorCategory::InlineTableImmutable,
                        statementOffset,
                        1,
                        "Cannot append keys to closed inline table '" + toString(segName) + "'"
                    ));
                }
                if (pSubTbl->isExplicitlyDefined && pSubTbl->dottedKeyScopeId != scopeId) {
                    return makeUnexpected(m_lexer.errorAt(
                        ErrorCategory::DottedKeyConflict,
                        statementOffset,
                        1,
                        "Cannot use dotted keys to append into table '"
                            + toString(segName) + "' defined outside this block"
                    ));
                }
                // Convert ImplicitSuper to Dotted on first dotted-key assignment inside ancestor section
                // so the serializer does not emit a duplicate [header].
                if (pSubTbl->style == TableStyle::ImplicitSuper) {
                    pSubTbl->style = TableStyle::Dotted;
                }
                pSubTbl->isExplicitlyDefined = true;
                pSubTbl->dottedKeyScopeId = scopeId;
                pCur = pSubTbl;
            }
        }

        KeySegment leafSeg = std::move(keySegs.back());
        keySegs.pop_back();

        if (pCur->findIndex(leafSeg.name) != StringView::npos) {
            const size_t errLen = (leafSeg.rawRepr.size() > 1) ? leafSeg.rawRepr.size() : 1;
            return makeUnexpected(m_lexer.errorAt(
                ErrorCategory::DuplicateKey,
                statementOffset,
                (uint32_t)errLen,
                "Duplicate key '" + leafSeg.name + "'"
            ));
        }

        Key entryKey(std::move(keySegs), std::move(leafSeg));
        pCur->entries.push_back(makeUnique<TableEntry>(
            std::move(entryKey),
            std::move(value),
            sectionBodyOrder
        ));
        pCur->invalidateIndex();
        return pCur->entries.back().get();
    }

    Result<void, ParseError> parseSectionKeyValue(
        TableStorage* pSectionTable,
        uint32_t scopeId,
        StringView leadTrivia
    )
    {
        const size_t stmtOffset = m_lexer.position();
        Result<std::vector<KeySegment>, ParseError> keySegsRes = m_lexer.lexDottedKey();
        if (!keySegsRes) {
            return makeUnexpected(std::move(keySegsRes.error()));
        }
        std::vector<KeySegment> keySegs = std::move(*keySegsRes);
        keySegs.front().decor.prefix = toString(leadTrivia);

        if (m_lexer.peek() != '=') {
            return makeUnexpected(m_lexer.errorHere(
                ErrorCategory::UnexpectedCharacter,
                "Expected '=' after key"
            ));
        }
        m_lexer.advance(); // consume '='

        const StringView afterEqWs = m_lexer.consumeHorizontalWs();
        Result<Item, ParseError> valRes = parseValue(1);
        if (!valRes) {
            return makeUnexpected(std::move(valRes.error()));
        }

        const Result<StringView, ParseError> lineTrailRes = m_lexer.consumeLineTrail();
        if (!lineTrailRes) {
            return makeUnexpected(lineTrailRes.error());
        }

        valRes->decor().prefix = toString(afterEqWs);
        valRes->decor().suffix = toString(*lineTrailRes);

        Result<TableEntry*, ParseError> insertRes = insertDottedKeyValue(
            pSectionTable,
            scopeId,
            std::move(keySegs),
            std::move(*valRes),
            stmtOffset
        );
        if (!insertRes) {
            return makeUnexpected(std::move(insertRes.error()));
        }
        return Result<void, ParseError>();
    }

    Result<Item, ParseError> parseValue(uint32_t depth)
    {
        if (depth > m_options.maxNestingDepth) {
            return makeUnexpected(m_lexer.errorHere(
                ErrorCategory::NestingLimitExceeded,
                "Value exceeds maximum nesting depth ("
                    + uintToString((uint64_t)m_options.maxNestingDepth) + ")"
            ));
        }

        if (m_lexer.isEof()) {
            return makeUnexpected(m_lexer.errorHere(
                ErrorCategory::UnexpectedEof,
                "Unexpected end of file while expecting a value"
            ));
        }

        const char ch = m_lexer.peek();
        if (ch == '"' || ch == '\'') {
            Result<LexedString, ParseError> strRes = m_lexer.lexString();
            if (!strRes) {
                return makeUnexpected(std::move(strRes.error()));
            }
            Item item(std::move(strRes->value));
            item.setStringStyle(strRes->style);
            item.setRawRepr(toString(strRes->rawRepr));
            return item;
        }

        if (ch == '[') {
            return parseArray(depth + 1);
        }

        if (ch == '{') {
            return parseInlineTable(depth + 1);
        }

        Result<LexedScalar, ParseError> scalarRes = m_lexer.lexScalar();
        if (!scalarRes) {
            return makeUnexpected(std::move(scalarRes.error()));
        }

        Item item = scalarPayloadToItem(scalarRes->payload);
        item.setIntegerRadix(scalarRes->integerRadix);
        item.setRawRepr(toString(scalarRes->rawRepr));
        return item;
    }

    Result<Item, ParseError> parseArray(uint32_t depth)
    {
        const size_t openOffset = m_lexer.position();
        m_lexer.advance(); // consume '['

        Item arrItem = Item::array();
        ArrayStorage* const pArr = arrItem.arrayStorage();
        pArr->hasExplicitLayout = true;

        while (true) {
            const Result<StringView, ParseError> prefixRes = m_lexer.consumeWsCommentNewlines();
            if (!prefixRes) {
                return makeUnexpected(prefixRes.error());
            }

            if (m_lexer.isEof()) {
                return makeUnexpected(m_lexer.errorAt(
                    ErrorCategory::UnexpectedEof,
                    openOffset,
                    1,
                    "Unterminated array '['"
                ));
            }

            if (m_lexer.peek() == ']') {
                m_lexer.advance();
                pArr->trailing = toString(*prefixRes);
                if (svContains(pArr->trailing, '\n')) {
                    pArr->multiline = true;
                }
                return arrItem;
            }

            if (m_lexer.peek() == ',') {
                return makeUnexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    "Unexpected comma ',' in array without preceding element"
                ));
            }

            if (svContains(*prefixRes, '\n')) {
                pArr->multiline = true;
            }

            Result<Item, ParseError> elemValRes = parseValue(depth);
            if (!elemValRes) {
                return makeUnexpected(std::move(elemValRes.error()));
            }
            elemValRes->decor().prefix = toString(*prefixRes);

            const Result<StringView, ParseError> postValTriviaRes = m_lexer.consumeWsCommentNewlines();
            if (!postValTriviaRes) {
                return makeUnexpected(postValTriviaRes.error());
            }

            if (m_lexer.isEof()) {
                return makeUnexpected(m_lexer.errorAt(
                    ErrorCategory::UnexpectedEof,
                    openOffset,
                    1,
                    "Unterminated array '['"
                ));
            }

            ArrayElement elem(std::move(*elemValRes));

            if (m_lexer.peek() == ',') {
                m_lexer.advance();
                elem.hasComma = true;
                elem.beforeComma = toString(*postValTriviaRes);

                const Result<StringView, ParseError> afterCommaRes = m_lexer.consumeAfterCommaSameLine();
                if (!afterCommaRes) {
                    return makeUnexpected(afterCommaRes.error());
                }
                elem.afterComma = toString(*afterCommaRes);
                if (svContains(elem.beforeComma, '\n') || svContains(elem.afterComma, '\n')) {
                    pArr->multiline = true;
                }
                pArr->elements.push_back(std::move(elem));
                continue;
            }

            if (m_lexer.peek() == ']') {
                m_lexer.advance();
                elem.hasComma = false;

                // If postValTrivia spans multiple lines, first line belongs to element beforeComma,
                // and remaining lines belong to container trailing.
                const StringView postSv = *postValTriviaRes;
                const size_t firstNl = postSv.find('\n');
                if (firstNl != StringView::npos) {
                    pArr->multiline = true;
                    elem.beforeComma = toString(postSv.substr(0, firstNl + 1));
                    pArr->trailing = toString(postSv.substr(firstNl + 1));
                }
                else {
                    pArr->trailing = toString(postSv);
                }

                pArr->elements.push_back(std::move(elem));
                return arrItem;
            }

            std::string msg = "Expected ',' or ']' in array, found '";
            msg.push_back(m_lexer.peek());
            msg.push_back('\'');
            return makeUnexpected(m_lexer.errorHere(
                ErrorCategory::UnexpectedCharacter,
                std::move(msg)
            ));
        }
    }

    Result<Item, ParseError> parseInlineTable(uint32_t depth)
    {
        const size_t openOffset = m_lexer.position();
        m_lexer.advance(); // consume '{'

        Item tblItem = Item::inlineTable();
        TableStorage* const pTbl = tblItem.tableStorage();
        const uint32_t inlineScopeId = m_nextScopeId++;
        pTbl->isExplicitlyDefined = true;
        pTbl->dottedKeyScopeId = inlineScopeId;

        const auto checkV10TriviaFunc = [&](StringView trivia, size_t offset) -> Result<void, ParseError>
            {
                if (m_options.spec == SpecVersion::v1_0 &&
                    (svContains(trivia, '\n') || svContains(trivia, '#')))
                {
                    const size_t len = (trivia.size() > 1) ? trivia.size() : 1;
                    return makeUnexpected(m_lexer.errorAt(
                        ErrorCategory::SpecRestriction,
                        offset,
                        (uint32_t)len,
                        "Newlines and comments inside inline tables require TOML v1.1"
                    ));
                }
                return Result<void, ParseError>();
            };

        bool afterTrailingComma = false;

        while (true) {
            const size_t triviaStart = m_lexer.position();
            const Result<StringView, ParseError> prefixRes = m_lexer.consumeWsCommentNewlines();
            if (!prefixRes) {
                return makeUnexpected(prefixRes.error());
            }
            Result<void, ParseError> v10PrefixRes = checkV10TriviaFunc(*prefixRes, triviaStart);
            if (!v10PrefixRes) {
                return makeUnexpected(std::move(v10PrefixRes.error()));
            }

            if (m_lexer.isEof()) {
                return makeUnexpected(m_lexer.errorAt(
                    ErrorCategory::UnexpectedEof,
                    openOffset,
                    1,
                    "Unterminated inline table '{'"
                ));
            }

            if (m_lexer.peek() == '}') {
                if (afterTrailingComma && m_options.spec == SpecVersion::v1_0) {
                    return makeUnexpected(m_lexer.errorHere(
                        ErrorCategory::SpecRestriction,
                        "Trailing comma in inline table requires TOML v1.1"
                    ));
                }
                m_lexer.advance();
                pTbl->trailing = toString(*prefixRes);
                if (svContains(pTbl->trailing, '\n')) {
                    pTbl->multilineInline = true;
                }
                sealTableRecursively(tblItem);
                return tblItem;
            }

            if (m_lexer.peek() == ',') {
                return makeUnexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    "Unexpected comma ',' in inline table"
                ));
            }

            if (svContains(*prefixRes, '\n')) {
                pTbl->multilineInline = true;
            }

            const size_t kvOffset = m_lexer.position();
            Result<std::vector<KeySegment>, ParseError> keySegsRes = m_lexer.lexDottedKey();
            if (!keySegsRes) {
                return makeUnexpected(std::move(keySegsRes.error()));
            }
            std::vector<KeySegment> keySegs = std::move(*keySegsRes);
            keySegs.front().decor.prefix = toString(*prefixRes);

            if (m_lexer.peek() != '=') {
                return makeUnexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    "Expected '=' after key in inline table"
                ));
            }
            m_lexer.advance(); // consume '='

            const StringView afterEqWs = m_lexer.consumeHorizontalWs();
            Result<Item, ParseError> valRes = parseValue(depth);
            if (!valRes) {
                return makeUnexpected(std::move(valRes.error()));
            }
            valRes->decor().prefix = toString(afterEqWs);

            const size_t postTriviaStart = m_lexer.position();
            const Result<StringView, ParseError> postValTriviaRes = m_lexer.consumeWsCommentNewlines();
            if (!postValTriviaRes) {
                return makeUnexpected(postValTriviaRes.error());
            }
            Result<void, ParseError> v10PostRes = checkV10TriviaFunc(*postValTriviaRes, postTriviaStart);
            if (!v10PostRes) {
                return makeUnexpected(std::move(v10PostRes.error()));
            }

            if (m_lexer.isEof()) {
                return makeUnexpected(m_lexer.errorAt(
                    ErrorCategory::UnexpectedEof,
                    openOffset,
                    1,
                    "Unterminated inline table '{'"
                ));
            }

            Result<TableEntry*, ParseError> entryRes = insertDottedKeyValue(
                pTbl,
                inlineScopeId,
                std::move(keySegs),
                std::move(*valRes),
                kvOffset
            );
            if (!entryRes) {
                return makeUnexpected(std::move(entryRes.error()));
            }
            TableEntry* const pEntry = *entryRes;

            if (m_lexer.peek() == ',') {
                m_lexer.advance();
                pEntry->hasComma = true;
                pEntry->beforeComma = toString(*postValTriviaRes);

                const size_t afterCommaStart = m_lexer.position();
                const Result<StringView, ParseError> afterCommaRes = m_lexer.consumeAfterCommaSameLine();
                if (!afterCommaRes) {
                    return makeUnexpected(afterCommaRes.error());
                }
                Result<void, ParseError> v10AfterRes = checkV10TriviaFunc(*afterCommaRes, afterCommaStart);
                if (!v10AfterRes) {
                    return makeUnexpected(std::move(v10AfterRes.error()));
                }
                pEntry->afterComma = toString(*afterCommaRes);
                if (svContains(pEntry->beforeComma, '\n') || svContains(pEntry->afterComma, '\n')) {
                    pTbl->multilineInline = true;
                }
                afterTrailingComma = true;
                continue;
            }

            if (m_lexer.peek() == '}') {
                m_lexer.advance();
                pEntry->hasComma = false;

                const StringView postSv = *postValTriviaRes;
                const size_t firstNl = postSv.find('\n');
                if (firstNl != StringView::npos) {
                    pTbl->multilineInline = true;
                    pEntry->beforeComma = toString(postSv.substr(0, firstNl + 1));
                    pTbl->trailing = toString(postSv.substr(firstNl + 1));
                }
                else {
                    pTbl->trailing = toString(postSv);
                }

                sealTableRecursively(tblItem);
                return tblItem;
            }

            std::string msg = "Expected ',' or '}' in inline table, found '";
            msg.push_back(m_lexer.peek());
            msg.push_back('\'');
            return makeUnexpected(m_lexer.errorHere(
                ErrorCategory::UnexpectedCharacter,
                std::move(msg)
            ));
        }
    }

    Lexer m_lexer;
    ParseOptions m_options;
    uint32_t m_nextSectionOrder = 1;
    uint32_t m_nextScopeId = 2;
};

} // namespace parser_detail

} // namespace detail

TOMLKIT11_INLINE Result<Item, ParseError> tryParse(
    StringView utf8Source,
    ParseOptions options,
    StringView sourcePath
)
{
    detail::parser_detail::ParserImpl parser(utf8Source, options, sourcePath);
    return parser.parseDocument();
}

TOMLKIT11_INLINE Result<Item, ParseError> tryParseFile(
    StringView utf8FilePath,
    ParseOptions options
)
{
    Result<std::string, ParseError> fileRes = detail::readBinaryFileUtf8Path(utf8FilePath);
    if (!fileRes) {
        return makeUnexpected(std::move(fileRes.error()));
    }
    return tryParse(StringView(fileRes->data(), fileRes->size()), options, utf8FilePath);
}

TOMLKIT11_INLINE Result<Item, ParseError> tryParseFile(
    WideStringView wideFilePath,
    ParseOptions options
)
{
    std::string utf8Path;
    Result<std::string, ParseError> fileRes = detail::readBinaryFileWidePath(wideFilePath, &utf8Path);
    if (!fileRes) {
        return makeUnexpected(std::move(fileRes.error()));
    }
    return tryParse(StringView(fileRes->data(), fileRes->size()), options, utf8Path);
}

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE Result<Item, ParseError> tryParseFile(
    std::u8string_view u8FilePath,
    ParseOptions options
)
{
    const StringView utf8Path(
        u8FilePath.empty() ? "" : (const char*)u8FilePath.data(),
        u8FilePath.size()
    );
    return tryParseFile(utf8Path, options);
}
#endif

#if TOMLKIT11_HAS_FILESYSTEM
namespace detail
{
    TOMLKIT11_INLINE Result<Item, ParseError> tryParsePathImpl(
        const std::filesystem::path& filePath,
        ParseOptions options
    )
    {
#ifdef _WIN32
        std::string utf8Path;
        Result<std::string, ParseError> fileRes = readBinaryFileWidePath(
            WideStringView(filePath.native().data(), filePath.native().size()),
            &utf8Path
        );
        if (!fileRes) {
            return makeUnexpected(std::move(fileRes.error()));
        }
        return tryParse(StringView(fileRes->data(), fileRes->size()), options, utf8Path);
#else
        const std::string utf8Path = pathToUtf8(filePath);
        Result<std::string, ParseError> fileRes = readBinaryFileUtf8Path(utf8Path);
        if (!fileRes) {
            return makeUnexpected(std::move(fileRes.error()));
        }
        return tryParse(StringView(fileRes->data(), fileRes->size()), options, utf8Path);
#endif
    }
} // namespace detail
#endif // TOMLKIT11_HAS_FILESYSTEM

#if TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_INLINE Item parse(
    StringView utf8Source,
    ParseOptions options,
    StringView sourcePath
)
{
    Result<Item, ParseError> res = tryParse(utf8Source, options, sourcePath);
    if (!res) {
        throw SyntaxError(std::move(res.error()));
    }
    return std::move(*res);
}

TOMLKIT11_INLINE Item parseFile(
    StringView utf8FilePath,
    ParseOptions options
)
{
    Result<Item, ParseError> res = tryParseFile(utf8FilePath, options);
    if (!res) {
        throw SyntaxError(std::move(res.error()));
    }
    return std::move(*res);
}

TOMLKIT11_INLINE Item parseFile(
    WideStringView wideFilePath,
    ParseOptions options
)
{
    Result<Item, ParseError> res = tryParseFile(wideFilePath, options);
    if (!res) {
        throw SyntaxError(std::move(res.error()));
    }
    return std::move(*res);
}

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
TOMLKIT11_INLINE Item parseFile(
    std::u8string_view u8FilePath,
    ParseOptions options
)
{
    Result<Item, ParseError> res = tryParseFile(u8FilePath, options);
    if (!res) {
        throw SyntaxError(std::move(res.error()));
    }
    return std::move(*res);
}
#endif

#if TOMLKIT11_HAS_FILESYSTEM
namespace detail
{
    TOMLKIT11_INLINE Item parsePathImpl(
        const std::filesystem::path& filePath,
        ParseOptions options
    )
    {
        Result<Item, ParseError> res = tryParsePathImpl(filePath, options);
        if (!res) {
            throw SyntaxError(std::move(res.error()));
        }
        return std::move(*res);
    }
} // namespace detail
#endif // TOMLKIT11_HAS_FILESYSTEM

#endif // TOMLKIT11_HAS_EXCEPTIONS

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_DETAIL_PARSER_IPP

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
