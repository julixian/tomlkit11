#include <tomlkit11/tomlkit11.hpp>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

static_assert(TOMLKIT11_HAS_EXCEPTIONS == 0, "test_no_exceptions must compile with TOMLKIT11_HAS_EXCEPTIONS == 0");

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

    struct NoExPoint
    {
        int x;
        int y;

        NoExPoint(int px = 0, int py = 0)
            : x(px)
            , y(py)
        { }

        friend bool operator==(const NoExPoint& lhs, const NoExPoint& rhs)
        {
            return lhs.x == rhs.x && lhs.y == rhs.y;
        }
    };

    std::string makeTempFilePath()
    {
#ifdef _WIN32
        wchar_t wbuf[MAX_PATH + 2];
        const DWORD len = ::GetTempPathW((DWORD)(sizeof(wbuf) / sizeof(wbuf[0])), wbuf);
        std::string dir = (len > 0 && len < (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])))
            ? tomlkit::wideToUtf8(tomlkit::WideStringView(wbuf, (size_t)len))
            : std::string(".\\");
        if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') {
            dir.push_back('\\');
        }
        return dir + "tomlkit11_no_exceptions_test.toml";
#else
        const char* pTemp = std::getenv("TMPDIR");
        std::string dir = (pTemp != nullptr && pTemp[0] != '\0') ? std::string(pTemp) : std::string("/tmp");
        if (!dir.empty() && dir.back() != '/') {
            dir.push_back('/');
        }
        return dir + "tomlkit11_no_exceptions_test.toml";
#endif
    }

    void removeTempFile(const std::string& utf8Path)
    {
#ifdef _WIN32
        const std::wstring wPath = tomlkit::utf8ToWide(tomlkit::StringView(utf8Path));
        ::DeleteFileW(wPath.c_str());
#else
        std::remove(utf8Path.c_str());
#endif
    }
} // namespace

template<>
struct tomlkit::Serializer<NoExPoint>
{
    static void toToml(const NoExPoint& pt, tomlkit::Item& out)
    {
        out = tomlkit::Item::inlineTable({
            { "x", pt.x },
            { "y", pt.y }
        });
    }

    static NoExPoint fromToml(const tomlkit::Item& item)
    {
        return NoExPoint(item.valueOr("x", 0), item.valueOr("y", 0));
    }
};

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

