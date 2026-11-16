#if TOMLKIT11_SHOULD_INCLUDE_IMPL

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <sys/stat.h>
#endif

TOMLKIT11_NAMESPACE_BEGIN

namespace
{
    uint32_t countUtf8CodepointsForError(StringView sv) noexcept
    {
        uint32_t count = 0;
        for (size_t i = 0; i < sv.size(); ++i) {
            const uint8_t b = (uint8_t)sv[i];
            if ((b & 0xC0u) != 0x80u) {
                ++count;
            }
        }
        return count;
    }

#ifdef _WIN32
    struct WinFileHandleGuard
    {
        HANDLE handle;

        explicit WinFileHandleGuard(HANDLE h) noexcept
            : handle(h)
        { }

        ~WinFileHandleGuard()
        {
            if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
                ::CloseHandle(handle);
            }
        }

        WinFileHandleGuard(const WinFileHandleGuard&) = delete;
        WinFileHandleGuard& operator=(const WinFileHandleGuard&) = delete;
    };

    Result<std::string, ParseError> readWinFileHandle(HANDLE hFile, const std::string& utf8Path)
    {
        LARGE_INTEGER liSize;
        liSize.QuadPart = 0;
        if (::GetFileSizeEx(hFile, &liSize)) {
            if (liSize.QuadPart < 0) {
                return makeUnexpected(ParseError(
                    ErrorCategory::IoError,
                    SourceLocation(),
                    "Failed to determine size of TOML file '" + utf8Path + "'",
                    utf8Path,
                    "TOML I/O error: failed to read size of '" + utf8Path + "'"
                ));
            }
            const uint64_t uSize = (uint64_t)liSize.QuadPart;
            if (uSize > (uint64_t)(std::numeric_limits<size_t>::max)()) {
                return makeUnexpected(ParseError(
                    ErrorCategory::IoError,
                    SourceLocation(),
                    "TOML file '" + utf8Path + "' is too large to read into memory",
                    utf8Path,
                    "TOML I/O error: file too large '" + utf8Path + "'"
                ));
            }

            const size_t totalSize = (size_t)uSize;
            std::string buffer(totalSize, '\0');
            size_t offset = 0;
            while (offset < totalSize) {
                const size_t remaining = totalSize - offset;
                const DWORD chunk = (DWORD)(remaining > 0x40000000u ? 0x40000000u : remaining);
                DWORD bytesRead = 0;
                if (!::ReadFile(hFile, &buffer[0] + offset, chunk, &bytesRead, nullptr)) {
                    const DWORD err = ::GetLastError();
                    const std::string errUtf8 = formatErrorCodeUtf8(std::error_code((int)err, std::system_category()));
                    return makeUnexpected(ParseError(
                        ErrorCategory::IoError,
                        SourceLocation(),
                        "Failed while reading TOML file '" + utf8Path + "': " + errUtf8,
                        utf8Path,
                        "TOML I/O error: failed while reading '" + utf8Path + "': " + errUtf8
                    ));
                }
                if (bytesRead == 0) {
                    buffer.resize(offset);
                    break;
                }
                offset += (size_t)bytesRead;
            }
            return buffer;
        }

        // Fallback for non-seekable handles (e.g., pipes).
        std::string buffer;
        char chunkBuf[65536];
        for (;;) {
            DWORD bytesRead = 0;
            if (!::ReadFile(hFile, chunkBuf, (DWORD)sizeof(chunkBuf), &bytesRead, nullptr)) {
                const DWORD err = ::GetLastError();
                if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) {
                    break;
                }
                const std::string errUtf8 = formatErrorCodeUtf8(std::error_code((int)err, std::system_category()));
                return makeUnexpected(ParseError(
                    ErrorCategory::IoError,
                    SourceLocation(),
                    "Failed while reading TOML file '" + utf8Path + "': " + errUtf8,
                    utf8Path,
                    "TOML I/O error: failed while reading '" + utf8Path + "': " + errUtf8
                ));
            }
            if (bytesRead == 0) {
                break;
            }
            buffer.append(chunkBuf, (size_t)bytesRead);
        }
        return buffer;
    }

    Result<std::string, ParseError> readWidePathWin32(WideStringView widePath, const std::string& utf8Path)
    {
        if (widePath.empty()) {
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Failed to open TOML file ''",
                "",
                "TOML I/O error: failed to open file ''"
            ));
        }

        std::wstring wideNull(widePath.data(), widePath.size());
        if (wideNull.find(L'\0') != std::wstring::npos) {
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Invalid TOML file path containing null character '" + utf8Path + "'",
                utf8Path,
                "TOML I/O error: invalid path '" + utf8Path + "'"
            ));
        }

        HANDLE hFile = ::CreateFileW(
            wideNull.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            const std::string errUtf8 = formatErrorCodeUtf8(std::error_code((int)err, std::system_category()));
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Failed to open TOML file '" + utf8Path + "': " + errUtf8,
                utf8Path,
                "TOML I/O error: failed to open file '" + utf8Path + "': " + errUtf8
            ));
        }

        WinFileHandleGuard guard(hFile);
        return readWinFileHandle(hFile, utf8Path);
    }
