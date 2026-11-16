#include <tomlkit11/tomlkit11.hpp>

#include <iostream>
#include <string>

int main()
{
    using namespace tomlkit;

    const StringView kSource =
        "# Package config\n"
        "[package]\n"
        "name = \"tomlkit11\" # inline comment\n"
        "tags = [\"toml\", \"v1.1\"]\n";

    Item doc = parse(kSource);
    doc.ensure("package", "version") = "1.0.0";
    doc.ensure("package", "tags").push_back("cpp11");

    const std::string formatted = format(doc);
    const Item reparsed = parse(formatted);
    if (reparsed != doc) {
        std::cerr << "[FAIL] reparsed != doc in compiled target mode\n";
        return 1;
    }
    if (findOr(reparsed, "package", "version", "") != "1.0.0") {
        std::cerr << "[FAIL] version mismatch in compiled target mode\n";
        return 1;
    }
    if (findOr(reparsed, "package", "tags", 2, "") != "cpp11") {
        std::cerr << "[FAIL] tags[2] mismatch in compiled target mode\n";
        return 1;
    }

    std::cout << "test_compiled_target: all checks passed!\n";
    return 0;
}