int main()
{
    using namespace tomlkit;

    // 1. tryParse valid & invalid TOML
    const StringView kSample =
        "# header comment\n"
        "[server]\n"
        "host = \"127.0.0.1\" # host comment\n"
        "ports = [80, 443]\n"
        "enabled = true\n";

    Result<Item, ParseError> parsedRes = tryParse(kSample, ParseOptions(), "sample.toml");
    CHECK(parsedRes.has_value());

    Result<Item, ParseError> badRes = tryParse("port = 0xZZ\n", ParseOptions(), "bad.toml");
    CHECK(!badRes.has_value());
    CHECK(badRes.error().category == ErrorCategory::InvalidNumber);
    CHECK(badRes.error().location.line == 1);
    CHECK(!badRes.error().format().empty());

    Item& doc = *parsedRes;
    const Item& cdoc = doc;

    // 2. tryFind (noexcept), findOr, findOrDefault, findArrayOrEmpty, findTableOrEmpty
    static_assert(
        noexcept(tryFind<std::string>(cdoc, "server", "host")),
        "tryFind must be noexcept under -fno-exceptions"
    );

    const Optional<std::string> hostOpt = tryFind<std::string>(cdoc, "server", "host");
    CHECK(hostOpt.has_value() && *hostOpt == "127.0.0.1");

    const Optional<int> port1Opt = tryFind<int>(cdoc, "server", "ports", 1);
    CHECK(port1Opt.has_value() && *port1Opt == 443);

    CHECK(!tryFind<int>(cdoc, "server", "host").has_value());
    CHECK(!tryFind<int>(cdoc, "server", "missing").has_value());
    CHECK(!tryFind<int>(cdoc, "server", "ports", -1).has_value());
    CHECK(!tryFind<int>(cdoc, "server", "ports", 99).has_value());

    CHECK(findOr(cdoc, "server", "host", "fallback") == "127.0.0.1");
    CHECK(findOr(cdoc, "server", "missing", "fallback") == "fallback");
    CHECK(findOr(cdoc, "server", "ports", 0, -1) == 80);
    CHECK(findOr(cdoc, "server", "ports", 5, -1) == -1);
    CHECK(findOrDefault<int>(cdoc, "server", "missing_int") == 0);
    CHECK(findArrayOrEmpty(cdoc, "missing").isArray());
    CHECK(findTableOrEmpty(cdoc, "missing").isTable());

    // 3. tryGet with STL containers and custom Serializer
    const Item* pPorts = cdoc.find("server", "ports");
    CHECK(pPorts != nullptr);
    const Optional<std::vector<int>> portsVec = pPorts->tryGet<std::vector<int>>();
    CHECK(portsVec.has_value() && portsVec->size() == 2 && (*portsVec)[0] == 80 && (*portsVec)[1] == 443);

    doc.ensure("server", "labels") = Item::inlineTable({ { "env", "prod" } });
    const Item* pLabels = cdoc.find("server", "labels");
    CHECK(pLabels != nullptr);
    const Optional<std::map<std::string, std::string>> labelsMap =
        pLabels->tryGet<std::map<std::string, std::string>>();
    CHECK(labelsMap.has_value() && labelsMap->at("env") == "prod");

    doc.ensure("server", "point") = NoExPoint(10, 20);
    const Optional<NoExPoint> ptOpt = tryFind<NoExPoint>(cdoc, "server", "point");
    CHECK(ptOpt.has_value() && *ptOpt == NoExPoint(10, 20));

    // 4. ensure, findKey, and comment edits
    doc.ensure("workspace", "profile", "fast", "opt") = 0;
    doc.ensure("workspace", "profile", "fast", "opt").setInlineComment("no opt");
    doc.ensure("workspace", "profile", "fast", "opt").addLeadingComment("debug build");

    Item* pFastTbl = doc.find("workspace", "profile", "fast");
    CHECK(pFastTbl != nullptr);
    Key* pOptKey = pFastTbl->findKey("opt");
    CHECK(pOptKey != nullptr && pOptKey->name() == "opt");
    CHECK(pFastTbl->findKey("nonexistent") == nullptr);
    pOptKey->setStyle(KeyStyle::BasicQuoted);

    const Item* pOptVal = cdoc.find("workspace", "profile", "fast", "opt");
    CHECK(pOptVal != nullptr);
    CHECK(pOptVal->inlineComment().has_value() && *pOptVal->inlineComment() == "no opt");
    CHECK(pOptVal->leadingComments().size() == 1 && pOptVal->leadingComments()[0] == "debug build");

    // 5. tryFormat valid & invalid (Empty node)
    const Result<std::string, ParseError> fmtRes = tryFormat(doc);
    CHECK(fmtRes.has_value());
    CHECK(fmtRes->find("\"opt\" = 0 # no opt") != std::string::npos);

    Item invalidDoc = Item::document();
    (void)invalidDoc.ensure("broken", "emptyLeaf");
    const Result<std::string, ParseError> fmtErr = tryFormat(invalidDoc);
    CHECK(!fmtErr.has_value());
    CHECK(fmtErr.error().category == ErrorCategory::TypeMismatch);

    // 6. trySaveFileAtomic & tryParseFile
    const std::string tempPath = makeTempFilePath();
    removeTempFile(tempPath);

    const Result<void, ParseError> saveErr = trySaveFileAtomic(StringView(tempPath), invalidDoc);
    CHECK(!saveErr.has_value());
    CHECK(saveErr.error().category == ErrorCategory::TypeMismatch);

    const Result<void, ParseError> saveOk = trySaveFileAtomic(StringView(tempPath), doc);
    CHECK(saveOk.has_value());

    const Result<Item, ParseError> reloadRes = tryParseFile(StringView(tempPath));
    CHECK(reloadRes.has_value());
    CHECK(*reloadRes == doc);

    removeTempFile(tempPath);

    const Result<Item, ParseError> missingFileRes = tryParseFile("definitely_missing_no_exceptions_file.toml");
    CHECK(!missingFileRes.has_value());
    CHECK(missingFileRes.error().category == ErrorCategory::IoError);

    if (g_failures != 0) {
        std::cerr << "test_no_exceptions failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_no_exceptions: all checks passed!\n";
    return 0;
}
