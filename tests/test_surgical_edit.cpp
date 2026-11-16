#include <tomlkit11/tomlkit11.hpp>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#if TOMLKIT11_HAS_FILESYSTEM
#    include <filesystem>
#endif

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
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

std::wstring makeUniqueTempDirWide()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH + 1];
    const DWORD len = ::GetTempPathW(MAX_PATH, buf);
    std::wstring base = (len > 0 && len <= MAX_PATH) ? std::wstring(buf, (size_t)len) : std::wstring(L".\\");
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/') {
        base.push_back(L'\\');
    }
    const unsigned long pid = (unsigned long)::GetCurrentProcessId();
    const std::wstring suffix = tomlkit::utf8ToWide("tomlkit11_测试_テスト_🚀_")
        + tomlkit::utf8ToWide(tomlkit::detail::uintToString((tomlkit::uint64_t)pid));
    return base + suffix;
#else
    const char* pTmp = std::getenv("TMPDIR");
    std::string base = (pTmp != nullptr && pTmp[0] != '\0') ? std::string(pTmp) : std::string("/tmp");
    if (!base.empty() && base.back() != '/') {
        base.push_back('/');
    }
    const unsigned long pid = (unsigned long)::getpid();
    const std::string fullUtf8 = base + "tomlkit11_测试_テスト_🚀_"
        + tomlkit::detail::uintToString((tomlkit::uint64_t)pid);
    return tomlkit::utf8ToWide(fullUtf8);
#endif
}

void removeFileAndDirIfPossible(const std::wstring& filePathW, const std::wstring& dirPathW)
{
#ifdef _WIN32
    ::DeleteFileW(filePathW.c_str());
    ::RemoveDirectoryW(dirPathW.c_str());
#else
    const std::string fileUtf8 = tomlkit::wideToUtf8(tomlkit::WideStringView(filePathW.data(), filePathW.size()));
    const std::string dirUtf8 = tomlkit::wideToUtf8(tomlkit::WideStringView(dirPathW.data(), dirPathW.size()));
    std::remove(fileUtf8.c_str());
    ::rmdir(dirUtf8.c_str());
#endif
}

} // namespace

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void testSurgicalSingleLineEdit()
{
    using namespace tomlkit;

    const char* const kOriginal =
        "# 全局构建配置\n"
        "[workspace]\n"
        "mask   = 0xFF00 # 保持十六进制不被动\n"
        "opt    = 0      # 日常调试关闭优化\n"
        "target = 'x86_64-pc-windows-msvc' # 单引号字面量\n"
        "\n"
        "# 发布配置节\n"
        "[profile.release]\n"
        "lto = true # 开启链接时优化\n";

    Item doc = parse(kOriginal);

    // Modifying workspace.opt preserves spacing and inline comment on that line and all other lines.
    doc["workspace"]["opt"] = 3;

    const char* const kExpectedAfterOptChange =
        "# 全局构建配置\n"
        "[workspace]\n"
        "mask   = 0xFF00 # 保持十六进制不被动\n"
        "opt    = 3      # 日常调试关闭优化\n"
        "target = 'x86_64-pc-windows-msvc' # 单引号字面量\n"
        "\n"
        "# 发布配置节\n"
        "[profile.release]\n"
        "lto = true # 开启链接时优化\n";

    CHECK(doc.toToml() == kExpectedAfterOptChange);

    // Appending a new key to [workspace] with leading & inline comments keeps [profile.release] below intact.
    doc.ensure("workspace", "debug") = true;
    doc["workspace"]["debug"].addLeadingComment("是否生成调试符号");
    doc["workspace"]["debug"].setInlineComment("保留 PDB");

    const char* const kExpectedAfterInsert =
        "# 全局构建配置\n"
        "[workspace]\n"
        "mask   = 0xFF00 # 保持十六进制不被动\n"
        "opt    = 3      # 日常调试关闭优化\n"
        "target = 'x86_64-pc-windows-msvc' # 单引号字面量\n"
        "# 是否生成调试符号\n"
        "debug = true # 保留 PDB\n"
        "\n"
        "# 发布配置节\n"
        "[profile.release]\n"
        "lto = true # 开启链接时优化\n";

    CHECK(doc.toToml() == kExpectedAfterInsert);

#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    Item doc23 = parse(kOriginal);
    doc23["workspace", "opt"] = 3;
    CHECK(doc23.toToml() == kExpectedAfterOptChange);

    doc23["workspace", "debug"] = true;
    doc23["workspace", "debug"].addLeadingComment("是否生成调试符号");
    doc23["workspace", "debug"].setInlineComment("保留 PDB");
    CHECK(doc23.toToml() == kExpectedAfterInsert);
#endif
}

