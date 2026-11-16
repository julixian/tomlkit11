#include <tomlkit11/tomlkit11.hpp>

#include <iostream>
#include <string>

bool runCompiledLibImplChecks();

int main()
{
    using namespace tomlkit;

    if (!runCompiledLibImplChecks()) {
        std::cerr << "[FAIL] runCompiledLibImplChecks() failed\n";
        return 1;
    }

    const StringView kSource =
        "# Build config\n"
        "[workspace]\n"
        "opt = 0 # debug\n"
        "tags = [\"a\", \"b\"]\n";

    Item doc = parse(kSource);
    doc.ensure("workspace", "opt") = 3;
    doc.ensure("workspace", "opt").setInlineComment("release");
    doc.ensure("workspace", "tags").push_back("c");

    const std::string out = doc.toToml();
    const Item reparsed = parse(out);
    if (reparsed != doc) {
        std::cerr << "[FAIL] reparsed != doc in compiled lib mode\n";
        return 1;
    }
    if (findOr(reparsed, "workspace", "opt", -1) != 3) {
        std::cerr << "[FAIL] opt mismatch in compiled lib mode\n";
        return 1;
    }
    if (findOr(reparsed, "workspace", "tags", 2, "") != "c") {
        std::cerr << "[FAIL] tags[2] mismatch in compiled lib mode\n";
        return 1;
    }

    std::cout << "test_compiled_lib: all checks passed!\n";
    return 0;
}
