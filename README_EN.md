# tomlkit11

[简体中文](./README.md) | English

[![Try it on Compiler Explorer](https://img.shields.io/badge/Compiler_Explorer-Try_Online-1f6feb?logo=compilerexplorer&logoColor=white)](https://godbolt.org/z/8PqT131j1)

> [!NOTE]
> **Using C++23? Check out the C++23 native module version: [tomlkitpp](https://github.com/julixian/tomlkitpp)**
>
> If your project and toolchain support **C++23 native modules** (`export module tomlkitpp;` + `import std;`) and [`mcpp`](https://github.com/mcpp-community/mcpp), consider using **`tomlkitpp`**:
> - **Native module compilation model**: Avoids repeated preprocessor parsing of headers across translation units via module BMIs, with no compiled-lib macro setup needed
> - **Native C++23 ergonomics**: Native `std::expected`, out-of-the-box multidimensional `cfg["a", "b"]`, `std::println`, and Ranges integration
> - **Consistent API**: Shares the `tomlkit::` namespace and identical `Item` / `parse` / `findOr` contracts with `tomlkit11`, so upgrading to C++23 modules later only requires changing `#include <tomlkit11/tomlkit11.hpp>` to `import tomlkitpp;`.

`tomlkit11` is the **zero-dependency C++11 header-only sister library** to [tomlkitpp](https://github.com/julixian/tomlkitpp) (also available as an amalgamated single header `single_include/tomlkit11/tomlkit11.hpp` and an optional compiled target for in-tree CMake builds). It targets **TOML v1.1.0** by default (with `SpecVersion::v1_0` compatibility mode) and preserves comments, blank lines, indentation, quote styles, and integer radices across edits.

---

## Comparison with C++23 Module Version (`tomlkitpp`)

| Feature | **[tomlkitpp](https://github.com/julixian/tomlkitpp) (C++23 Native Module)** | **tomlkit11 (This Repo)** |
| --- | --- | --- |
| **C++ Baseline** | C++23 (`import std;`) | **C++11+** (progressively unlocks C++14/17/20/23 features) |
| **Distribution** | C++23 native module (`export module tomlkitpp;`) | Header-only / single-header / optional in-tree `tomlkit11::tomlkit11_compiled` |
| **Build Systems** | [`mcpp`](https://github.com/mcpp-community/mcpp) | CMake (`FetchContent` / `find_package`), vcpkg, single-header drop-in |
| **Error Handling** | Exceptions + native `std::expected<T, ParseError>` | Exceptions + `tomlkit::Result<T, ParseError>` (convertible to `std::expected` in C++23) + full `-fno-exceptions` support |
| **Deep Path Access** | Native C++23 `cfg["a", "b", 0]` | `cfg.find("a", "b", 0)` / `cfg.ensure("a", "b")` in C++11–20; auto-unlocks `cfg["a", "b", 0]` in C++23 |
| **TOML Spec & Round-Trip** | TOML v1.1.0 / v1.0.0, byte-lossless CST | Identical lexer, parser, and CST-preserving emitter |
| **Official `toml-test`** | 100% 4-way pass (v1.1.0 & v1.0.0) | 100% 4-way pass (pure C++11 compliance runner) |

### Migration Example (`tomlkit11` → `tomlkitpp`)

When you are ready to adopt C++23 modules, your TOML code requires almost no changes:

```cpp
// ==================== tomlkit11 (C++11+) ====================
#include <tomlkit11/tomlkit11.hpp>

int main()
{
    tomlkit::Item cfg = tomlkit::parse("title = 'demo'\n[server]\nport = 8080\n");
    int port = tomlkit::findOr(cfg, "server", "port", 80);
    cfg.ensure("server", "host") = "127.0.0.1"; // In C++23, cfg["server", "host"] = "127.0.0.1"; also works
    std::string out = tomlkit::format(cfg);
    return port == 8080 && !out.empty() ? 0 : 1;
}
```

```cpp
// ==================== Upgrade to tomlkitpp (C++23 Modules) ====================
import std;
import tomlkitpp;

int main()
{
    tomlkit::Item cfg = tomlkit::parse("title = 'demo'\n[server]\nport = 8080\n");
    int port = tomlkit::findOr(cfg, "server", "port", 80);
    cfg["server", "host"] = "127.0.0.1";
    std::string out = tomlkit::format(cfg);
    return port == 8080 && !out.empty() ? 0 : 1;
}
```

---

## Highlights

- **TOML v1.1.0 by Default** (with `SpecVersion::v1_0` compatibility mode):
  - Multi-line inline tables with internal comments and trailing commas
  - `\e` (ESC) and `\xHH` escapes in basic strings
  - Optional seconds in `LocalTime`, `LocalDateTime`, and `OffsetDateTime` (e.g., `07:32`, `1979-05-27T07:32Z`)
- **Format and Comment Preservation**:
  - Preserves leading comments, inline comments, comments inside arrays and inline tables, blank lines, indentation, CRLF/LF line endings, and UTF-8 BOM
  - Preserves integer radices and underscores (`0xff`, `1_000_000`), string quote styles, dotted keys, and section ordering
  - Re-formats only modified nodes while emitting untouched regions verbatim
- **Single-Type Node and Orthogonal Lookup API**:
  - Represents all TOML values through `tomlkit::Item` (`using Document = Item;`, easily aliased as `using toml = tomlkit::Item;`)
  - Distinct lookup helpers: `find`, `findOptional`, `tryFind`, `findOr`, `findOrDefault`, `findArrayOrEmpty`, `findTableOrEmpty`, `ensure`
  - Both throwing (`parse` / `get<T>()`) and non-throwing (`tryParse` / `tryGet<T>()` / `tryFormat` / `trySaveFileAtomic`) APIs
  - Custom struct serialization via `tomlkit::Serializer<T>` specialization
- **Pure C++11 Baseline & Zero External Dependencies**:
  - Core library and `toml-test` tools strictly adhere to C++11 with no dependency on `std::filesystem`, `std::string_view`, `std::optional`, or `std::variant`
  - Full support for `-fno-exceptions` and MSVC `/EHs-c- /D_HAS_EXCEPTIONS=0`
- **100% Pass Rate on Official [`toml-lang/toml-test`](https://github.com/toml-lang/toml-test)**:
  - **TOML v1.1.0**: Valid `218/218`, Invalid `502/502`, Round-trip `218/218`, Encoder `218/218`
  - **TOML v1.0.0**: Valid `208/208`, Invalid `509/509`, Round-trip `208/208`, Encoder `208/208`

---

## Comparison with Other C++ TOML Libraries

| Dimension | **[tomlkitpp](https://github.com/julixian/tomlkitpp)** | **tomlkit11** | **toml++** (v3.4) | **toml11** (v4.4) |
| --- | --- | --- | --- | --- |
| **Minimum C++ Standard** | C++23 | **C++11** | C++17 | **C++11** |
| **Distribution** | **Native C++23 module** (`export module`) | Header-only / single-header / optional static lib | Header-only / single-header / optional compiled lib | Header-only / single-header / optional precompiled lib |
| **TOML Spec Support** | **v1.1.0** (default) & v1.0.0 | **v1.1.0** (default) & v1.0.0 | v1.0.0 | **v1.1.0** & v1.0.0 |
| **Comment & Whitespace Preservation** | **Full** (leading/inline/inner comments, blank lines, indentation) | **Full** (same as `tomlkitpp`) | No (AST-only, oriented toward parsing & fresh emission) | Partial (`preserve_comments` keeps node-attached comments, not blank lines or exact whitespace trivia) |
| **Literal Format & Key Order Preservation** | **Verbatim replay** (`0xff_00`, quote styles, dotted keys, section order) | **Verbatim replay** (same as `tomlkitpp`) | No (`table` sorts keys lexicographically by default) | Partial (`ordered_value` keeps order and radix/quote enums, not numeric underscores or raw text) |
| **Value Model** | Single `tomlkit::Item` (native `doc["a", "b", 0]`) | Single `tomlkit::Item` (unlocks `doc["a", "b", 0]` in C++23) | Polymorphic `toml::node` hierarchy | Templated `toml::basic_value<Config>` |
| **Non-Throwing API / `-fno-exceptions`** | `std::expected` APIs (`tryParse` / `tryGet`) | **Full** (`Result<T, E>` + `-fno-exceptions` / `/EHs-c-`) | **Full** (`TOML_EXCEPTIONS=0`) | Provides `try_parse`; does not compile with exceptions disabled |

> **When to choose which**: For read-only config loading or generating fresh TOML from scratch, AST-based libraries like `toml++` are lightweight and mature. When your tool needs to **programmatically update config files in place without stripping user comments, blank lines, or literal formatting** (similar to Rust's `toml_edit` or Python's `tomlkit`), choose `tomlkitpp` (C++23 module) or `tomlkit11` (C++11+).

---

## Progressive C++11 → C++23 Feature Unlocks

`tomlkit11` detects the active C++ standard using `_MSVC_LANG` on MSVC (so it works accurately even without `/Zc:__cplusplus`) and automatically unlocks modern standard-library integrations:

| Standard | Feature Macros | Unlocked Capabilities |
| --- | --- | --- |
| **C++11 / C++14** (Baseline) | `TOMLKIT11_CPLUSPLUS` | Full parse/edit/CST-preserving emit, `StringView` / `Optional<T>` / `Result<T, E>`, variadic `find(...)` / `ensure(...)`, UTF-8 `std::string` & `std::wstring` file I/O |
| **C++17** | `TOMLKIT11_HAS_STRING_VIEW`, `TOMLKIT11_HAS_OPTIONAL`, `TOMLKIT11_HAS_FILESYSTEM`, `TOMLKIT11_NODISCARD` | `tomlkit::StringView` aliases `std::string_view`; `tomlkit::Optional<T>` aliases `std::optional<T>`; enables `std::filesystem::path` overloads plus `pathToUtf8` / `utf8ToPath`; structured bindings on `for (auto [k, v] : tbl.tableItems())` |
| **C++20** | `TOMLKIT11_HAS_CHAR8_T` | Enables `std::u8string_view` overloads on `tryParseFile`, `parseFile`, `trySaveFileAtomic`, `saveFileAtomic` |
| **C++23** | `TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT`, `__cpp_lib_expected` | Enables multidimensional subscripting `cfg["a", "b", 0]`; `tomlkit::Result<T, E>` implicitly converts to `std::expected<T, E>` |

---

## Integration & Build Acceleration

### 1. CMake `FetchContent`

```cmake
include(FetchContent)
FetchContent_Declare(
    tomlkit11
    GIT_REPOSITORY https://github.com/julixian/tomlkit11.git
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(tomlkit11)

target_link_libraries(your_target PRIVATE tomlkit11::tomlkit11)
```

### 2. CMake Install & `find_package`

```bash
cmake -B build -DTOMLKIT11_BUILD_TESTS=OFF -DTOMLKIT11_BUILD_TOOLS=OFF
cmake --install build --prefix /path/to/install
```

```cmake
find_package(tomlkit11 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE tomlkit11::tomlkit11)
```

### 3. Single-Header Copy

Drop [`single_include/tomlkit11/tomlkit11.hpp`](./single_include/tomlkit11/tomlkit11.hpp) directly into your project's include directory:

```cpp
#include "tomlkit11.hpp"
```

When building tests or tools via CMake, pass `-DTOMLKIT11_USE_SINGLE_INCLUDE=ON` to compile against the amalgamated header.

### 4. vcpkg

Official port manifests are provided under [`packaging/vcpkg/`](./packaging/vcpkg). Use as an overlay port (or install directly once merged upstream):

```bash
vcpkg install tomlkit11 --overlay-ports=/path/to/tomlkit11/packaging/vcpkg
```

### 5. Compiled-Unit Mode (Optional Build Speedup)

1. **In-tree CMake (`FetchContent` / `add_subdirectory`)**: When multiple `.cpp` translation units include `tomlkit11`, link the built-in static target `tomlkit11::tomlkit11_compiled` instead of creating a manual wrapper file — it compiles `src/tomlkit11.cpp` under your project's own C++ standard and propagates `TOMLKIT11_COMPILED_LIB` automatically:

   ```cmake
   target_link_libraries(your_target PRIVATE tomlkit11::tomlkit11_compiled)
   ```

   > **Note**: Installed packages (`vcpkg` and `cmake --install`) intentionally remain pure header-only (`tomlkit11::tomlkit11`). Precompiling a binary `.lib` under one C++ standard (e.g. C++14) would mismatch symbol mangling for consumers built under another standard (where `tomlkit::StringView`, `tomlkit::Optional`, and `std::filesystem::path` signatures adapt), and MSBuild `.sln` projects integrated via vcpkg do not automatically propagate CMake target compile definitions.

2. **`vcpkg` / Visual Studio `.sln` (`.vcxproj`) / single-header builds**: Add `TOMLKIT11_COMPILED_LIB` to project-wide Preprocessor Definitions (or compiler flags), and in any single `.cpp` file in your project (e.g. `main.cpp`, `pch.cpp`, or a dedicated `.cpp`) place:

   ```cpp
   #define TOMLKIT11_IMPLEMENTATION
   #include <tomlkit11/tomlkit11.hpp>
   ```

> [!NOTE]
> The C++23 native module library [tomlkitpp](https://github.com/julixian/tomlkitpp) resolves repeated header parsing natively through module BMIs without needing compiled-unit macros.

### 6. Exception-Free Builds (`-fno-exceptions`)

`tomlkit11` automatically detects `-fno-exceptions` (`__cpp_exceptions == 0`) and MSVC `/EHs-c- /D_HAS_EXCEPTIONS=0`, or can be forced via `#define TOMLKIT11_NO_EXCEPTIONS 1`:
- `TOMLKIT11_HAS_EXCEPTIONS` evaluates to `0`, excluding all `throw`, `try`, `catch`, and `<stdexcept>` usage.
- `tryParse`, `tryParseFile`, `tryFormat`, `trySaveFileAtomic`, `tryGet<T>()`, `tryFind<T>()`, `findOr`, `findOrDefault<T>`, `findArrayOrEmpty`, `findTableOrEmpty`, `ensure`, and `findKey` operate cleanly without exceptions.
- Precondition violations in non-`try` mutating methods call `TOMLKIT11_ABORT()` (defaults to `std::abort()`, overridable via `#define TOMLKIT11_ABORT()`).

---

## Quick Start

### 1. Parsing, Lookup, and In-Place Editing (C++11)

```cpp
#include <tomlkit11/tomlkit11.hpp>
#include <iostream>
#include <string>
#include <vector>

using toml = tomlkit::Item;

int main()
{
    toml cfg = tomlkit::parse(
        "# Application settings\n"
        "title = \"GalTranslPP\"\n\n"
        "[server]\n"
        "endpoints = [\n"
        "    { host = \"127.0.0.1\", port = 8080, }, # v1.1 trailing comma in inline table\n"
        "]\n"
        "wakeup = 07:30 # v1.1 time without seconds\n"
    );

    // Variadic path lookups mixing string keys and integer indices
    std::string title = tomlkit::find<std::string>(cfg, "title");
    tomlkit::Optional<std::vector<std::string>> tagsOpt =
        tomlkit::findOptional<std::vector<std::string>>(cfg, "tags");
    tomlkit::Optional<int> portOpt =
        tomlkit::tryFind<int>(cfg, "server", "endpoints", 0, "port");
    int port = tomlkit::findOr(cfg, "server", "endpoints", 0, "port", 80);

    // Update existing entries and add commented keys while keeping surrounding formatting intact
    cfg["server"]["endpoints"][0]["port"] = 9090;
    cfg.ensure("server", "timeoutMs") = 5000;
    cfg["server"]["timeoutMs"].addLeadingComment("Connection timeout in milliseconds");
    cfg["server"]["timeoutMs"].setInlineComment("newly added");

    std::cout << tomlkit::format(cfg);
    return (title == "GalTranslPP" && !tagsOpt.has_value() && portOpt.has_value() && port == 8080) ? 0 : 1;
}
```

### 2. Non-Throwing Parsing and Diagnostics

```cpp
tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res = tomlkit::tryParseFile("config.toml");
if (!res) {
    // Prints file path, line/column numbers, and caret indicator
    std::cerr << res.error().formatDiagnostic() << '\n';
    return 1;
}
tomlkit::Item& cfg = *res;
```

### 3. Custom Type Serialization

```cpp
struct Endpoint
{
    std::string host;
    int port;

    Endpoint() : port(8080) {}
    Endpoint(std::string h, int p) : host(h), port(p) {}
};

namespace tomlkit
{
    template<>
    struct Serializer<Endpoint>
    {
        static void toToml(const Endpoint& ep, Item& out)
        {
            out = Item::inlineTable({
                { "host", Item(ep.host) },
                { "port", Item(ep.port) }
            });
        }

        static Endpoint fromToml(const Item& item)
        {
            return Endpoint(
                item.at("host").get<std::string>(),
                item.valueOr("port", 8080)
            );
        }
    };
}
```

### 4. Atomic File Saving and Spec Selection

```cpp
// Writes to a temporary file first and atomically replaces the target path (accepts UTF-8 string, std::wstring, or fs::path)
tomlkit::saveFileAtomic("D:/Configs/config.toml", cfg);

// Emit TOML v1.0-compatible output for older downstream consumers
std::string v10Text = tomlkit::format(cfg, tomlkit::FormatOptions::v1_0());
```

---

## Building & Testing

```bash
# Configure, build, and run unit tests (C++11 / 14 / 17 / 20 / 23)
cmake -B build -G Ninja -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build build
ctest --test-dir build --output-on-failure

# Test the amalgamated single-include header
cmake -B build-single -G Ninja -DTOMLKIT11_USE_SINGLE_INCLUDE=ON
cmake --build build-single
ctest --test-dir build-single --output-on-failure

# Run the official toml-lang/toml-test compliance suite (validates both v1.1.0 and v1.0.0)
./build/tomlkit11-compliance-runner /path/to/toml-test/tests

# Verify single_include/tomlkit11/tomlkit11.hpp matches include/tomlkit11/
python scripts/amalgamate.py --check
```

Standalone `tomlkit11-decoder` and `tomlkit11-encoder` binaries implementing the standard `toml-test` stdin/stdout JSON protocol are provided under `tools/toml-test-adapter/`.

---

## License

Licensed under the [Apache License 2.0](./LICENSE.txt).
