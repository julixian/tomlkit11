#pragma once

#ifndef TOMLKIT11_SPEC_HPP
#define TOMLKIT11_SPEC_HPP

#include "compat.hpp"

TOMLKIT11_NAMESPACE_BEGIN

constexpr const char* kLibraryVersion = "1.0.0";

enum class SpecVersion : uint8_t
{
    v1_0,
    v1_1
};

struct ParseOptions
{
    SpecVersion spec = SpecVersion::v1_1;
    uint32_t maxNestingDepth = 128;

    ParseOptions(SpecVersion s = SpecVersion::v1_1, uint32_t maxDepth = 128) noexcept
        : spec(s)
        , maxNestingDepth(maxDepth)
    { }

    static ParseOptions v1_0() noexcept
    {
        return ParseOptions(SpecVersion::v1_0);
    }
};

struct FormatOptions
{
    SpecVersion spec = SpecVersion::v1_1;
    StringView indent = "  ";
    StringView newline = "\n";
    uint32_t maxLineWidth = 80;
    bool preferTrailingNewline = true;

    FormatOptions(
        SpecVersion s = SpecVersion::v1_1,
        StringView ind = "  ",
        StringView nl = "\n",
        uint32_t width = 80,
        bool trailingNl = true
    ) noexcept
        : spec(s)
        , indent(ind)
        , newline(nl)
        , maxLineWidth(width)
        , preferTrailingNewline(trailingNl)
    { }

    static FormatOptions v1_0() noexcept
    {
        return FormatOptions(SpecVersion::v1_0);
    }
};

TOMLKIT11_NAMESPACE_END

#endif // TOMLKIT11_SPEC_HPP
