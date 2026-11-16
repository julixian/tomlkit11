#include <tomlkit11/tomlkit11.hpp>

#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
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

    template<typename Ex, typename Fn>
    void checkThrows(Fn&& fn, tomlkit::StringView expr, int line)
    {
        try {
            fn();
            std::cerr << "[FAIL] line " << line << ": expected exception was not thrown: " << expr << '\n';
            ++g_failures;
        }
        catch (const Ex& ex) {
            if (ex.what()[0] == '\0') {
                std::cerr << "[FAIL] line " << line << ": thrown exception had empty what(): " << expr << '\n';
                ++g_failures;
            }
        }
        catch (const std::exception& ex) {
            std::cerr << "[FAIL] line " << line << ": wrong exception type (" << ex.what() << "): " << expr << '\n';
            ++g_failures;
        }
    }

    std::string makePaddedKey(int idx)
    {
        std::ostringstream oss;
        oss << "key_" << std::setw(3) << std::setfill('0') << idx;
        return oss.str();
    }
} // namespace

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define CHECK_THROWS(ExType, ...) checkThrows<ExType>([&]() { (void)(__VA_ARGS__); }, #__VA_ARGS__, __LINE__)

void testExceptionColdPaths()
{
    using namespace tomlkit;

    // 1. Syntax error throws SyntaxError with accurate line/column and category.
    CHECK_THROWS(SyntaxError, parse("broken = [1, 2"));
    try {
        (void)parse("ok = 1\nbad = 0xGG\n", ParseOptions(), "test.toml");
        CHECK(false);
    }
    catch (const SyntaxError& err) {
        CHECK(err.location().line == 2);
        CHECK(err.error().sourcePath == "test.toml");
        CHECK(!err.error().format().empty());
    }

    // 2. Type mismatch throws TypeError across typed accessors and narrowing get<T>.
    Item doc = parse("flag = true\nnum = -42\nbig = 500\nflt = 3.5\nstr = 'hi'\narr = [10, 20]\n[tbl]\nk = 1\n");
    const Item& cdoc = doc;

    CHECK_THROWS(TypeError, cdoc["flag"].asInteger());
    CHECK_THROWS(TypeError, cdoc["num"].asBoolean());
    CHECK_THROWS(TypeError, cdoc["num"].asFloat());
    CHECK_THROWS(TypeError, cdoc["num"].asString());
    CHECK_THROWS(TypeError, cdoc["num"].asLocalDate());
    CHECK_THROWS(TypeError, cdoc["num"].asLocalTime());
    CHECK_THROWS(TypeError, cdoc["num"].asLocalDateTime());
    CHECK_THROWS(TypeError, cdoc["num"].asOffsetDateTime());
    CHECK_THROWS(TypeError, cdoc["str"].asNumber());

    // Narrowing integer overflow and negative-to-unsigned must fail cleanly.
    CHECK(!cdoc["big"].tryGet<uint8_t>().has_value());
    CHECK(!cdoc["num"].tryGet<uint32_t>().has_value());
    CHECK_THROWS(TypeError, cdoc["big"].get<uint8_t>());
    CHECK_THROWS(TypeError, cdoc["num"].get<uint32_t>());
    CHECK_THROWS(TypeError, cdoc["flt"].get<int>());
    CHECK_THROWS(TypeError, cdoc["str"].get<std::vector<int>>());

    // Unsigned 64-bit integer exceeding INT64_MAX throws OutOfRangeError.
    CHECK_THROWS(OutOfRangeError, Item((std::numeric_limits<uint64_t>::max)()));

    // 3. Key lookup or indexing on non-container items throws TypeError.
    CHECK_THROWS(TypeError, cdoc["num"]["sub"]);
    CHECK_THROWS(TypeError, doc["num"]["sub"]);
    CHECK_THROWS(TypeError, cdoc["num"][0]);
    CHECK_THROWS(TypeError, doc["num"][0]);
    CHECK_THROWS(TypeError, doc["arr"]["not_a_table_key"]);
#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    CHECK_THROWS(TypeError, cdoc["num", "deep", 0]);
#endif

    // 4. Negative array index, out-of-bounds index, and missing key throw OutOfRangeError.
    CHECK_THROWS(OutOfRangeError, doc["arr"][-1]);
    CHECK_THROWS(OutOfRangeError, cdoc["arr"][-5]);
    CHECK_THROWS(OutOfRangeError, cdoc["arr"].at(2));
    CHECK_THROWS(OutOfRangeError, cdoc["tbl"].at("missing"));
    CHECK_THROWS(OutOfRangeError, cdoc["tbl"].keyAt("missing"));
    CHECK_THROWS(OutOfRangeError, find<int>(cdoc, "tbl", "missing"));
    CHECK_THROWS(OutOfRangeError, find<int>(cdoc, "arr", 99));
#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    CHECK_THROWS(OutOfRangeError, doc["arr", -1]);
    CHECK_THROWS(OutOfRangeError, cdoc["tbl", "missing"]);
#endif

    Item emptyArr = Item::array();
    CHECK_THROWS(OutOfRangeError, emptyArr.front());
    CHECK_THROWS(OutOfRangeError, emptyArr.back());
}

void testCopyMoveAndHashTableStress()
{
    using namespace tomlkit;

    Item original = parse(
        "# 顶部注释\n"
        "[server]\n"
        "host = \"127.0.0.1\" # 主机\n"
        "ports = [80, 443]\n"
    );

    // 1. Deep copy independence.
    Item copied = original;
    copied.ensure("server", "host") = "0.0.0.0";
    copied.ensure("server", "host").setInlineComment("新注释");
    copied.ensure("server", "ports").push_back(8080);

    CHECK(original["server"]["host"].asString() == "127.0.0.1");
    CHECK(original["server"]["host"].inlineComment() == "主机");
    CHECK(original["server"]["ports"].size() == 2);
    CHECK(copied["server"]["ports"].size() == 3);

    // 2. Self-copy and move semantics.
    Item& alias = copied;
    copied = alias;
    CHECK(copied["server"]["host"].asString() == "0.0.0.0");

    const std::string expectedDump = copied.toToml();
    Item movedCtor(std::move(copied));
    CHECK(movedCtor.toToml() == expectedDump);

    Item movedAssign = Item::document();
    movedAssign = std::move(movedCtor);
    CHECK(movedAssign.toToml() == expectedDump);

    // 3. Stress open-addressing hash index beyond TableStorage::kHashIndexThreshold (16).
    Item bigTbl = Item::document();
    const int kKeyCount = 64;
    for (int i = 0; i < kKeyCount; ++i) {
        std::pair<Item&, bool> insRes = bigTbl.insertOrAssign(makePaddedKey(i), i * 10);
        CHECK(insRes.second);
        CHECK(insRes.first.asInteger() == i * 10);
    }
    CHECK(bigTbl.size() == (size_t)kKeyCount);

    // Overwriting an existing key preserves insertion order and returns second == false.
    {
        std::pair<Item&, bool> insRes = bigTbl.insertOrAssign("key_010", 9999);
        CHECK(!insRes.second);
        CHECK(insRes.first.asInteger() == 9999);
    }

    // Erase all even keys and verify hash index rebuild and insertion order.
    for (int i = 0; i < kKeyCount; i += 2) {
        CHECK(bigTbl.erase(makePaddedKey(i)));
    }
    CHECK(!bigTbl.erase("non_existent_key"));
    CHECK(bigTbl.size() == (size_t)(kKeyCount / 2));

    int expectedOdd = 1;
    const Item& cBigTbl = bigTbl;
    for (const TableKeyValuePair<true> kv : cBigTbl.tableItems()) {
        CHECK(kv.key.name() == makePaddedKey(expectedOdd));
        const int expectedVal = (expectedOdd == 10) ? 9999 : (expectedOdd * 10);
        CHECK(kv.value.asInteger() == expectedVal);
        CHECK(bigTbl.contains(StringView(kv.key.name())));
        expectedOdd += 2;
    }
    CHECK(expectedOdd == kKeyCount + 1);

#if TOMLKIT11_CPP17
    int sbOdd = 1;
    for (auto [key, val] : cBigTbl.tableItems()) {
        CHECK(key.name() == makePaddedKey(sbOdd));
        CHECK(val.asInteger() == sbOdd * 10);
        sbOdd += 2;
    }
    CHECK(sbOdd == kKeyCount + 1);
#endif

    const Item reparsedBig = parse(bigTbl.toToml());
    CHECK(reparsedBig == bigTbl);

    bigTbl.clear();
    CHECK(bigTbl.empty() && bigTbl.size() == 0);
}

void testExtremeScalarAndControlCharRoundtrip()
{
    using namespace tomlkit;

    Item doc = Item::document();

    // 1. 64-bit integer bounds and radix formatting.
    doc["int_min"] = (std::numeric_limits<int64_t>::min)();
    doc["int_max"] = (std::numeric_limits<int64_t>::max)();
    doc["hex_max"] = (std::numeric_limits<int64_t>::max)();
    doc["hex_max"].setIntegerRadix(IntegerRadix::Hexadecimal);
    doc["oct_val"] = 0755;
    doc["oct_val"].setIntegerRadix(IntegerRadix::Octal);
    doc["bin_val"] = 45;
    doc["bin_val"].setIntegerRadix(IntegerRadix::Binary);
    // Negative integers with Hex radix must fall back safely to decimal.
    doc["neg_hex_fallback"] = -255;
    doc["neg_hex_fallback"].setIntegerRadix(IntegerRadix::Hexadecimal);

    // 2. Extreme floating-point values (-0.0, +inf, -inf, nan, denorm_min, DBL_MAX).
    doc["neg_zero"] = -0.0;
    doc["pos_inf"] = std::numeric_limits<double>::infinity();
    doc["neg_inf"] = -std::numeric_limits<double>::infinity();
    doc["quiet_nan"] = std::numeric_limits<double>::quiet_NaN();
    doc["denorm_min"] = std::numeric_limits<double>::denorm_min();
    doc["dbl_max"] = (std::numeric_limits<double>::max)();

    // 3. Full ASCII control chars (0x00..0x1F, 0x7F) and tricky multiline quote clusters.
    std::string allControls;
    for (int c = 0; c <= 0x1F; ++c) {
        allControls.push_back((char)c);
    }
    allControls.push_back('\x7F');
    doc["controls"] = allControls;

    Item trickyMlBasic("line1 \"\"\" triple quotes \"\"\" inside\nand trailing \"\"");
    trickyMlBasic.setStringStyle(StringStyle::MultilineBasic);
    doc["tricky_ml_basic"] = std::move(trickyMlBasic);

    // MultilineLiteral containing ''' must safely fall back to escaped MultilineBasic.
    Item trickyMlLit("cannot stay raw ''' triple single quotes ''' here");
    trickyMlLit.setStringStyle(StringStyle::MultilineLiteral);
    doc["tricky_ml_lit"] = std::move(trickyMlLit);

    // 4. Extreme timezone offsets (-23:59 / +23:59) with nanosecond precision.
    doc["odt_min_offset"] = OffsetDateTime(
        LocalDate(2024, 2, 29),
        LocalTime(23, 59, 59, 999999999u, 9, true),
        -(23 * 60 + 59),
        false
    );
    doc["odt_max_offset"] = OffsetDateTime(
        LocalDate(2000, 2, 29),
        LocalTime(0, 0, 0, 1u, 9, true),
        +(23 * 60 + 59),
        false
    );

    const SpecVersion specs[2] = { SpecVersion::v1_1, SpecVersion::v1_0 };
    for (size_t i = 0; i < 2; ++i) {
        const SpecVersion spec = specs[i];
        const std::string emitted = doc.toToml(FormatOptions(spec));
        const Result<Item, ParseError> reparsed = tryParse(emitted, ParseOptions(spec));
        if (!reparsed.has_value()) {
            std::cerr << "[FAIL] failed to reparse under spec "
                      << (spec == SpecVersion::v1_1 ? "v1.1" : "v1.0")
                      << ": " << reparsed.error().message << '\n'
                      << emitted << '\n';
            ++g_failures;
            continue;
        }
        CHECK(*reparsed == doc);
        CHECK((*reparsed)["int_min"].asInteger() == (std::numeric_limits<int64_t>::min)());
        CHECK((*reparsed)["int_max"].asInteger() == (std::numeric_limits<int64_t>::max)());
        CHECK((*reparsed)["controls"].asString() == allControls);
        CHECK(std::signbit((*reparsed)["neg_zero"].asFloat()));
        CHECK(std::isnan((*reparsed)["quiet_nan"].asFloat()));
    }

    // Invalid timezone offsets and non-leap-year Feb 29 must be rejected.
    CHECK(!tryParse("dt = 2026-11-14T08:00:00+24:00\n").has_value());
    CHECK(!tryParse("dt = 2026-11-14T08:00:00-00:60\n").has_value());
    CHECK(!tryParse("dt = 1900-02-29\n").has_value());
}

int main()
{
    testExceptionColdPaths();
    testCopyMoveAndHashTableStress();
    testExtremeScalarAndControlCharRoundtrip();

    if (g_failures != 0) {
        std::cerr << "test_api_edge_cases failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_api_edge_cases: all checks passed!\n";
    return 0;
}