void testArrayCommentPreservationOnEraseAndPush()
{
    using namespace tomlkit;

    const char* const kSrc =
        "items = [\n"
        "  \"alpha\", # 第一项注释\n"
        "  \"beta\", # 第二项注释\n"
        "  \"gamma\", # 第三项注释\n"
        "]\n";

    Item doc = parse(kSrc);
    doc["items"].erase(1);
    doc["items"].push_back("delta");

    const char* const kExpected =
        "items = [\n"
        "  \"alpha\", # 第一项注释\n"
        "  \"gamma\", # 第三项注释\n"
        "  \"delta\",\n"
        "]\n";

    CHECK(doc.toToml() == kExpected);
}

void testAtomicFileSaveAndReload()
{
    using namespace tomlkit;

    const std::wstring tempDirWide = makeUniqueTempDirWide();
#ifdef _WIN32
    const std::wstring sep = L"\\";
#else
    const std::wstring sep = L"/";
#endif
    const std::wstring targetFileWide = tempDirWide + sep + utf8ToWide("配置_設定_✨.toml");
    const std::string targetFileUtf8 = wideToUtf8(WideStringView(targetFileWide.data(), targetFileWide.size()));

    Item doc = Item::document();
    doc.addLeadingComment("自动生成的测试配置");
    doc["app"]["name"] = "tomlkit11";
    doc["app"]["hexId"] = 255;
    doc["app"]["hexId"].setIntegerRadix(IntegerRadix::Hexadecimal);
    doc["app"]["hexId"].setInlineComment("十六进制输出");

    // 1. Save via UTF-8 StringView and reload via WideStringView
    saveFileAtomic(targetFileUtf8, doc);
    const Item reloadedFromWide = parseFile(targetFileWide);
    CHECK(reloadedFromWide == doc);
    CHECK(reloadedFromWide.toToml().find("0xff # 十六进制输出") != std::string::npos);

    // 2. Save via WideStringView and reload via UTF-8 StringView
    doc["app"]["version"] = "1.0.0";
    saveFileAtomic(targetFileWide, doc);
    const Item reloadedFromUtf8 = parseFile(targetFileUtf8);
    CHECK(reloadedFromUtf8 == doc);

    // 3. Verify trySaveFileAtomic handles error categories cleanly without throwing
    const Result<void, ParseError> emptyItemSave = trySaveFileAtomic(targetFileUtf8, Item());
    CHECK(!emptyItemSave.has_value());
    CHECK(emptyItemSave.error().category == ErrorCategory::TypeMismatch);

    const Result<void, ParseError> emptyPathSave = trySaveFileAtomic("", doc);
    CHECK(!emptyPathSave.has_value());
    CHECK(emptyPathSave.error().category == ErrorCategory::IoError);

    // 4. Verify non-existent Unicode path error keeps valid UTF-8
    const std::string missingUtf8 = wideToUtf8(WideStringView(tempDirWide.data(), tempDirWide.size()))
        + "/不存在_存在しない_🔍.toml";
    const Result<Item, ParseError> missingRes = tryParseFile(missingUtf8);
    CHECK(!missingRes.has_value());
    CHECK(isValidUtf8(missingRes.error().message));
    CHECK(missingRes.error().sourcePath.find("不存在_存在しない_🔍.toml") != std::string::npos);

#if TOMLKIT11_HAS_FILESYSTEM
    {
        const std::filesystem::path fsTarget = utf8ToPath(targetFileUtf8);
        doc["app"]["fs_path"] = true;
        saveFileAtomic(fsTarget, doc);
        const Item reloadedFs = parseFile(fsTarget);
        CHECK(reloadedFs == doc);

        const Result<void, ParseError> tryFsSave = trySaveFileAtomic(fsTarget, doc);
        CHECK(tryFsSave.has_value());
    }
#endif

#if TOMLKIT11_HAS_CHAR8_T && defined(__cpp_lib_char8_t)
    {
        std::u8string u8Target(
            (const char8_t*)targetFileUtf8.data(),
            targetFileUtf8.size()
        );
        doc["app"]["char8_path"] = true;
        saveFileAtomic(std::u8string_view(u8Target), doc);
        const Item reloadedU8 = parseFile(std::u8string_view(u8Target));
        CHECK(reloadedU8 == doc);
    }
#endif

    removeFileAndDirIfPossible(targetFileWide, tempDirWide);
}

