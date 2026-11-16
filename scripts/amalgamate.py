#!/usr/bin/env python3
"""Amalgamate include/tomlkit11 headers and .ipp files into single_include/tomlkit11/tomlkit11.hpp."""

import argparse
import re
import sys
from pathlib import Path
from typing import List, Optional, Tuple

kScriptRoot = Path(__file__).resolve().parent
kRepoRoot = kScriptRoot.parent
kIncludeRoot = kRepoRoot / "include" / "tomlkit11"
kDefaultOutput = kRepoRoot / "single_include" / "tomlkit11" / "tomlkit11.hpp"

kHeaderOrder = (
    "config.hpp",
    "compat.hpp",
    "spec.hpp",
    "error.hpp",
    "types.hpp",
    "decor.hpp",
    "item.hpp",
    "lexer.hpp",
    "parser.hpp",
    "emitter.hpp",
)

kImplOrder = (
    "detail/error.ipp",
    "detail/types.ipp",
    "detail/decor.ipp",
    "detail/item_core.ipp",
    "detail/item_access.ipp",
    "detail/lexer.ipp",
    "detail/parser.ipp",
    "detail/emitter.ipp",
)

kPragmaOnceRe = re.compile(r"^\s*#\s*pragma\s+once\b.*$")
kLocalIncludeRe = re.compile(r'^\s*#\s*include\s*"[^"]+"\s*(?://.*)?$')
kTreeIncludeRe = re.compile(r"^\s*#\s*include\s*<tomlkit11/[^>]+>\s*(?://.*)?$")
kIfDirectiveRe = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
kElseDirectiveRe = re.compile(r"^\s*#\s*(?:else|elif)\b")
kEndifDirectiveRe = re.compile(r"^\s*#\s*endif\b")
kIfndefLineRe = re.compile(r"^\s*#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?://.*)?$")
kDefineLineRe = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?://.*)?$")
kImplGuardStartRe = re.compile(r"^\s*#\s*if\s+TOMLKIT11_SHOULD_INCLUDE_IMPL\b.*$")


