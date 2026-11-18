# tomlkit11

简体中文 | [English](./README_EN.md)

[![Try it on Compiler Explorer](https://img.shields.io/badge/Compiler_Explorer-Try_Online-1f6feb?logo=compilerexplorer&logoColor=white)](https://godbolt.org/z/8PqT131j1)

> [!NOTE]
> **如果你的项目已采用 C++23：可直接使用 C++23 原生模块版 [tomlkitpp](https://github.com/julixian/tomlkitpp)**
>
> 当工具链支持 **C++23 原生模块**（`export module tomlkitpp;` + `import std;`）与 [`mcpp`](https://github.com/mcpp-community/mcpp) 时，可以优先考虑 **`tomlkitpp`**：
> - **原生模块编译模型**：通过 BMI 避免大型头文件在多个翻译单元中的重复预处理与解析开销，也无需配置分离编译宏
> - **原生 C++23 接口**：内置 `std::expected`、开箱即用的多维下标 `cfg["a", "b"]`、配合 `std::println` 与 Ranges 使用
> - **API 一致性**：`tomlkit11` 与 `tomlkitpp` 共享 `tomlkit::` 命名空间及相同的 `Item` / `parse` / `findOr` 语义，后续升级到 C++23 模块构建时仅需将 `#include <tomlkit11/tomlkit11.hpp>` 替换为 `import tomlkitpp;`。

`tomlkit11` 是 [tomlkitpp](https://github.com/julixian/tomlkitpp) 的 **C++11 零依赖 Header-Only 姊妹库**（同时提供单头文件 `single_include/tomlkit11/tomlkit11.hpp` 与可选编译库模式），默认支持 **TOML v1.1.0**（并兼容 `SpecVersion::v1_0`），在解析、原位编辑与序列化过程中完整保留原始注释、空行、缩进、引号形态与数字进制。

---

## 与原生模块版 `tomlkitpp` 的对比

| 特性维度 | **[tomlkitpp](https://github.com/julixian/tomlkitpp)（C++23 原生模块）** | **tomlkit11（本仓库）** |
| --- | --- | --- |
| **语言标准要求** | C++23（需支持 `import std;`） | **C++11 起**（自动渐进解锁 C++14/17/20/23 特性） |
| **分发形态** | C++23 原生模块（`export module tomlkitpp;`） | Header-Only / 单头文件 / 可选 `tomlkit11::tomlkit11_compiled` 静态库目标 |
| **构建体系** | [`mcpp`](https://github.com/mcpp-community/mcpp) | CMake (`FetchContent` / `find_package`)、vcpkg、单文件直拷 |
| **错误处理** | 异常 + 原生 `std::expected<T, ParseError>` | 异常 + `tomlkit::Result<T, ParseError>`（C++23 可隐式转 `std::expected`）+ 完整 `-fno-exceptions` 支持 |
| **多维路径访问** | 原生 C++23 `cfg["a", "b", 0]` | C++11–20 使用 `cfg.find("a", "b", 0)` / `cfg.ensure("a", "b")`；C++23 自动解锁 `cfg["a", "b", 0]` |
| **TOML 规范与保真** | TOML v1.1.0 / v1.0.0、100% CST 无损往返 | 与 `tomlkitpp` 一致的词法/语法/CST 序列化引擎 |
| **官方 `toml-test`** | v1.1.0 & v1.0.0 四向全绿 | v1.1.0 & v1.0.0 四向全绿（纯 C++11 合规运行器） |

### 迁移至 `tomlkitpp` 示例

当你日后将项目升级到 C++23 模块构建时，业务代码几乎无需改动：

```cpp
// ==================== 使用 tomlkit11 (C++11+) ====================
#include <tomlkit11/tomlkit11.hpp>

int main()
{
    tomlkit::Item cfg = tomlkit::parse("title = 'demo'\n[server]\nport = 8080\n");
    int port = tomlkit::findOr(cfg, "server", "port", 80);
    cfg.ensure("server", "host") = "127.0.0.1"; // C++23 下也可直接 cfg["server", "host"] = "127.0.0.1";
    std::string out = tomlkit::format(cfg);
    return port == 8080 && !out.empty() ? 0 : 1;
}
```

```cpp
// ==================== 升级至 tomlkitpp (C++23 Modules) ====================
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

## 特性概览

- **默认支持 TOML v1.1.0**（可通过 `SpecVersion::v1_0` 切换为 1.0 规范）：
  - 内联表支持跨多行书写、内部注释与尾随逗号
  - 基本字符串支持 `\e`（ESC）与 `\xHH` 转义
  - 时间与日期时间支持省略秒数（如 `07:32`、`1979-05-27T07:32Z`）
- **保留原始格式与注释**：
  - 完整保留前置注释、行尾注释、数组及内联表内部注释、空行、缩进、CRLF/LF 换行以及 UTF-8 BOM
  - 保留整数进制与下划线分隔（如 `0xff`、`1_000_000`）、字符串引号形态、点分键及表头出现顺序
  - 仅重排被修改的节点，未修改段落与注释原样输出
- **单类型节点与正交查找接口**：
  - 统一使用 `tomlkit::Item`（别名 `tomlkit::Document`），支持 `using toml = tomlkit::Item;`
  - 职责明确的查找函数族：`find`、`findOptional`、`tryFind`、`findOr`、`findOrDefault`、`findArrayOrEmpty`、`findTableOrEmpty`、`ensure`
  - 同时提供抛异常接口（`parse` / `get<T>()`）与无异常接口（`tryParse` / `tryGet<T>()` / `tryFormat` / `trySaveFileAtomic`）
  - 支持特化 `tomlkit::Serializer<T>` 实现自定义结构体双向转换
- **纯 C++11 基线 & 零第三方依赖**：
  - 核心库与 `toml-test` 适配器全部严格遵循 C++11，不依赖 `std::filesystem`、`std::string_view`、`std::optional` 或 `std::variant`
  - 支持 `-fno-exceptions` / `/EHs-c- /D_HAS_EXCEPTIONS=0` 完全无异常编译
- **通过 [`toml-lang/toml-test`](https://github.com/toml-lang/toml-test) 官方测试集**：
  - **TOML v1.1.0**：Valid `218/218`、Invalid `502/502`、往返一致性 `218/218`、Encoder `218/218`
  - **TOML v1.0.0**：Valid `208/208`、Invalid `509/509`、往返一致性 `208/208`、Encoder `208/208`

---

## 与常见 C++ TOML 库的定位对比

| 维度 | **[tomlkitpp](https://github.com/julixian/tomlkitpp)** | **tomlkit11** | **toml++** (v3.4) | **toml11** (v4.4) |
| --- | --- | --- | --- | --- |
| **最低 C++ 标准** | C++23 | **C++11** | C++17 | **C++11** |
| **分发形态** | **原生 C++23 模块** (`export module`) | Header-only / 单头文件 / 可选静态库 | Header-only / 单头文件 / 可选编译库 | Header-only / 单头文件 / 可选预编译库 |
| **TOML 规范支持** | **v1.1.0**（默认）& v1.0.0 | **v1.1.0**（默认）& v1.0.0 | v1.0.0 | **v1.1.0** & v1.0.0 |
| **写回保留注释与空白排版** | **完整保留**（含行内/块内注释、空行、缩进） | **完整保留**（同左） | 不保留（纯 AST，面向只读解析/重新生成） | 部分保留（`preserve_comments` 保留节点关联注释，不保留空行与精确空白） |
| **保留原始字面量与键顺序** | **原样回放**（`0xff_00`、引号风格、点分键、表顺序） | **原样回放**（同左） | 不保留（`table` 默认按键名字典序排列） | 部分保留（`ordered_value` 保序并记录进制/引号枚举，不保留数值下划线等原始文本） |
| **节点类型模型** | 单一 `tomlkit::Item`（原生 `doc["a", "b", 0]`） | 单一 `tomlkit::Item`（C++23 下同样支持多维下标） | `toml::node` 多态派生体系 | 模板化 `toml::basic_value<Config>` |
| **无异常接口 / `-fno-exceptions`** | `std::expected` 接口（`tryParse` / `tryGet`） | **完整支持** `Result<T, E>` 与 `-fno-exceptions` / `/EHs-c-` | **完整支持**（`TOML_EXCEPTIONS=0`） | 提供 `try_parse`，不支持关闭异常编译 |

> **选型参考**：如果只需要解析读取配置、不需要把修改写回原文件，`toml++` 等纯 AST 库非常轻量成熟；如果需要像 Rust `toml_edit` / Python `tomlkit` 那样**在不破坏用户手写注释和排版的前提下程序化增删改配置**，可按项目 C++ 标准选择 `tomlkitpp`（C++23 模块）或 `tomlkit11`（C++11 起）。

---

## C++11 → C++23 渐进增强特性表

`tomlkit11` 自动检测编译期标准宏（MSVC 下优先读取 `_MSVC_LANG`，无需额外开启 `/Zc:__cplusplus`），在保持 C++11 兼容性的同时向高标准用户无感开放现代 C++ 特性：

| 语言标准 | 自动检测宏 | 自动解锁能力 |
| --- | --- | --- |
| **C++11 / C++14**（基线） | `TOMLKIT11_CPLUSPLUS` | 完整解析/编辑/保真序列化、`StringView` / `Optional<T>` / `Result<T, E>`、`find(...)` / `ensure(...)` 变参路径、UTF-8 `std::string` 与 `std::wstring` 文件接口 |
| **C++17** | `TOMLKIT11_HAS_STRING_VIEW` / `TOMLKIT11_HAS_OPTIONAL` / `TOMLKIT11_HAS_FILESYSTEM` / `TOMLKIT11_NODISCARD` | `tomlkit::StringView` 直接别名为 `std::string_view`；`tomlkit::Optional<T>` 别名为 `std::optional<T>`；解锁 `std::filesystem::path` 文件读写重载与 `pathToUtf8` / `utf8ToPath`；支持 `for (auto [k, v] : tbl.tableItems())` 结构化绑定 |
| **C++20** | `TOMLKIT11_HAS_CHAR8_T` | 解锁 `std::u8string_view` 路径重载（`tryParseFile`、`parseFile`、`trySaveFileAtomic`、`saveFileAtomic`） |
| **C++23** | `TOMLKIT11_HAS_MULTIDIM_SUBSCRIPT` / `__cpp_lib_expected` | 解锁多维下标 `cfg["a", "b", 0]` 链式读写；`tomlkit::Result<T, E>` 自动支持隐式转换为 `std::expected<T, E>` |

---

## 接入方式与构建加速

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

### 2. CMake 安装与 `find_package`

```bash
cmake -B build -DTOMLKIT11_BUILD_TESTS=OFF -DTOMLKIT11_BUILD_TOOLS=OFF
cmake --install build --prefix /path/to/install
```

```cmake
find_package(tomlkit11 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE tomlkit11::tomlkit11)
```

### 3. 单头文件直拷（Single-Include）

直接复制仓库中的 [`single_include/tomlkit11/tomlkit11.hpp`](./single_include/tomlkit11/tomlkit11.hpp) 到你的工程头文件目录即可使用：

```cpp
#include "tomlkit11.hpp"
```

在 CMake 中也可通过 `-DTOMLKIT11_USE_SINGLE_INCLUDE=ON` 切换为单头文件模式构建测试与工具。

### 4. vcpkg 接入

仓库在 [`packaging/vcpkg/`](./packaging/vcpkg) 提供了符合官方规范的 port 文件，可通过 overlay port 本地试用（或待收录后直接 `vcpkg install tomlkit11`）：

```bash
vcpkg install tomlkit11 --overlay-ports=/path/to/tomlkit11/packaging/vcpkg
```

### 5. 编译加速模式（可选分离编译）

1. **CMake 同树构建（`FetchContent` / `add_subdirectory`）**：如果多个 `.cpp` 翻译单元引入 `tomlkit11`，可直接链接内置静态库目标 `tomlkit11::tomlkit11_compiled`（内部自动以当前工程同一 C++ 标准编译 `src/tomlkit11.cpp` 并传播 `TOMLKIT11_COMPILED_LIB` 宏），**无需手建实现文件或手动配置宏**：

   ```cmake
   target_link_libraries(your_target PRIVATE tomlkit11::tomlkit11_compiled)
   ```

   > **注**：`vcpkg` 与 `cmake --install` 产物有意保持纯 Header-Only（`tomlkit11::tomlkit11`），因为预编译 `.lib` 与不同 C++ 标准（如 C++14 编库 + C++17/20/23 消费端切换 `std::string_view` / `std::optional` / `std::filesystem::path`）混用会产生 ABI 符号不匹配，且 MSBuild `.sln` 工程不会自动传播 CMake 目标宏。

2. **`vcpkg` 安装引入 / VS `.sln`（`.vcxproj`）/ 单头文件项目**：在工程属性（「预处理器定义 / Preprocessor Definitions」或编译选项）中全局添加 `TOMLKIT11_COMPILED_LIB`，并在工程内任意一个 `.cpp`（例如现有的 `main.cpp`、`pch.cpp` 或单独新建的 `.cpp`）顶部加入：

   ```cpp
   #define TOMLKIT11_IMPLEMENTATION
   #include <tomlkit11/tomlkit11.hpp>
   ```

> [!NOTE]
> 在 C++23 原生模块版 [tomlkitpp](https://github.com/julixian/tomlkitpp) 中，头文件重复解析问题已由模块 BMI 机制原生解决，无需配置编译宏。

### 6. 无异常编译（`-fno-exceptions`）

`tomlkit11` 自动识别 GCC/Clang 的 `-fno-exceptions` 与 MSVC 的 `/EHs-c- /D_HAS_EXCEPTIONS=0`（也可手动 `#define TOMLKIT11_NO_EXCEPTIONS 1` 强制开启）。在无异常模式下：
- `TOMLKIT11_HAS_EXCEPTIONS` 自动置为 `0`，头文件内不编译任何 `throw` / `try` / `catch` 与 `<stdexcept>` 依赖。
- 可使用 `tryParse`、`tryParseFile`、`tryFormat`、`trySaveFileAtomic`、`tryGet<T>()`、`tryFind<T>()`、`findOr`、`findOrDefault<T>`、`findArrayOrEmpty`、`findTableOrEmpty`、`ensure`、`findKey` 完成全部解析、修改与写盘操作。
- 若错误调用在无异常模式下无法继续的变异操作（如对整数节点强行 `push_back`），默认触发 `TOMLKIT11_ABORT()`（可通过 `#define TOMLKIT11_ABORT() ...` 自定义）。

---

## 快速上手

### 1. 解析、查找与原位修改（C++11）

```cpp
#include <tomlkit11/tomlkit11.hpp>
#include <iostream>
#include <string>
#include <vector>

using toml = tomlkit::Item;

int main()
{
    toml cfg = tomlkit::parse(
        "# 应用配置\n"
        "title = \"GalTranslPP\"\n\n"
        "[server]\n"
        "endpoints = [\n"
        "    { host = \"127.0.0.1\", port = 8080, }, # v1.1 尾随逗号与内联表\n"
        "]\n"
        "wakeup = 07:30 # v1.1 省略秒数的时间\n"
    );

    // 任意深度的路径查找：C++11 起统一支持变参混排键名与数组下标
    std::string title = tomlkit::find<std::string>(cfg, "title");
    tomlkit::Optional<std::vector<std::string>> tagsOpt =
        tomlkit::findOptional<std::vector<std::string>>(cfg, "tags");
    tomlkit::Optional<int> portOpt =
        tomlkit::tryFind<int>(cfg, "server", "endpoints", 0, "port");
    int port = tomlkit::findOr(cfg, "server", "endpoints", 0, "port", 80);

    // 原位修改与安全建表（不破坏原有行尾注释、缩进与其它段落）
    cfg["server"]["endpoints"][0]["port"] = 9090;
    cfg.ensure("server", "timeoutMs") = 5000;
    cfg["server"]["timeoutMs"].addLeadingComment("连接超时（毫秒）");
    cfg["server"]["timeoutMs"].setInlineComment("新增配置项");

    std::cout << tomlkit::format(cfg);
    return (title == "GalTranslPP" && !tagsOpt.has_value() && portOpt.has_value() && port == 8080) ? 0 : 1;
}
```

### 2. 无异常解析与诊断输出

```cpp
tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res = tomlkit::tryParseFile("config.toml");
if (!res) {
    // 输出包含文件路径、行列号及源码位置指示（^）的诊断文本
    std::cerr << res.error().formatDiagnostic() << '\n';
    return 1;
}
tomlkit::Item& cfg = *res;
```

### 3. 自定义类型转换

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

### 4. 原子写盘与规范版本指定

```cpp
// 先写入临时文件再原子替换目标文件，支持直接传入 UTF-8 字符串路径或 std::wstring / fs::path
tomlkit::saveFileAtomic("D:/项目配置/config.toml", cfg);

// 如需兼容仅支持 TOML v1.0 的下游工具，可在格式化时指定 SpecVersion::v1_0
std::string v10Text = tomlkit::format(cfg, tomlkit::FormatOptions::v1_0());
```

---

## 构建与测试

```bash
# 配置、编译并运行单元测试（支持 C++11 / 14 / 17 / 20 / 23）
cmake -B build -G Ninja -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build build
ctest --test-dir build --output-on-failure

# 测试单头文件模式
cmake -B build-single -G Ninja -DTOMLKIT11_USE_SINGLE_INCLUDE=ON
cmake --build build-single
ctest --test-dir build-single --output-on-failure

# 运行官方 toml-lang/toml-test 合规套件（同时覆盖 v1.1.0 与 v1.0.0）
./build/tomlkit11-compliance-runner /path/to/toml-test/tests

# 校验单头文件是否与 include/tomlkit11/ 同步
python scripts/amalgamate.py --check
```

`tools/toml-test-adapter/` 目录同时提供遵循 `toml-test` 标准 stdin/stdout JSON 协议的 `tomlkit11-decoder` 与 `tomlkit11-encoder` 可执行程序。

---

## 许可证

本项目采用 [Apache License 2.0](./LICENSE.txt) 许可证开源。