void testSubtreeArrayOfTablesExtractAndGraft()
{
    using namespace tomlkit;

    const char* const kOriginal =
        "[backend]\n"
        "timeout = 30\n"
        "\n"
        "[plugins.Epub]\n"
        "bilingualOutput = true\n"
        "\n"
        "[[plugins.Epub.preprocRegex]]\n"
        "# 保留的注释\n"
        "org = '<ruby>(.+?)</ruby>'\n"
        "rep = '$1'\n"
        "\n"
        "[[plugins.Epub.postprocRegex]]\n"
        "\n"
        "[plugins.SkipTrans]\n"
        "skipH = false\n";

    Item projectConfig = parse(kOriginal);

    // 1. Extracting a nested AoT to a temp document remaps headers to [[preprocRegex]].
    Item preRegexDoc = Item::document();
    preRegexDoc["preprocRegex"] = findArrayOrEmpty(projectConfig, "plugins", "Epub", "preprocRegex");
    const std::string subToml = format(preRegexDoc);
    CHECK(subToml.find("[[preprocRegex]]") != std::string::npos);
    CHECK(subToml.find("[[plugins.Epub.preprocRegex]]") == std::string::npos);
    CHECK(subToml.find("# 保留的注释") != std::string::npos);

    // 2. Editing and grafting back restores full header path and section order.
    const std::string editedSubToml = subToml +
        "\n[[preprocRegex]]\n"
        "org = 'foo'\n"
        "rep = 'bar'\n";
    Item reparsedSub = parse(editedSubToml);
    projectConfig["plugins"]["Epub"]["preprocRegex"] = reparsedSub["preprocRegex"];

    const std::string finalToml = format(projectConfig);
    CHECK(finalToml.find("[[plugins.Epub.preprocRegex]]") != std::string::npos);
    CHECK(finalToml.find("\n[[preprocRegex]]") == std::string::npos);
    CHECK(finalToml.find("# 保留的注释") != std::string::npos);
    CHECK(finalToml.find("org = 'foo'") != std::string::npos);

    const size_t posBackend = finalToml.find("[backend]");
    const size_t posEpub = finalToml.find("[plugins.Epub]");
    const size_t posPre = finalToml.find("[[plugins.Epub.preprocRegex]]");
    const size_t posPost = finalToml.find("[[plugins.Epub.postprocRegex]]");
    const size_t posSkip = finalToml.find("[plugins.SkipTrans]");
    CHECK(posBackend < posEpub);
    CHECK(posEpub < posPre);
    CHECK(posPre < posPost);
    CHECK(posPost < posSkip);

#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    Item cfg23 = parse(kOriginal);
    cfg23["plugins", "Epub", "preprocRegex"] = reparsedSub["preprocRegex"];
    CHECK(format(cfg23) == finalToml);
#endif
}

int main()
{
    testSurgicalSingleLineEdit();
    testArrayCommentPreservationOnEraseAndPush();
    testAtomicFileSaveAndReload();
    testSubtreeArrayOfTablesExtractAndGraft();

    if (g_failures != 0) {
        std::cerr << "test_surgical_edit failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_surgical_edit: all checks passed!\n";
    return 0;
}
