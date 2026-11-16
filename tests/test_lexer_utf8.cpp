#include <tomlkit11/tomlkit11.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;

void check(bool cond, tomlkit::StringView expr, int line)
{
    if (!cond) {
        std::cerr << "[FAIL] line " << line << ": " << expr << '\n';
        ++g_failures;
    }
}

} // namespace

#define CHECK(expr) check((expr), #expr, __LINE__)

void testUtf8Validation()
{
    using namespace tomlkit;

    CHECK(isValidUtf8("hello 世界 🚀"));

    // Isolated continuation byte
    size_t errPos = 999;
    CHECK(!isValidUtf8("abc\x80z", &errPos));
    CHECK(errPos == 3);

    // 2-byte overlong encoding (\xC0\xAF)
    CHECK(!isValidUtf8("\xC0\xAF"));

    // 3-byte overlong encoding (\xE0\x80\xAF)
    CHECK(!isValidUtf8("\xE0\x80\xAF"));

    // UTF-16 surrogates U+D800 (\xED\xA0\x80) and U+DFFF (\xED\xBF\xBF)
    CHECK(!isValidUtf8("\xED\xA0\x80"));
    CHECK(!isValidUtf8("\xED\xBF\xBF"));

    // 4-byte codepoint exceeding U+10FFFF (\xF4\x90\x80\x80)
    CHECK(!isValidUtf8("\xF4\x90\x80\x80"));

    // UTF-16 BOM
    CHECK(!isValidUtf8("\xFF\xFE"));
    CHECK(!isValidUtf8("\xFE\xFF"));

    // UTF-8 BOM recognition and consumption
    detail::Lexer bomLexer("\xEF\xBB\xBFkey = 1");
    const Result<bool, ParseError> bomRes = bomLexer.validateUtf8AndConsumeBom();
    CHECK(bomRes.has_value() && *bomRes == true);
    CHECK(bomLexer.position() == 3);
}

void testStringsAndV1_1Escapes()
{
    using namespace tomlkit;

    // v1.1 \e and \xHH (\xE9 is U+00E9, 2 UTF-8 bytes \xC3\xA9)
    {
        detail::Lexer lex("\"\\e[31m\\x00\\x61\\xE9\"");
        const Result<detail::LexedString, ParseError> res = lex.lexString();
        CHECK(res.has_value());
        std::string expected;
        expected.append("\x1B[31m");
        expected.push_back('\0');
        expected.push_back('a');
        expected.append("\xC3\xA9");
        CHECK(res->value == expected);
        CHECK(res->style == StringStyle::Basic);
        CHECK(res->rawRepr == "\"\\e[31m\\x00\\x61\\xE9\"");
    }

    // v1.0 mode rejects \e and \xHH
    {
        detail::Lexer lex1("\"\\e\"", ParseOptions::v1_0());
        const Result<detail::LexedString, ParseError> r1 = lex1.lexString();
        CHECK(!r1.has_value() && r1.error().category == ErrorCategory::SpecRestriction);

        detail::Lexer lex2("\"\\x41\"", ParseOptions::v1_0());
        const Result<detail::LexedString, ParseError> r2 = lex2.lexString();
        CHECK(!r2.has_value() && r2.error().category == ErrorCategory::SpecRestriction);
    }

    // Reject surrogate \uD800 and out-of-range \U00110000
    {
        detail::Lexer lex1("\"\\uD800\"");
        CHECK(!lex1.lexString().has_value());

        detail::Lexer lex2("\"\\U00110000\"");
        CHECK(!lex2.lexString().has_value());
    }

    // Multi-line basic string: strip initial CRLF, backslash continuation, trailing quotes, CRLF->LF
    {
        const StringView src = "\"\"\"\r\nhello \\\r\n   \r\n  world\r\nend\"\"\"\"\"";
        detail::Lexer lex(src);
        const Result<detail::LexedString, ParseError> res = lex.lexString();
        CHECK(res.has_value());
        CHECK(res->value == "hello world\nend\"\"");
        CHECK(res->rawRepr == src);
    }

    // Multi-line literal string: CRLF->LF while rawRepr preserves CRLF
    {
        const StringView src = "'''\r\nline1\\n\r\nline2''''";
        detail::Lexer lex(src);
        const Result<detail::LexedString, ParseError> res = lex.lexString();
        CHECK(res.has_value());
        CHECK(res->value == "line1\\n\nline2'");
        CHECK(res->rawRepr == src);
    }

    // Bare \r must be rejected
    {
        detail::Lexer lex("\"\"\"\nhello\rworld\"\"\"");
        const Result<detail::LexedString, ParseError> res = lex.lexString();
        CHECK(!res.has_value() && res.error().category == ErrorCategory::ControlCharacter);
    }
}