#else
    struct PosixFileGuard
    {
        std::FILE* pFile;

        explicit PosixFileGuard(std::FILE* f) noexcept
            : pFile(f)
        { }

        ~PosixFileGuard()
        {
            if (pFile != nullptr) {
                std::fclose(pFile);
            }
        }

        PosixFileGuard(const PosixFileGuard&) = delete;
        PosixFileGuard& operator=(const PosixFileGuard&) = delete;
    };
#endif
} // namespace

TOMLKIT11_INLINE ParseError makeParseError(
    ErrorCategory category,
    StringView source,
    size_t byteOffset,
    uint32_t byteLength,
    std::string message,
    StringView sourcePath
)
{
    const size_t clampedOffset = (std::min)(byteOffset, source.size());

    uint32_t line = 1;
    size_t lineStart = 0;
    for (size_t i = 0; i < clampedOffset; ++i) {
        if (source[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }

    size_t lineEnd = clampedOffset;
    while (lineEnd < source.size() && source[lineEnd] != '\n' && source[lineEnd] != '\r') {
        ++lineEnd;
    }

    const StringView prefixOnLine = source.substr(lineStart, clampedOffset - lineStart);
    const uint32_t column = countUtf8CodepointsForError(prefixOnLine) + 1;

    uint32_t caretCount = 1;
    if (byteLength > 1 && clampedOffset < lineEnd) {
        const size_t spanEnd = (std::min)(clampedOffset + (size_t)byteLength, lineEnd);
        caretCount = (std::max)(
            (uint32_t)1,
            countUtf8CodepointsForError(source.substr(clampedOffset, spanEnd - clampedOffset))
        );
    }

    // Expand tabs to spaces so the source line aligns with the caret indicator below.
    std::string displayLine;
    displayLine.reserve((lineEnd - lineStart) + 8);
    uint32_t visualCaretPos = 0;
    const StringView fullLine = source.substr(lineStart, lineEnd - lineStart);
    for (size_t i = 0; i < fullLine.size();) {
        const bool beforeTarget = (lineStart + i) < clampedOffset;
        if (fullLine[i] == '\t') {
            displayLine.append("    ");
            if (beforeTarget) {
                visualCaretPos += 4;
            }
            ++i;
        }
        else {
            const uint8_t b = (uint8_t)fullLine[i];
            size_t seqLen = 1;
            if ((b & 0x80u) == 0u) {
                seqLen = 1;
            }
            else if ((b & 0xE0u) == 0xC0u) {
                seqLen = 2;
            }
            else if ((b & 0xF0u) == 0xE0u) {
                seqLen = 3;
            }
            else if ((b & 0xF8u) == 0xF0u) {
                seqLen = 4;
            }
            seqLen = (std::min)(seqLen, fullLine.size() - i);
            detail::appendSv(displayLine, fullLine.substr(i, seqLen));
            if (beforeTarget) {
                ++visualCaretPos;
            }
            i += seqLen;
        }
    }

    const std::string lineNumStr = detail::uintToString((uint64_t)line);
    const std::string gutterPad(lineNumStr.size(), ' ');

    std::string locHeader;
    if (sourcePath.empty()) {
        locHeader = "line ";
        detail::appendUint(locHeader, (uint64_t)line);
        locHeader.append(", column ");
        detail::appendUint(locHeader, (uint64_t)column);
    }
    else {
        detail::appendSv(locHeader, sourcePath);
        locHeader.push_back(':');
        detail::appendUint(locHeader, (uint64_t)line);
        locHeader.push_back(':');
        detail::appendUint(locHeader, (uint64_t)column);
    }

    std::string diag;
    diag.reserve(64 + locHeader.size() + message.size() + displayLine.size() + visualCaretPos + caretCount);
    diag.append("TOML parse error at ");
    diag.append(locHeader);
    diag.append(": ");
    diag.append(message);
    diag.push_back('\n');

    diag.push_back(' ');
    diag.append(gutterPad);
    diag.append(" |\n");

    diag.push_back(' ');
    diag.append(lineNumStr);
    diag.append(" | ");
    diag.append(displayLine);
    diag.push_back('\n');

    diag.push_back(' ');
    diag.append(gutterPad);
    diag.append(" | ");
    diag.append(visualCaretPos, ' ');
    diag.append(caretCount, '^');

    return ParseError(
        category,
        SourceLocation(clampedOffset, line, column, (std::max)((uint32_t)1, byteLength)),
        std::move(message),
        detail::toString(sourcePath),
        std::move(diag)
    );
}

namespace detail
{
    TOMLKIT11_INLINE Result<std::string, ParseError> readBinaryFileUtf8Path(StringView utf8Path)
    {
#ifdef _WIN32
        const std::string pathStr = detail::toString(utf8Path);
        if (pathStr.empty()) {
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Failed to open TOML file ''",
                "",
                "TOML I/O error: failed to open file ''"
            ));
        }
        if (pathStr.find('\0') != std::string::npos) {
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Invalid TOML file path containing null character '" + pathStr + "'",
                pathStr,
                "TOML I/O error: invalid path '" + pathStr + "'"
            ));
        }
        const std::wstring widePath = utf8ToWide(utf8Path);
        return readWidePathWin32(WideStringView(widePath.data(), widePath.size()), pathStr);
