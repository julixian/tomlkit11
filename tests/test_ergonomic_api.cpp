#include <tomlkit11/tomlkit11.hpp>

#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
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

    std::string makeIndexedKey(int index)
    {
        std::ostringstream oss;
        oss << 'k' << index;
        return oss.str();
    }

    struct EndpointConfig
    {
        std::string url;
        int timeoutSec;
        bool stream;

        EndpointConfig(std::string u = std::string(), int t = 30, bool s = true)
            : url(std::move(u))
            , timeoutSec(t)
            , stream(s)
        { }

        friend bool operator==(const EndpointConfig& lhs, const EndpointConfig& rhs)
        {
            return lhs.url == rhs.url
                && lhs.timeoutSec == rhs.timeoutSec
                && lhs.stream == rhs.stream;
        }

        friend bool operator!=(const EndpointConfig& lhs, const EndpointConfig& rhs)
        {
            return !(lhs == rhs);
        }
    };
} // namespace

template<>
struct tomlkit::Serializer<EndpointConfig>
{
    static void toToml(const EndpointConfig& cfg, tomlkit::Item& out)
    {
        out = tomlkit::Item::inlineTable({
            { "url", cfg.url },
            { "timeoutSec", cfg.timeoutSec },
            { "stream", cfg.stream }
        });
    }

    static EndpointConfig fromToml(const tomlkit::Item& item)
    {
        return EndpointConfig(
            item.at("url").get<std::string>(),
            item.valueOr("timeoutSec", 30),
            item.valueOr("stream", true)
        );
    }
};

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void testAutoVivificationAndMultidimensionalSubscript()
{
    using namespace tomlkit;

    Item doc = Item::document();
    doc.ensure("workspace", "package", "version") = "1.0.0";
    doc.ensure("workspace", "profile", "release", "opt") = 3;
    doc.ensure("workspace", "profile", "release", "lto") = true;
    doc.ensure("workspace", "members") = { "core", "cli", "gui" };

    // Keep { "name", 42 } as a 2-element array rather than coercing it to a table.
    doc.ensure("nameTable", "アリス") = { "爱丽丝", 42 };

    const Item& cdoc = doc;
    CHECK(cdoc["workspace"]["profile"]["release"]["opt"].get<int>() == 3);
    CHECK(cdoc["workspace"]["members"][1].get<StringView>() == "cli");

    // Mixed string keys and array indices with findOr.
    const std::string transName = findOr(cdoc, "nameTable", "アリス", 0, "");
    const int count = findOr(cdoc, "nameTable", "アリス", 1, 0);
    const std::string missing = findOr(cdoc, "nameTable", "不存在的角色", 0, "默认译名");
    CHECK(transName == "爱丽丝");
    CHECK(count == 42);
    CHECK(missing == "默认译名");

#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    doc["cxx23", "profile", "release", "opt"] = 3;
    CHECK((cdoc["cxx23", "profile", "release", "opt"].get<int>() == 3));
    CHECK((cdoc["workspace", "members", 1].get<StringView>() == "cli"));
    doc.erase("cxx23");
#endif

    // Self-referential assignment stability across table growth.
    for (int i = 0; i < 40; ++i) {
        doc[makeIndexedKey(i)] = i;
    }
    doc["copiedFromExisting"] = doc["k0"];
    CHECK(doc["copiedFromExisting"].get<int>() == 0);
    CHECK(doc["k39"].get<int>() == 39);

    const std::string text = doc.toToml();
    const Item reparsed = parse(text);
    CHECK(reparsed == doc);
}

void testStlContainerConversionsAndCustomSerializer()
{
    using namespace tomlkit;

    Item doc = Item::document();
    doc["ports"] = { 80, 443, 8080 };
    doc["labels"] = Item::table({
        { "env", "prod" },
        { "region", "ap-east" }
    });

    const EndpointConfig ep("https://api.example.com/v1", 60, false);
    doc["endpoint"] = ep;

    std::vector<int> ports;
    doc["ports"].getTo(ports);
    std::vector<int> expectedPorts;
    expectedPorts.push_back(80);
    expectedPorts.push_back(443);
    expectedPorts.push_back(8080);
    CHECK(ports == expectedPorts);

    const std::map<std::string, std::string> labels =
        doc["labels"].get<std::map<std::string, std::string>>();
    CHECK(labels.at("env") == "prod" && labels.at("region") == "ap-east");

    // Verify C++11 TableKeyValuePair iteration.
    size_t kvCount = 0;
    const Item& cdoc = doc;
    for (const TableKeyValuePair<true> kv : cdoc["labels"].tableItems()) {
        CHECK(!kv.key.name().empty());
        CHECK(kv.value.isString());
        ++kvCount;
    }
    CHECK(kvCount == 2);

#if TOMLKIT11_CPP17
    // Verify C++17 structured bindings support on tableItems().
    size_t sbCount = 0;
    for (const auto [k, v] : cdoc["labels"].tableItems()) {
        CHECK(!k.name().empty());
        CHECK(v.isString());
        ++sbCount;
    }
    CHECK(sbCount == 2);

#    if TOMLKIT11_HAS_OPTIONAL
    std::optional<EndpointConfig> stdOptEp = doc["endpoint"].tryGet<EndpointConfig>();
    CHECK(stdOptEp.has_value() && *stdOptEp == ep);
#    endif
#endif

    const EndpointConfig decodedEp = doc["endpoint"].get<EndpointConfig>();
    CHECK(decodedEp == ep);
    CHECK(!(decodedEp != ep));
    CHECK(decodedEp != EndpointConfig("https://other.example.com", 15, true));

    const Item roundtripped = parse(doc.toToml());
    CHECK(roundtripped["endpoint"].get<EndpointConfig>() == ep);
}

