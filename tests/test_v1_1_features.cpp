#include <tomlkit11/tomlkit11.hpp>

#include <iostream>
#include <string>

#if TOMLKIT11_HAS_FILESYSTEM
#    include <filesystem>
#endif

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
#    include <string_view>
#endif

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

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void testMultilineInlineTablesAndTrailingComma()
{
    using namespace tomlkit;

    const char* const kSrc = R"(# TOML v1.1 多行内联表测试
endpoint = {
    # 主机配置
    host = "127.0.0.1", # 本地回环
    port = 8080,
    nested = {
        a.b = 1,
        a.c = [
            10,
            20, # 数组尾随逗号
        ], # 内嵌内联表尾随逗号
    }, # 顶层内联表尾随逗号
}
)";

    const Item doc = parse(kSrc);
    CHECK(doc["endpoint"]["host"].asString() == "127.0.0.1");
    CHECK(doc["endpoint"]["port"].asInteger() == 8080);
    CHECK(doc["endpoint"]["nested"]["a"]["b"].asInteger() == 1);
    CHECK(doc["endpoint"]["nested"]["a"]["c"][1].asInteger() == 20);

#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    CHECK(doc["endpoint", "host"].asString() == "127.0.0.1");
    CHECK(doc["endpoint", "port"].asInteger() == 8080);
    CHECK(doc["endpoint", "nested", "a", "b"].asInteger() == 1);
    CHECK(doc["endpoint", "nested", "a", "c", 1].asInteger() == 20);
#endif

    // Byte-for-byte lossless roundtrip
    CHECK(doc.toToml() == kSrc);

    // Parsing under v1.0 mode must report SpecRestriction
    const Result<Item, ParseError> v10Res = tryParse(kSrc, ParseOptions::v1_0());
    CHECK(!v10Res.has_value() && v10Res.error().category == ErrorCategory::SpecRestriction);

    // Single-line inline table trailing comma: allowed in v1.1, rejected in v1.0
    CHECK(tryParse("x = { a = 1, }").has_value());
    const Result<Item, ParseError> v10Trailing = tryParse("x = { a = 1, }", ParseOptions::v1_0());
    CHECK(!v10Trailing.has_value() && v10Trailing.error().category == ErrorCategory::SpecRestriction);
}