void testNumbers()
{
    using namespace tomlkit;

    const auto lexNumFunc = [](StringView s) -> Result<detail::LexedScalar, ParseError>
        {
            detail::Lexer lex(s);
            return lex.lexScalar();
        };

    // 64-bit signed integer min/max bounds
    {
        const Result<detail::LexedScalar, ParseError> rMax = lexNumFunc("9_223_372_036_854_775_807");
        CHECK(
            rMax.has_value() &&
            rMax->payload.isInteger() &&
            rMax->payload.asInteger() == (std::numeric_limits<int64_t>::max)()
        );

        const Result<detail::LexedScalar, ParseError> rMin = lexNumFunc("-9_223_372_036_854_775_808");
        CHECK(
            rMin.has_value() &&
            rMin->payload.isInteger() &&
            rMin->payload.asInteger() == (std::numeric_limits<int64_t>::min)()
        );

        CHECK(!lexNumFunc("9223372036854775808").has_value());
        CHECK(!lexNumFunc("-9223372036854775809").has_value());
        CHECK(!lexNumFunc("0x8000_0000_0000_0000").has_value());
    }

    // Radix prefixes
    {
        const Result<detail::LexedScalar, ParseError> rHex = lexNumFunc("0x00dead_BEEF");
        CHECK(
            rHex.has_value() &&
            rHex->payload.isInteger() &&
            rHex->payload.asInteger() == 0xdeadbeefLL &&
            rHex->integerRadix == IntegerRadix::Hexadecimal
        );

        const Result<detail::LexedScalar, ParseError> rOct = lexNumFunc("0o0755");
        CHECK(
            rOct.has_value() &&
            rOct->payload.isInteger() &&
            rOct->payload.asInteger() == 0755LL &&
            rOct->integerRadix == IntegerRadix::Octal
        );

        const Result<detail::LexedScalar, ParseError> rBin = lexNumFunc("0b0010_1010");
        CHECK(
            rBin.has_value() &&
            rBin->payload.isInteger() &&
            rBin->payload.asInteger() == 42LL &&
            rBin->integerRadix == IntegerRadix::Binary
        );

        CHECK(!lexNumFunc("+0x10").has_value());
        CHECK(!lexNumFunc("-0b10").has_value());
        CHECK(!lexNumFunc("0X10").has_value());
        CHECK(!lexNumFunc("01").has_value());
        CHECK(!lexNumFunc("1__2").has_value());
        CHECK(!lexNumFunc("0x_1").has_value());
    }

    // Floats, special values, and overflow/underflow
    {
        const Result<detail::LexedScalar, ParseError> rNegZero = lexNumFunc("-0.0");
        CHECK(rNegZero.has_value() && rNegZero->payload.isFloat());
        const double nz = rNegZero->payload.asFloat();
        CHECK(nz == 0.0 && std::signbit(nz));

        const Result<detail::LexedScalar, ParseError> rInf = lexNumFunc("-inf");
        CHECK(
            rInf.has_value() &&
            rInf->payload.isFloat() &&
            std::isinf(rInf->payload.asFloat()) &&
            rInf->payload.asFloat() < 0
        );

        const Result<detail::LexedScalar, ParseError> rNan = lexNumFunc("+nan");
        CHECK(
            rNan.has_value() &&
            rNan->payload.isFloat() &&
            std::isnan(rNan->payload.asFloat())
        );

        const Result<detail::LexedScalar, ParseError> rExp = lexNumFunc("+1_0.2_5e+002");
        CHECK(
            rExp.has_value() &&
            rExp->payload.isFloat() &&
            rExp->payload.asFloat() == 1025.0
        );

        const Result<detail::LexedScalar, ParseError> rHuge = lexNumFunc("1e500");
        CHECK(
            rHuge.has_value() &&
            rHuge->payload.isFloat() &&
            std::isinf(rHuge->payload.asFloat()) &&
            rHuge->payload.asFloat() > 0
        );

        const Result<detail::LexedScalar, ParseError> rNegHuge = lexNumFunc("-1e500");
        CHECK(
            rNegHuge.has_value() &&
            rNegHuge->payload.isFloat() &&
            std::isinf(rNegHuge->payload.asFloat()) &&
            rNegHuge->payload.asFloat() < 0
        );

        const Result<detail::LexedScalar, ParseError> rUnder = lexNumFunc("-1e-500");
        CHECK(
            rUnder.has_value() &&
            rUnder->payload.isFloat() &&
            rUnder->payload.asFloat() == 0.0 &&
            std::signbit(rUnder->payload.asFloat())
        );

        CHECK(!lexNumFunc(".5").has_value());
        CHECK(!lexNumFunc("5.").has_value());
        CHECK(!lexNumFunc("5.e2").has_value());
        CHECK(!lexNumFunc("INF").has_value());
        CHECK(!lexNumFunc("NaN").has_value());
    }
}

