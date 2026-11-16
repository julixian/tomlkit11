#define TOMLKIT11_IMPLEMENTATION
#include <tomlkit11/tomlkit11.hpp>

bool runCompiledLibImplChecks()
{
    const tomlkit::StringView kInput =
        "[package]\n"
        "name = \"tomlkit11\"\n"
        "version = \"1.0.0\"\n";

    tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res = tomlkit::tryParse(kInput);
    if (!res.has_value()) {
        return false;
    }
    tomlkit::Item& doc = *res;
    doc.ensure("package", "cxx") = 11;
    const tomlkit::Result<std::string, tomlkit::ParseError> dumped = tomlkit::tryFormat(doc);
    return dumped.has_value() && dumped->find("cxx = 11") != std::string::npos;
}
