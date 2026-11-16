#include <tomlkit11/tomlkit11.hpp>
#include "TaggedJson.hpp"

#include <iostream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    tomlkit::ParseOptions opts;
    for (int i = 1; i < argc; ++i) {
        const tomlkit::StringView arg(argv[i]);
        if (arg == "-toml=1.0" || arg == "--v1.0") {
            opts.spec = tomlkit::SpecVersion::v1_0;
        }
        else if (arg == "-toml=1.1" || arg == "--v1.1") {
            opts.spec = tomlkit::SpecVersion::v1_1;
        }
    }

    const std::string input(
        (std::istreambuf_iterator<char>(std::cin)),
        std::istreambuf_iterator<char>()
    );

    const tomlkit::Result<tomlkit::Item, tomlkit::ParseError> res =
        tomlkit::tryParse(input, opts, "<stdin>");
    if (!res) {
        std::cerr << res.error().format() << '\n';
        return 1;
    }

    std::cout << tomlkit::test_adapter::itemToTaggedJson(*res) << '\n';
    return 0;
}