def normalizeNewlines(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def stripCommentsForGuardCheck(lines: List[str]) -> List[int]:
    """Return indices of lines that are not blank or single-line comments."""
    inBlockComment = False
    codeIndices: List[int] = []
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if not stripped:
            continue
        if inBlockComment:
            endPos = stripped.find("*/")
            if endPos == -1:
                continue
            stripped = stripped[endPos + 2 :].strip()
            inBlockComment = False
            if not stripped:
                continue
        if stripped.startswith("//"):
            continue
        if stripped.startswith("/*"):
            endPos = stripped.find("*/", 2)
            if endPos == -1:
                inBlockComment = True
                continue
            rest = stripped[endPos + 2 :].strip()
            if not rest:
                continue
        codeIndices.append(idx)
    return codeIndices


def findMatchingEndifLine(lines: List[str], startCodeIdx: int) -> Optional[int]:
    """Verify if directive at startCodeIdx spans until the last directive/code line without top-level #else/#elif."""
    depth = 0
    for idx in range(startCodeIdx, len(lines)):
        line = lines[idx]
        if kIfDirectiveRe.match(line):
            depth += 1
        elif depth == 1 and kElseDirectiveRe.match(line):
            return None
        elif kEndifDirectiveRe.match(line):
            depth -= 1
            if depth == 0:
                return idx
            if depth < 0:
                return None
    return None


def stripOuterWrapper(lines: List[str], isImpl: bool) -> List[str]:
    """Strip outermost include guard (for .hpp) or TOMLKIT11_SHOULD_INCLUDE_IMPL wrapper (for .ipp)."""
    codeIndices = stripCommentsForGuardCheck(lines)
    if len(codeIndices) < 2:
        return lines

    firstIdx = codeIndices[0]
    lastIdx = codeIndices[-1]

    if not isImpl and len(codeIndices) >= 3:
        secondIdx = codeIndices[1]
        mIfndef = kIfndefLineRe.match(lines[firstIdx])
        mDefine = kDefineLineRe.match(lines[secondIdx])
        if mIfndef and mDefine and mIfndef.group(1) == mDefine.group(1):
            matchEnd = findMatchingEndifLine(lines, firstIdx)
            if matchEnd == lastIdx:
                result = lines[:firstIdx] + lines[firstIdx + 1 : secondIdx] + lines[secondIdx + 1 : lastIdx] + lines[lastIdx + 1 :]
                return result

    if isImpl:
        if kImplGuardStartRe.match(lines[firstIdx]):
            matchEnd = findMatchingEndifLine(lines, firstIdx)
            if matchEnd == lastIdx:
                result = lines[:firstIdx] + lines[firstIdx + 1 : lastIdx] + lines[lastIdx + 1 :]
                return result

    return lines


def trimEmptyEdges(lines: List[str]) -> List[str]:
    start = 0
    end = len(lines)
    while start < end and lines[start].strip() == "":
        start += 1
    while end > start and lines[end - 1].strip() == "":
        end -= 1
    return lines[start:end]


def processComponent(relPath: str, isImpl: bool) -> str:
    filePath = kIncludeRoot / relPath
    if not filePath.is_file():
        raise FileNotFoundError(f"Missing required source file: {filePath}")

    rawText = normalizeNewlines(filePath.read_text(encoding="utf-8"))
    lines = rawText.split("\n")

    filtered: List[str] = []
    for line in lines:
        if kPragmaOnceRe.match(line):
            continue
        if kLocalIncludeRe.match(line) or kTreeIncludeRe.match(line):
            continue
        filtered.append(line)

    unwrapped = stripOuterWrapper(filtered, isImpl)
    trimmed = trimEmptyEdges(unwrapped)

    sectionHeader = (
        f"// ----------------------------------------------------------------------------\n"
        f"// {relPath}\n"
        f"// ----------------------------------------------------------------------------"
    )
    body = "\n".join(trimmed)
    return f"{sectionHeader}\n\n{body}\n"


def generateAmalgamatedContent() -> str:
    parts: List[str] = [
        "// Copyright (c) 2026 julixian",
        "// SPDX-License-Identifier: Apache-2.0",
        "//",
        "// tomlkit11 - Single-header amalgamated distribution.",
        "// Auto-generated by scripts/amalgamate.py. Do not edit directly.",
        "",
        "#ifndef TOMLKIT11_SINGLE_INCLUDE_HPP",
        "#define TOMLKIT11_SINGLE_INCLUDE_HPP",
        "",
    ]

    for relHeader in kHeaderOrder:
        parts.append(processComponent(relHeader, isImpl=False))

    parts.append("#if TOMLKIT11_SHOULD_INCLUDE_IMPL")
    parts.append("")

    for relImpl in kImplOrder:
        parts.append(processComponent(relImpl, isImpl=True))

    parts.append("#endif // TOMLKIT11_SHOULD_INCLUDE_IMPL")
    parts.append("")
    parts.append("#endif // TOMLKIT11_SINGLE_INCLUDE_HPP")
    parts.append("")

    return "\n".join(parts)


def parseArgs(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate or verify the single-include amalgamated tomlkit11 header."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify that single_include/tomlkit11/tomlkit11.hpp is up to date without writing.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=kDefaultOutput,
        help="Output path for the amalgamated header.",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parseArgs(argv)
    outputPath: Path = args.output

    try:
        generated = generateAmalgamatedContent()
    except FileNotFoundError as exc:
        print(f"amalgamate.py error: {exc}", file=sys.stderr)
        return 1

    if args.check:
        if not outputPath.is_file():
            print(f"amalgamate.py --check FAILED: missing file {outputPath}", file=sys.stderr)
            return 1
        existing = normalizeNewlines(outputPath.read_text(encoding="utf-8"))
        if existing != generated:
            print(
                f"amalgamate.py --check FAILED: {outputPath} is out of date. Run scripts/amalgamate.py to regenerate.",
                file=sys.stderr,
            )
            return 1
        print(f"amalgamate.py --check OK: {outputPath} is up to date.")
        return 0

    outputPath.parent.mkdir(parents=True, exist_ok=True)
    if outputPath.is_file():
        existing = normalizeNewlines(outputPath.read_text(encoding="utf-8"))
        if existing == generated:
            print(f"amalgamate.py: {outputPath} already up to date.")
            return 0

    outputPath.write_text(generated, encoding="utf-8", newline="\n")
    print(f"amalgamate.py: wrote {outputPath} ({len(generated)} bytes).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