#else
        const std::string pathStr = detail::toString(utf8Path);
        if (pathStr.empty()) {
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Failed to open TOML file ''",
                "",
                "TOML I/O error: failed to open file ''"
            ));
        }
        if (pathStr.find('\0') != std::string::npos) {
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Invalid TOML file path containing null character '" + pathStr + "'",
                pathStr,
                "TOML I/O error: invalid path '" + pathStr + "'"
            ));
        }

        std::FILE* pFile = std::fopen(pathStr.c_str(), "rb");
        if (pFile == nullptr) {
            const int err = errno;
            const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
            return makeUnexpected(ParseError(
                ErrorCategory::IoError,
                SourceLocation(),
                "Failed to open TOML file '" + pathStr + "': " + errUtf8,
                pathStr,
                "TOML I/O error: failed to open file '" + pathStr + "': " + errUtf8
            ));
        }

        PosixFileGuard guard(pFile);
        struct stat st;
        if (::fstat(fileno(pFile), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return makeUnexpected(ParseError(
                    ErrorCategory::IoError,
                    SourceLocation(),
                    "Failed to open TOML file '" + pathStr + "': path is a directory",
                    pathStr,
                    "TOML I/O error: path is a directory '" + pathStr + "'"
                ));
            }
            if (S_ISREG(st.st_mode) && st.st_size >= 0) {
                const uint64_t uSize = (uint64_t)st.st_size;
                if (uSize > (uint64_t)(std::numeric_limits<size_t>::max)()) {
                    return makeUnexpected(ParseError(
                        ErrorCategory::IoError,
                        SourceLocation(),
                        "TOML file '" + pathStr + "' is too large to read into memory",
                        pathStr,
                        "TOML I/O error: file too large '" + pathStr + "'"
                    ));
                }

                const size_t totalSize = (size_t)uSize;
                std::string buffer(totalSize, '\0');
                size_t offset = 0;
                while (offset < totalSize) {
                    const size_t n = std::fread(&buffer[0] + offset, 1, totalSize - offset, pFile);
                    if (n == 0) {
                        if (std::ferror(pFile)) {
                            const int err = errno;
                            const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
                            return makeUnexpected(ParseError(
                                ErrorCategory::IoError,
                                SourceLocation(),
                                "Failed while reading TOML file '" + pathStr + "': " + errUtf8,
                                pathStr,
                                "TOML I/O error: failed while reading '" + pathStr + "': " + errUtf8
                            ));
                        }
                        buffer.resize(offset);
                        break;
                    }
                    offset += n;
                }
                return buffer;
            }
        }

        std::string buffer;
        char chunkBuf[65536];
        for (;;) {
            const size_t n = std::fread(chunkBuf, 1, sizeof(chunkBuf), pFile);
            if (n > 0) {
                buffer.append(chunkBuf, n);
            }
            if (n < sizeof(chunkBuf)) {
                if (std::ferror(pFile)) {
                    const int err = errno;
                    const std::string errUtf8 = formatErrorCodeUtf8(std::error_code(err, std::generic_category()));
                    return makeUnexpected(ParseError(
                        ErrorCategory::IoError,
                        SourceLocation(),
                        "Failed while reading TOML file '" + pathStr + "': " + errUtf8,
                        pathStr,
                        "TOML I/O error: failed while reading '" + pathStr + "': " + errUtf8
                    ));
                }
                break;
            }
        }
        return buffer;
#endif
    }

    TOMLKIT11_INLINE Result<std::string, ParseError> readBinaryFileWidePath(
        WideStringView widePath,
        std::string* pOutUtf8Path
    )
    {
        const std::string utf8Path = wideToUtf8(widePath);
        if (pOutUtf8Path != nullptr) {
            *pOutUtf8Path = utf8Path;
        }
#ifdef _WIN32
        return readWidePathWin32(widePath, utf8Path);
#else
        return readBinaryFileUtf8Path(StringView(utf8Path.data(), utf8Path.size()));
#endif
    }
} // namespace detail

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL
