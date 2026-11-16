#include <tomlkit11/tomlkit11.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if TOMLKIT11_HAS_FILESYSTEM
#    include <filesystem>
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

void verifyLosslessString(tomlkit::StringView label, tomlkit::StringView input)
{
    const tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res =
        tomlkit::tryParse(input, tomlkit::ParseOptions(), label);
    if (!res.has_value()) {
        std::cerr << "[FAIL] " << label << " failed to parse:\n"
                  << res.error().format() << '\n';
        ++g_failures;
        return;
    }

    const std::string output = res->toToml();
    const std::string expected = tomlkit::detail::toString(input);
    if (output != expected) {
        std::cerr << "[FAIL] " << label << " round-trip mismatch!\n"
                  << "--- EXPECTED (" << expected.size() << " bytes) ---\n" << expected << '\n'
                  << "--- ACTUAL (" << output.size() << " bytes) ---\n" << output << '\n';
        ++g_failures;
    }
}

void testTortureRoundtripCases()
{
    // 1. Out-of-order sub-tables [a.c] -> [b] -> [a], interleaved dotted keys,
    //    different quote styles on key segments, and trailing orphan comments.
    const char* const kOutOfOrder =
        "# 文件头第一段注释\n"
        "# 文件头第二段注释\n"
        "\n"
        "\"root\" . x = 0xdead_beef # 十六进制字面量\n"
        "middle = 1_000.50e-2\n"
        "root.y = '''raw\n"
        "multiline\n"
        "string'''\n"
        "\n"
        "# 先定义子表 [alpha.child]\n"
        "[ alpha . \"child\" ] # 表头行尾注释\n"
        "k1 = true\n"
        "\n"
        "# 中间穿插无关表 [beta]\n"
        "[beta]\n"
        "arr = [\n"
        "  # 元素 1 前注释\n"
        "  1 , # 元素 1 逗号后注释\n"
        "  # 元素 2 前注释\n"
        "  2 # 无尾随逗号的行尾注释\n"
        "  # 闭合括号前孤立注释\n"
        "]\n"
        "\n"
        "# 最后才定义父表 [alpha]\n"
        "[alpha]\n"
        "self_key = 0b1010_0101\n"
        "\n"
        "[[products]] # 第一个表数组元素\n"
        "name = \"Hammer\"\n"
        "\n"
        "[products.meta]\n"
        "weight = 738\n"
        "\n"
        "[[products]] # 第二个表数组元素\n"
        "\n"
        "[[products]]\n"
        "name = 'Nail'\n"
        "\n"
        "# 文件末尾孤立注释块\n"
        "# 完\n";

    verifyLosslessString("out-of-order-torture", kOutOfOrder);

    // 2. CRLF line endings + UTF-8 BOM + no trailing newline
    const char* const kCrlfBomNoFinalNl =
        "\xEF\xBB\xBF# CRLF BOM header\r\n"
        "a   =   123 # keep custom spaces\r\n"
        "\r\n"
        "[sec]\r\n"
        "b = { x = 1,\r\n"
        "  # multiline inline table with CRLF\r\n"
        "  y = 14:30,\r\n"
        "}";

    verifyLosslessString("crlf-bom-no-final-nl", kCrlfBomNoFinalNl);

    // 3. Empty and comment-only documents
    verifyLosslessString("empty-doc", "");
    verifyLosslessString("comment-only-doc", "# 仅有注释的模板文件\n\n# 第二段注释");

    // 4. Programmatically constructed multiline strings starting with '\n'
    {
        tomlkit::Item doc = tomlkit::Item::document();
        tomlkit::Item mlBasic("\nleading newline basic\nline2");
        mlBasic.setStringStyle(tomlkit::StringStyle::MultilineBasic);
        doc["ml_basic"] = std::move(mlBasic);

        tomlkit::Item mlLiteral("\nleading newline literal\nline2");
        mlLiteral.setStringStyle(tomlkit::StringStyle::MultilineLiteral);
        doc["ml_literal"] = std::move(mlLiteral);

        const tomlkit::Item reparsed = tomlkit::parse(tomlkit::format(doc));
        CHECK(reparsed["ml_basic"].asString() == "\nleading newline basic\nline2");
        CHECK(reparsed["ml_literal"].asString() == "\nleading newline literal\nline2");
    }

    // 5. Programmatic integers (all radices + INT64 bounds) and IEEE-754 float edge cases
    {
        tomlkit::Item doc = tomlkit::Item::document();
        doc["dec_min"] = (std::numeric_limits<tomlkit::int64_t>::min)();
        doc["dec_max"] = (std::numeric_limits<tomlkit::int64_t>::max)();
        doc["hex_val"] = 0xCAFE;
        doc["hex_val"].setIntegerRadix(tomlkit::IntegerRadix::Hexadecimal);
        doc["oct_val"] = 0755;
        doc["oct_val"].setIntegerRadix(tomlkit::IntegerRadix::Octal);
        doc["bin_val"] = 42;
        doc["bin_val"].setIntegerRadix(tomlkit::IntegerRadix::Binary);

        doc["f_tenth"] = 0.1;
        doc["f_sum"] = 0.1 + 0.2;
        doc["f_third"] = 1.0 / 3.0;
        doc["f_whole"] = 42.0;
        doc["f_neg_zero"] = -0.0;
        doc["f_pos_inf"] = std::numeric_limits<double>::infinity();
        doc["f_neg_inf"] = -std::numeric_limits<double>::infinity();
        doc["f_nan"] = std::numeric_limits<double>::quiet_NaN();
        doc["f_neg_nan"] = -std::numeric_limits<double>::quiet_NaN();

        const std::string emitted = tomlkit::format(doc);
        CHECK(emitted.find("hex_val = 0xcafe") != std::string::npos);
        CHECK(emitted.find("oct_val = 0o755") != std::string::npos);
        CHECK(emitted.find("bin_val = 0b101010") != std::string::npos);
        CHECK(emitted.find("f_tenth = 0.1\n") != std::string::npos);
        CHECK(emitted.find("f_whole = 42.0\n") != std::string::npos);
        CHECK(emitted.find("f_neg_zero = -0.0\n") != std::string::npos);

        const tomlkit::Item reparsed = tomlkit::parse(emitted);
        CHECK(reparsed["dec_min"].asInteger() == (std::numeric_limits<tomlkit::int64_t>::min)());
        CHECK(reparsed["dec_max"].asInteger() == (std::numeric_limits<tomlkit::int64_t>::max)());
        CHECK(reparsed["hex_val"].asInteger() == 0xCAFE);
        CHECK(reparsed["oct_val"].asInteger() == 0755);
        CHECK(reparsed["bin_val"].asInteger() == 42);
        CHECK(reparsed["f_tenth"].asFloat() == 0.1);
        CHECK(reparsed["f_sum"].asFloat() == (0.1 + 0.2));
        CHECK(reparsed["f_third"].asFloat() == (1.0 / 3.0));
        CHECK(reparsed["f_whole"].asFloat() == 42.0);
        CHECK(reparsed["f_neg_zero"].asFloat() == 0.0 && std::signbit(reparsed["f_neg_zero"].asFloat()));
        CHECK(std::isinf(reparsed["f_pos_inf"].asFloat()) && !std::signbit(reparsed["f_pos_inf"].asFloat()));
        CHECK(std::isinf(reparsed["f_neg_inf"].asFloat()) && std::signbit(reparsed["f_neg_inf"].asFloat()));
        CHECK(std::isnan(reparsed["f_nan"].asFloat()));
        CHECK(std::isnan(reparsed["f_neg_nan"].asFloat()) && std::signbit(reparsed["f_neg_nan"].asFloat()));
    }
}

