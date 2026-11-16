#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include <tomlkit11/tomlkit11.hpp>
#include "TaggedJson.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    bool endsWithStr(const std::string& s, const char* suffix)
    {
        const std::size_t suffixLen = std::strlen(suffix);
        if (s.size() < suffixLen) {
            return false;
        }
        return s.compare(s.size() - suffixLen, suffixLen, suffix) == 0;
    }

    bool startsWithStr(const std::string& s, const char* prefix)
    {
        const std::size_t prefixLen = std::strlen(prefix);
        if (s.size() < prefixLen) {
            return false;
        }
        return s.compare(0, prefixLen, prefix) == 0;
    }

    std::string joinUtf8Path(const std::string& base, const std::string& rel)
    {
        if (base.empty()) {
            return rel;
        }
        if (rel.empty()) {
            return base;
        }
        std::string out = base;
        while (!out.empty() && (out.back() == '/' || out.back() == '\\')) {
            out.pop_back();
        }
        std::size_t start = 0;
        while (start < rel.size() && (rel[start] == '/' || rel[start] == '\\')) {
            ++start;
        }
        out.push_back('/');
        out.append(rel, start, rel.size() - start);
        return out;
    }

#ifdef _WIN32
    std::wstring utf8ToWideStrict(const std::string& utf8)
    {
        if (!tomlkit::isValidUtf8(tomlkit::StringView(utf8))) {
            throw std::runtime_error("Invalid UTF-8 path: " + utf8);
        }
        return tomlkit::utf8ToWide(tomlkit::StringView(utf8));
    }

    std::string wideToUtf8Str(const wchar_t* wide, std::size_t wideLen)
    {
        if (wide == nullptr || wideLen == 0) {
            return std::string();
        }
        return tomlkit::wideToUtf8(tomlkit::WideStringView(wide, wideLen));
    }

    std::vector<std::wstring> parseWindowsCommandLineW(const wchar_t* pCmdLine)
    {
        std::vector<std::wstring> args;
        if (pCmdLine == nullptr) {
            return args;
        }

        const wchar_t* p = pCmdLine;
        while (*p == L' ' || *p == L'\t') {
            ++p;
        }
        if (*p != L'\0') {
            std::wstring exe;
            if (*p == L'"') {
                ++p;
                while (*p != L'\0' && *p != L'"') {
                    exe.push_back(*p++);
                }
                if (*p == L'"') {
                    ++p;
                }
            }
            else {
                while (*p != L'\0' && *p != L' ' && *p != L'\t') {
                    exe.push_back(*p++);
                }
            }
            args.push_back(std::move(exe));
        }

        while (*p != L'\0') {
            while (*p == L' ' || *p == L'\t') {
                ++p;
            }
            if (*p == L'\0') {
                break;
            }

            std::wstring arg;
            bool inQuotes = false;
            while (*p != L'\0') {
                if (!inQuotes && (*p == L' ' || *p == L'\t')) {
                    break;
                }
                if (*p == L'\\') {
                    std::size_t bsCount = 0;
                    while (*p == L'\\') {
                        ++bsCount;
                        ++p;
                    }
                    if (*p == L'"') {
                        arg.append(bsCount / 2, L'\\');
                        if ((bsCount % 2) != 0) {
                            arg.push_back(L'"');
                            ++p;
                        }
                        else {
                            if (inQuotes && p[1] == L'"') {
                                arg.push_back(L'"');
                                p += 2;
                            }
                            else {
                                inQuotes = !inQuotes;
                                ++p;
                            }
                        }
                    }
                    else {
                        arg.append(bsCount, L'\\');
                    }
                }
                else if (*p == L'"') {
                    if (inQuotes && p[1] == L'"') {
                        arg.push_back(L'"');
                        p += 2;
                    }
                    else {
                        inQuotes = !inQuotes;
                        ++p;
                    }
                }
                else {
                    arg.push_back(*p++);
                }
            }
            args.push_back(std::move(arg));
        }
        return args;
    }

    std::wstring toWin32DirPattern(const std::string& utf8Dir)
    {
        std::wstring w = utf8ToWideStrict(utf8Dir);
        for (std::size_t i = 0; i < w.size(); ++i) {
            if (w[i] == L'/') {
                w[i] = L'\\';
            }
        }
        while (!w.empty() && w.back() == L'\\') {
            w.pop_back();
        }
        w.append(L"\\*");
        return w;
    }

    bool fileExists(const std::string& utf8Path)
    {
        const std::wstring wide = utf8ToWideStrict(utf8Path);
        const DWORD attrs = GetFileAttributesW(wide.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool dirExists(const std::string& utf8Path)
    {
        const std::wstring wide = utf8ToWideStrict(utf8Path);
        const DWORD attrs = GetFileAttributesW(wide.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::string readBinaryFile(const std::string& utf8Path)
    {
        const std::wstring wide = utf8ToWideStrict(utf8Path);
        std::FILE* pFile = nullptr;
#if defined(_MSC_VER)
        if (_wfopen_s(&pFile, wide.c_str(), L"rb") != 0 || pFile == nullptr) {
            throw std::runtime_error("Cannot open file: " + utf8Path);
        }
#else
        pFile = _wfopen(wide.c_str(), L"rb");
        if (pFile == nullptr) {
            throw std::runtime_error("Cannot open file: " + utf8Path);
        }
#endif

        std::string out;
        char buf[65536];
        while (true) {
            const std::size_t n = std::fread(buf, 1, sizeof(buf), pFile);
            if (n > 0) {
                out.append(buf, n);
            }
            if (n < sizeof(buf)) {
                if (std::ferror(pFile) != 0) {
                    std::fclose(pFile);
                    throw std::runtime_error("Read error in file: " + utf8Path);
                }
                break;
            }
        }
        std::fclose(pFile);
        return out;
    }

    void scanTomlFilesRecursive(
        const std::string& baseDir,
        const std::string& relDir,
        std::vector<std::string>& outRelPaths
    )
    {
        const std::string fullDir = relDir.empty() ? baseDir : joinUtf8Path(baseDir, relDir);
        const std::wstring pattern = toWin32DirPattern(fullDir);

        WIN32_FIND_DATAW ffd;
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
        if (hFind == INVALID_HANDLE_VALUE) {
            return;
        }

        do {
            if (std::wcscmp(ffd.cFileName, L".") == 0 || std::wcscmp(ffd.cFileName, L"..") == 0) {
                continue;
            }
            const std::string name = wideToUtf8Str(ffd.cFileName, std::wcslen(ffd.cFileName));
            const std::string childRel = relDir.empty() ? name : (relDir + "/" + name);
            if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                scanTomlFilesRecursive(baseDir, childRel, outRelPaths);
            }
            else if (endsWithStr(childRel, ".toml")) {
                outRelPaths.push_back(childRel);
            }
        }
        while (FindNextFileW(hFind, &ffd) != 0);

        FindClose(hFind);
    }
#else
    bool fileExists(const std::string& utf8Path)
    {
        struct stat st;
        if (::stat(utf8Path.c_str(), &st) != 0) {
            return false;
        }
        return S_ISREG(st.st_mode);
    }

    bool dirExists(const std::string& utf8Path)
    {
        struct stat st;
        if (::stat(utf8Path.c_str(), &st) != 0) {
            return false;
        }
        return S_ISDIR(st.st_mode);
    }

    std::string readBinaryFile(const std::string& utf8Path)
    {
        std::FILE* pFile = std::fopen(utf8Path.c_str(), "rb");
        if (pFile == nullptr) {
            throw std::runtime_error("Cannot open file: " + utf8Path);
        }

        std::string out;
        char buf[65536];
        while (true) {
            const std::size_t n = std::fread(buf, 1, sizeof(buf), pFile);
            if (n > 0) {
                out.append(buf, n);
            }
            if (n < sizeof(buf)) {
                if (std::ferror(pFile) != 0) {
                    std::fclose(pFile);
                    throw std::runtime_error("Read error in file: " + utf8Path);
                }
                break;
            }
        }
        std::fclose(pFile);
        return out;
    }

    void scanTomlFilesRecursive(
        const std::string& baseDir,
        const std::string& relDir,
        std::vector<std::string>& outRelPaths
    )
    {
        const std::string fullDir = relDir.empty() ? baseDir : joinUtf8Path(baseDir, relDir);
        DIR* pDir = ::opendir(fullDir.c_str());
        if (pDir == nullptr) {
            return;
        }

        while (struct dirent* pEnt = ::readdir(pDir)) {
            if (std::strcmp(pEnt->d_name, ".") == 0 || std::strcmp(pEnt->d_name, "..") == 0) {
                continue;
            }
            const std::string name(pEnt->d_name);
            const std::string childRel = relDir.empty() ? name : (relDir + "/" + name);
            const std::string childFull = joinUtf8Path(baseDir, childRel);

            struct stat st;
            if (::stat(childFull.c_str(), &st) != 0) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                scanTomlFilesRecursive(baseDir, childRel, outRelPaths);
            }
            else if (S_ISREG(st.st_mode) && endsWithStr(childRel, ".toml")) {
                outRelPaths.push_back(childRel);
            }
        }

        ::closedir(pDir);
    }
#endif

    std::vector<std::string> collectSuiteCases(
        const std::string& testsDir,
        const std::string& manifestName
    )
    {
        std::vector<std::string> cases;
        const std::string manifestPath = joinUtf8Path(testsDir, manifestName);
        if (fileExists(manifestPath)) {
            const std::string manifestContent = readBinaryFile(manifestPath);
            std::size_t pos = 0;
            while (pos < manifestContent.size()) {
                std::size_t nl = manifestContent.find('\n', pos);
                if (nl == std::string::npos) {
                    nl = manifestContent.size();
                }
                std::string line = manifestContent.substr(pos, nl - pos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                pos = nl + 1;

                if (!line.empty() && endsWithStr(line, ".toml")) {
                    cases.push_back(line);
                }
            }
            return cases;
        }

        if (dirExists(joinUtf8Path(testsDir, "invalid"))) {
            scanTomlFilesRecursive(testsDir, "invalid", cases);
        }
        if (dirExists(joinUtf8Path(testsDir, "valid"))) {
            scanTomlFilesRecursive(testsDir, "valid", cases);
        }
        if (cases.empty()) {
            scanTomlFilesRecursive(testsDir, "", cases);
        }
        std::sort(cases.begin(), cases.end());
        return cases;
    }

    struct SuiteStats
    {
        int validPassed = 0;
        int validFailed = 0;
        int invalidPassed = 0;
        int invalidFailed = 0;
        int losslessPassed = 0;
        int losslessFailed = 0;
        int encoderPassed = 0;
        int encoderFailed = 0;
    };

    SuiteStats runManifest(
        const std::string& testsDir,
        const std::string& manifestName,
        tomlkit::SpecVersion spec
    )
    {
        SuiteStats stats;
        const std::vector<std::string> cases = collectSuiteCases(testsDir, manifestName);

        tomlkit::ParseOptions parseOpts;
        parseOpts.spec = spec;
        parseOpts.maxNestingDepth = 512;

        tomlkit::FormatOptions fmtOpts;
        fmtOpts.spec = spec;

        for (std::size_t i = 0; i < cases.size(); ++i) {
            const std::string& line = cases[i];
            const std::string tomlPath = joinUtf8Path(testsDir, line);
            const std::string rawToml = readBinaryFile(tomlPath);

            if (startsWithStr(line, "invalid/")) {
                const tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res =
                    tomlkit::tryParse(rawToml, parseOpts, line);
                if (!res) {
                    ++stats.invalidPassed;
                }
                else {
                    ++stats.invalidFailed;
                    if (stats.invalidFailed <= 20) {
                        std::cerr << "[" << manifestName << "] INVALID ACCEPTED: " << line << '\n';
                    }
                }
                continue;
            }

            if (startsWithStr(line, "valid/")) {
                const tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res =
                    tomlkit::tryParse(rawToml, parseOpts, line);
                if (!res) {
                    ++stats.validFailed;
                    if (stats.validFailed <= 20) {
                        std::cerr << "[" << manifestName << "] VALID REJECTED: " << line
                                  << "\n  " << res.error().message << '\n';
                    }
                    continue;
                }

                std::string jsonPath = tomlPath.substr(0, tomlPath.size() - 5);
                jsonPath.append(".json");
                const std::string rawJson = readBinaryFile(jsonPath);
                const tomlkit::Item expectedDoc = tomlkit::test_adapter::taggedJsonToItem(rawJson);

                if (tomlkit::test_adapter::itemsMatchForTomlTest(*res, expectedDoc)) {
                    ++stats.validPassed;
                }
                else {
                    ++stats.validFailed;
                    if (stats.validFailed <= 20) {
                        std::cerr << "[" << manifestName << "] SEMANTIC MISMATCH: " << line
                                  << "\n  expected: " << tomlkit::test_adapter::itemToTaggedJson(expectedDoc)
                                  << "\n  actual:   " << tomlkit::test_adapter::itemToTaggedJson(*res) << '\n';
                    }
                }

                const std::string reemitted = res->toToml(fmtOpts);
                const std::string losslessOut = res->toToml();
                if (losslessOut == rawToml) {
                    ++stats.losslessPassed;
                }
                else {
                    ++stats.losslessFailed;
                    if (stats.losslessFailed <= 20) {
                        std::cerr << "[" << manifestName << "] LOSSLESS MISMATCH: " << line
                                  << "\n--- RAW (" << rawToml.size() << ") ---\n" << rawToml
                                  << "\n--- OUT (" << losslessOut.size() << ") ---\n" << losslessOut << '\n';
                    }
                }
                (void)reemitted;

                const std::string encodedToml = expectedDoc.toToml(fmtOpts);
                const tomlkit::Result<tomlkit::Item, tomlkit::ParseError> reparsedEncoded =
                    tomlkit::tryParse(encodedToml, parseOpts, line);
                if (reparsedEncoded && tomlkit::test_adapter::itemsMatchForTomlTest(*reparsedEncoded, expectedDoc)) {
                    ++stats.encoderPassed;
                }
                else {
                    ++stats.encoderFailed;
                    if (stats.encoderFailed <= 20) {
                        std::cerr << "[" << manifestName << "] ENCODER ROUNDTRIP FAILED: " << line
                                  << "\n--- ENCODED TOML ---\n" << encodedToml
                                  << "\n--- ERROR ---\n"
                                  << (reparsedEncoded ? std::string("semantic mismatch") : reparsedEncoded.error().message)
                                  << '\n';
                    }
                }
            }
        }

        return stats;
    }

    struct SuiteSpec
    {
        const char* manifest;
        tomlkit::SpecVersion spec;
        const char* title;
    };
}

int main(int argc, char** argv)
{
    std::string testsDir;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    SetConsoleOutputCP(CP_UTF8);
    (void)argc;
    (void)argv;
    const std::vector<std::wstring> wargs = parseWindowsCommandLineW(GetCommandLineW());
    if (wargs.size() < 2) {
        std::cerr << "Usage: tomlkit11-compliance-runner <path-to-toml-test/tests>\n";
        return 2;
    }
    try {
        testsDir = wideToUtf8Str(wargs[1].c_str(), wargs[1].size());
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 2;
    }
#else
    if (argc < 2) {
        std::cerr << "Usage: tomlkit11-compliance-runner <path-to-toml-test/tests>\n";
        return 2;
    }
    testsDir = argv[1];
#endif

    const SuiteSpec suites[2] = {
        { "files-toml-1.1.0", tomlkit::SpecVersion::v1_1, "TOML v1.1.0" },
        { "files-toml-1.0.0", tomlkit::SpecVersion::v1_0, "TOML v1.0.0" }
    };

    int totalFailures = 0;

    try {
        for (std::size_t i = 0; i < 2; ++i) {
            const SuiteStats s = runManifest(testsDir, suites[i].manifest, suites[i].spec);
            std::cout << "=== " << suites[i].title << " Official Compliance Suite ===\n"
                      << "  Valid Decoder    : " << s.validPassed << " passed, " << s.validFailed << " failed\n"
                      << "  Invalid Reject   : " << s.invalidPassed << " passed, " << s.invalidFailed << " failed\n"
                      << "  Byte-Lossless RT : " << s.losslessPassed << " passed, " << s.losslessFailed << " failed\n"
                      << "  Encoder RT       : " << s.encoderPassed << " passed, " << s.encoderFailed << " failed\n";
            totalFailures += s.validFailed + s.invalidFailed + s.losslessFailed + s.encoderFailed;
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Runner error: " << ex.what() << '\n';
        return 2;
    }

    return totalFailures == 0 ? 0 : 1;
}