void testDatesAndTimes()
{
    using namespace tomlkit;

    // v1.1 omitted seconds and space-separated date-time
    {
        detail::Lexer lex1("14:15 # comment");
        const Result<detail::LexedScalar, ParseError> r1 = lex1.lexScalar();
        CHECK(r1.has_value() && r1->payload.isLocalTime());
        const LocalTime& lt = r1->payload.asLocalTime();
        CHECK(lt.hour == 14 && lt.minute == 15 && lt.second == 0 && !lt.hasSeconds);
        CHECK(lt == LocalTime(14, 15, 0));
        CHECK(lt.toString(false) == "14:15");
        CHECK(lt.toString(true) == "14:15:00");

        detail::Lexer lex2("2026-11-14 22:30 # trailing comment");
        const Result<detail::LexedScalar, ParseError> r2 = lex2.lexScalar();
        CHECK(r2.has_value() && r2->payload.isLocalDateTime());
        const LocalDateTime& ldt = r2->payload.asLocalDateTime();
        CHECK(ldt.date == LocalDate(2026, 11, 14));
        CHECK(ldt.time == LocalTime(22, 30, 0));
        CHECK(ldt.delimiter == ' ');

        // Date followed by space + comment (not digit) must lex as pure LocalDate.
        detail::Lexer lex3("2026-11-14 # birthday");
        const Result<detail::LexedScalar, ParseError> r3 = lex3.lexScalar();
        CHECK(r3.has_value() && r3->payload.isLocalDate());

        // v1.0 rejects omitted seconds
        detail::Lexer lexV10("14:15", ParseOptions::v1_0());
        const Result<detail::LexedScalar, ParseError> rV10 = lexV10.lexScalar();
        CHECK(!rV10.has_value() && rV10.error().category == ErrorCategory::SpecRestriction);
    }

    // Leap year and timezone offset equivalence
    {
        detail::Lexer leapOk("2000-02-29");
        CHECK(leapOk.lexScalar().has_value());

        detail::Lexer leapBad("2100-02-29");
        CHECK(!leapBad.lexScalar().has_value());

        detail::Lexer odt1("2026-11-14T08:00:00.123456+08:00");
        detail::Lexer odt2("2026-11-14t00:00:00.123456z");
        const Result<detail::LexedScalar, ParseError> a = odt1.lexScalar();
        const Result<detail::LexedScalar, ParseError> b = odt2.lexScalar();
        CHECK(a.has_value() && b.has_value());
        CHECK(a->payload.isOffsetDateTime() && b->payload.isOffsetDateTime());
        CHECK(a->payload.asOffsetDateTime() == b->payload.asOffsetDateTime());
    }
}

void testKeysAndDecor()
{
    using namespace tomlkit;

    // 3.14 in key domain must lex as two bare keys "3" and "14"
    {
        detail::Lexer lex("3.14 = 1");
        const Result<std::vector<KeySegment>, ParseError> segs = lex.lexDottedKey();
        CHECK(segs.has_value() && segs->size() == 2);
        CHECK((*segs)[0].name == "3" && (*segs)[1].name == "14");
    }

    // Mixed quotes and whitespace in dotted keys
    {
        detail::Lexer lex("\"site\" . 'google.com' . port = 443");
        const Result<std::vector<KeySegment>, ParseError> segs = lex.lexDottedKey();
        CHECK(segs.has_value() && segs->size() == 3);
        CHECK((*segs)[0].name == "site" && (*segs)[0].decor.suffix == " ");
        CHECK((*segs)[1].name == "google.com" && (*segs)[1].decor.prefix == " " && (*segs)[1].decor.suffix == " ");
        CHECK((*segs)[2].name == "port" && (*segs)[2].decor.prefix == " " && (*segs)[2].decor.suffix == " ");
    }

    // Decor comment extraction and editing
    {
        Decor d("\n  # 第一行注释\n  # 第二行注释\n  ", " # 初始行尾注释\r\n");
        const std::vector<std::string> leading = d.leadingComments();
        CHECK(leading.size() == 2 && leading[0] == "第一行注释" && leading[1] == "第二行注释");
        CHECK(d.inlineComment() == "初始行尾注释");

        d.addLeadingComment("新增第三行");
        CHECK(d.prefix == "\n  # 第一行注释\n  # 第二行注释\n  # 新增第三行\n  ");

        d.setInlineComment("更新后的注释");
        CHECK(d.suffix == " # 更新后的注释\r\n");

        d.clearInlineComment();
        CHECK(d.suffix == "\r\n");
    }
}

int main()
{
    testUtf8Validation();
    testStringsAndV1_1Escapes();
    testNumbers();
    testDatesAndTimes();
    testKeysAndDecor();

    if (g_failures != 0) {
        std::cerr << "test_lexer_utf8 failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_lexer_utf8: all checks passed!\n";
    return 0;
}