void testV1_1StringsAndOptionalSeconds()
{
    using namespace tomlkit;

    const char* const kSrc = R"(ansi = "\e[1;32mOK\e[0m"
latin = "\x41\xE9\x00"
alarm = 07:30
meeting = 2026-11-14 09:45
utc = 2026-11-14T09:45Z
offset = 2026-11-14 09:45+08:00
)";

    const Item doc = parse(kSrc);
    CHECK(doc["ansi"].asString() == "\x1B[1;32mOK\x1B[0m");
    CHECK(doc["alarm"].asLocalTime() == LocalTime(7, 30, 0));
    CHECK(!doc["alarm"].asLocalTime().hasSeconds);
    CHECK(doc["meeting"].asLocalDateTime().time == LocalTime(9, 45, 0));
    CHECK(doc["utc"].asOffsetDateTime().time == LocalTime(9, 45, 0));
    CHECK(doc["offset"].asOffsetDateTime().offsetMinutes == 480);

    // Default v1.1 lossless output
    CHECK(doc.toToml() == kSrc);

    // Downgrade export to v1.0-compatible TOML and verify strict v1.0 parser accepts it
    const std::string downgraded = doc.toToml(FormatOptions::v1_0());
    const Result<Item, ParseError> parsedByV10 = tryParse(downgraded, ParseOptions::v1_0());
    CHECK(parsedByV10.has_value());
    CHECK(*parsedByV10 == doc);
}

void testTableAndDottedKeyConflictRules()
{
    using namespace tomlkit;

    // Valid: dotted key under [fruit] defines apple, followed by [[fruit.apple.seeds]]
    {
        const char* const kValid = R"([fruit]
apple.color = "red"

[[fruit.apple.seeds]]
size = 2
)";
        const Result<Item, ParseError> res = tryParse(kValid);
        CHECK(res.has_value());
        CHECK((*res)["fruit"]["apple"]["color"].asString() == "red");
        CHECK((*res)["fruit"]["apple"]["seeds"][0]["size"].asInteger() == 2);
#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
        CHECK((*res)["fruit", "apple", "color"].asString() == "red");
        CHECK((*res)["fruit", "apple", "seeds", 0, "size"].asInteger() == 2);
#endif
        CHECK(res->toToml() == kValid);
    }

    // Valid: define sub-table [x.y.z] before explicit super-table [x]
    {
        const char* const kValidSuper = R"([x.y.z]
a = 1

[x]
b = 2
)";
        const Result<Item, ParseError> res = tryParse(kValidSuper);
        CHECK(res.has_value());
        CHECK((*res)["x"]["y"]["z"]["a"].asInteger() == 1);
        CHECK((*res)["x"]["b"].asInteger() == 2);
        CHECK(res->toToml() == kValidSuper);
    }

    // Valid regression case: deep [a.b.c.f] creates implicit supers, then [a] writes dotted b.c.d.k.t
    {
        const char* const kImplicitSuperThenDotted =
            "# header comment\n"
            "\n"
            "[a.b.c.f]\n"
            "  z = 9\n"
            "\n"
            "[a]\n"
            "  first = 1\n"
            "  b.c.d.k.t = \"nested via implicit super\"\n"
            "  last = 2\n";
        const Result<Item, ParseError> res = tryParse(kImplicitSuperThenDotted);
        CHECK(res.has_value());
        CHECK((*res)["a"]["b"]["c"]["f"]["z"].asInteger() == 9);
        CHECK((*res)["a"]["b"]["c"]["d"]["k"]["t"].asString() == "nested via implicit super");
        CHECK(res->toToml() == kImplicitSuperThenDotted);
        const Result<Item, ParseError> reparsed = tryParse(res->toToml());
        CHECK(reparsed.has_value() && *reparsed == *res);
    }

    // Invalid: reopening dotted-key table via [t1.t2]
    CHECK(!tryParse("[t1]\nt2.t3.v = 0\n[t1.t2]\n").has_value());

    // Invalid: extending explicitly defined [a.b.c] via dotted keys in later block (#859)
    CHECK(!tryParse("[a.b.c]\nz = 9\n[a]\nb.c.t = 1\n").has_value());

    // Invalid: appending keys or sub-tables to closed inline table
    CHECK(!tryParse("a = { b = 1 }\na.c = 2\n").has_value());
    CHECK(!tryParse("a = { b = 1 }\n[a.c]\nx = 1\n").has_value());
    CHECK(!tryParse("a = { k1 = 1, k1.name = 'joe' }\n").has_value());

    // Invalid: static array conflicting with [[aot]]
    CHECK(!tryParse("arr = [{ x = 1 }]\n[[arr]]\nx = 2\n").has_value());

    // Invalid: defining [a] twice
    CHECK(!tryParse("[a.b]\n[a]\n[a]\n").has_value());
}

void testFileOverloadResolution()
{
    using namespace tomlkit;

    const char* const pCstr = "__tomlkit11_nonexistent_file__.toml";
    const std::string stdPath(pCstr);
    const StringView svPath(pCstr);
    const wchar_t* const pWcstr = L"__tomlkit11_nonexistent_file__.toml";
    const std::wstring widePath(pWcstr);

    const Result<Item, ParseError> r1 = tryParseFile(pCstr);
    const Result<Item, ParseError> r2 = tryParseFile(stdPath);
    const Result<Item, ParseError> r3 = tryParseFile(svPath);
    const Result<Item, ParseError> r4 = tryParseFile(pWcstr);
    const Result<Item, ParseError> r5 = tryParseFile(widePath);

    CHECK(!r1.has_value() && r1.error().category == ErrorCategory::IoError);
    CHECK(!r2.has_value() && r2.error().category == ErrorCategory::IoError);
    CHECK(!r3.has_value() && r3.error().category == ErrorCategory::IoError);
    CHECK(!r4.has_value() && r4.error().category == ErrorCategory::IoError);
    CHECK(!r5.has_value() && r5.error().category == ErrorCategory::IoError);

#if TOMLKIT11_HAS_FILESYSTEM
    const std::filesystem::path fsPath(pCstr);
    const Result<Item, ParseError> rFs = tryParseFile(fsPath);
    CHECK(!rFs.has_value() && rFs.error().category == ErrorCategory::IoError);
#endif

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
    const char8_t rawU8[] = {
        '_', '_', 'n', 'o', 'n', 'e', 'x', 'i', 's', 't', '.', 't', 'o', 'm', 'l', 0
    };
    const Result<Item, ParseError> rU8 = tryParseFile(std::u8string_view(rawU8));
    CHECK(!rU8.has_value() && rU8.error().category == ErrorCategory::IoError);
#endif
}

int main()
{
    testMultilineInlineTablesAndTrailingComma();
    testV1_1StringsAndOptionalSeconds();
    testTableAndDottedKeyConflictRules();
    testFileOverloadResolution();

    if (g_failures != 0) {
        std::cerr << "test_v1_1_features failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_v1_1_features: all checks passed!\n";
    return 0;
}