void testRealWorldWorkspaceTomlFiles()
{
#if TOMLKIT11_HAS_FILESYSTEM
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path probe = fs::current_path(ec);
    fs::path workspaceParent;
    for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
        if (fs::exists(probe / "tomlkitpp" / "tomlkitpp" / "mcpp.toml", ec)) {
            workspaceParent = probe;
            break;
        }
        probe = probe.parent_path();
    }

    if (workspaceParent.empty()) {
        return;
    }

    const fs::path tkpRoot = workspaceParent / "tomlkitpp";
    const fs::path gppRoot = workspaceParent / "GalTranslPP";
    const std::vector<fs::path> realFiles = {
        tkpRoot / "mcpp.toml",
        tkpRoot / "tomlkitpp" / "mcpp.toml",
        tkpRoot / "tools" / "toml-test-adapter" / "mcpp.toml",
        tkpRoot / "templates" / "quickstart" / "template.toml",
        gppRoot / "mcpp.toml",
        gppRoot / "Example" / "BaseConfig" / "GlobalConfig.toml",
        gppRoot / "Example" / "BaseConfig" / "Prompt.toml",
        gppRoot / "Example" / "SampleProject" / "Config.toml"
    };

    size_t testedCount = 0;
    for (size_t i = 0; i < realFiles.size(); ++i) {
        const fs::path& p = realFiles[i];
        if (!fs::exists(p, ec)) {
            continue;
        }
        std::ifstream ifs(p, std::ios::binary);
        const std::string raw(
            (std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>()
        );
        verifyLosslessString(tomlkit::pathToUtf8(p), raw);
        ++testedCount;
    }
    CHECK(testedCount >= 3);
#endif
}

int main()
{
    testTortureRoundtripCases();
    testRealWorldWorkspaceTomlFiles();

    if (g_failures != 0) {
        std::cerr << "test_roundtrip failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_roundtrip: all checks passed!\n";
    return 0;
}