void testDiagnosticFormatting()
{
    using namespace tomlkit;

    const StringView kBadToml = "[server]\nhost = \"127.0.0.1\"\nport = 0xGG\n";
    const Result<Item, ParseError> res = tryParse(kBadToml, ParseOptions(), "config.toml");
    CHECK(!res.has_value());
    CHECK(res.error().location.line == 3);
    CHECK(res.error().format().find("config.toml:3:") != std::string::npos);
    CHECK(res.error().format().find('^') != std::string::npos);
}

void testEmptyItemSerializationErrorAndBoundsChecking()
{
    using namespace tomlkit;

    // 1. Uninitialized Empty nodes must fail with TypeError/TypeMismatch naming the key path or index.
    {
        Item doc = Item::document();
        (void)doc.ensure("server", "port");
        bool threw = false;
        try {
            (void)doc.toToml();
        }
        catch (const TypeError& ex) {
            threw = true;
            CHECK(std::string(ex.what()).find("server.port") != std::string::npos);
        }
        CHECK(threw);

        const Result<void, ParseError> saveRes = trySaveFileAtomic("should_not_be_created.toml", doc);
        CHECK(!saveRes.has_value());
        CHECK(saveRes.error().category == ErrorCategory::TypeMismatch);
    }

    {
        Item arr = Item::array();
        arr.push_back(42);
        arr.push_back(Item());
        bool threw = false;
        try {
            (void)arr.toToml();
        }
        catch (const TypeError& ex) {
            threw = true;
            CHECK(std::string(ex.what()).find("index 1") != std::string::npos);
        }
        CHECK(threw);
    }

    // 2. Array operator[] checks upper and negative bounds strictly.
    Item doc = parse("nums = [10, 20, 30]\n");
    const Item& cdoc = doc;

    CHECK(doc["nums"][0].asInteger() == 10);
    CHECK(cdoc["nums"][2].asInteger() == 30);

    bool mutableUpperOob = false;
    try {
        (void)doc["nums"][3];
    }
    catch (const OutOfRangeError&) {
        mutableUpperOob = true;
    }
    CHECK(mutableUpperOob);

    bool constNegOob = false;
    try {
        (void)cdoc["nums"][-1];
    }
    catch (const OutOfRangeError& ex) {
        constNegOob = true;
        CHECK(std::string(ex.what()).find("-1") != std::string::npos);
    }
    CHECK(constNegOob);

#if TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT
    bool multiDimOob = false;
    try {
        (void)doc["nums", 5];
    }
    catch (const OutOfRangeError&) {
        multiDimOob = true;
    }
    CHECK(multiDimOob);
#endif

    // 3. find / findOptional / tryFind / findArrayOrEmpty / setMultilineArray.
    CHECK(tomlkit::find<int>(doc, "nums", 1) == 20);
    CHECK(!tomlkit::findOptional<std::vector<std::string>>(doc, "missing", "arr").has_value());

    bool optTypeMismatchThrew = false;
    try {
        (void)tomlkit::findOptional<std::vector<std::string>>(doc, "nums");
    }
    catch (const TypeError&) {
        optTypeMismatchThrew = true;
    }
    CHECK(optTypeMismatchThrew);

    static_assert(
        noexcept(tomlkit::tryFind<std::vector<std::string>>(doc, "nums")),
        "tryFind must be noexcept"
    );
    CHECK(!tomlkit::tryFind<std::vector<std::string>>(doc, "nums").has_value());
    CHECK(tomlkit::tryFind<std::vector<int>>(doc, "nums")->size() == 3);
    CHECK(tomlkit::findArrayOrEmpty(doc, "missing").isArray());
    CHECK(tomlkit::findArrayOrEmpty(doc, "missing").empty());

    Item dictArr = Item::array();
    dictArr.push_back(Item::inlineTable({ { "org", "a" }, { "rep", "b" } }));
    dictArr.push_back(Item::inlineTable({ { "org", "c" }, { "rep", "d" } }));
    dictArr.setMultilineArray(true);
    Item dictDoc = Item::document();
    dictDoc["gptDict"] = std::move(dictArr);
    CHECK(
        dictDoc.toToml().find(
            "\n  { org = \"a\", rep = \"b\" },\n  { org = \"c\", rep = \"d\" },\n]"
        ) != std::string::npos
    );

    // 4. Local timezone OffsetDateTime roundtrip with millisecond precision.
    const int64_t kSampleMs = 1763060952345LL;
    const OffsetDateTime localDt = OffsetDateTime::fromEpochMillisLocal(kSampleMs);
    CHECK(localDt.isValid());
    CHECK(!localDt.isUtcZ);
    CHECK(localDt.toEpochMillis() == kSampleMs);

    Item dtDoc = Item::document();
    dtDoc["lastWriteTime"] = localDt;
    const std::string dtToml = tomlkit::format(dtDoc);
    CHECK(dtToml.find('+') != std::string::npos || dtToml.find('-') != std::string::npos);
    const Item reparsedDtDoc = tomlkit::parse(dtToml);
    CHECK(reparsedDtDoc["lastWriteTime"].isOffsetDateTime());
    CHECK(reparsedDtDoc["lastWriteTime"].asOffsetDateTime() == localDt);
    CHECK(reparsedDtDoc["lastWriteTime"].asOffsetDateTime().toEpochMillis() == kSampleMs);

    // 5. Auto-wrapping newly created long arrays while preserving explicit layouts and parsed input.
    Item wrapDoc = Item::document();
    wrapDoc["shortDicts"] = { "CommonGpt", "CommonGpt_H" };
    Item postDicts = Item::array();
    postDicts.push_back("PostStd");
    postDicts.push_back("PostPunctuation");
    postDicts.push_back("PostRareCnChar");
    postDicts.push_back("PostPunctuation_GT");
    wrapDoc["dictNames"] = std::move(postDicts);

    Item forcedSingleLine = Item::array({
        "PostStd", "PostPunctuation", "PostRareCnChar", "PostPunctuation_GT"
    });
    forcedSingleLine.setMultilineArray(false);
    wrapDoc["forcedSingle"] = std::move(forcedSingleLine);

    const std::string wrapToml = tomlkit::format(wrapDoc);
    CHECK(wrapToml.find("shortDicts = [\"CommonGpt\", \"CommonGpt_H\"]\n") != std::string::npos);
    CHECK(
        wrapToml.find(
            "dictNames = [\n"
            "  \"PostStd\",\n"
            "  \"PostPunctuation\",\n"
            "  \"PostRareCnChar\",\n"
            "  \"PostPunctuation_GT\",\n"
            "]\n"
        ) != std::string::npos
    );
    CHECK(
        wrapToml.find(
            "forcedSingle = [\"PostStd\", \"PostPunctuation\", \"PostRareCnChar\", \"PostPunctuation_GT\"]\n"
        ) != std::string::npos
    );

    const StringView kParsedLongSingle =
        "dictNames = [\"PostStd\", \"PostPunctuation\", \"PostRareCnChar\", \"PostPunctuation_GT\"]\n";
    CHECK(tomlkit::format(tomlkit::parse(kParsedLongSingle)) == detail::toString(kParsedLongSingle));

    // 6. Multiline basic strings always escape tabs as visible \t.
    Item promptDoc = Item::document();
    Item promptVal("NAME\tSRC\tID\nalice\thello\t1");
    promptVal.setStringStyle(StringStyle::MultilineBasic);
    promptDoc["PROMPT"] = std::move(promptVal);
    const std::string promptToml = tomlkit::format(promptDoc);
    CHECK(promptToml.find("NAME\\tSRC\\tID\nalice\\thello\\t1") != std::string::npos);
    CHECK(tomlkit::parse(promptToml)["PROMPT"].asString() == "NAME\tSRC\tID\nalice\thello\t1");

#if TOMLKIT11_HAS_FILESYSTEM
    // Verify std::filesystem::path compatibility in C++17+.
    const std::filesystem::path nonexistentPath = utf8ToPath("definitely_nonexistent_ergonomic_test_file.toml");
    const Result<Item, ParseError> fsRes = tryParseFile(nonexistentPath);
    CHECK(!fsRes.has_value());
    CHECK(fsRes.error().category == ErrorCategory::IoError);
#endif
}

int main()
{
    testAutoVivificationAndMultidimensionalSubscript();
    testStlContainerConversionsAndCustomSerializer();
    testDiagnosticFormatting();
    testEmptyItemSerializationErrorAndBoundsChecking();

    if (g_failures != 0) {
        std::cerr << "test_ergonomic_api failed with " << g_failures << " error(s)\n";
        return 1;
    }
    std::cout << "test_ergonomic_api: all checks passed!\n";
    return 0;
}
