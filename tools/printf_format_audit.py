#!/usr/bin/env python3
"""Audit application printf-family format contracts.

This is a Phase-1 evidence tool, not a formatter replacement.  It scans the
runtime source tree, extracts direct calls to libc/debug/application formatting
APIs, classifies literal conversion specifiers and reports dynamic format
arguments that require manual call-graph review before an integer-only formatter
policy can be adopted.
"""

from __future__ import annotations

import argparse
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

ROOTS = ("src", "include", "iop")
SUFFIXES = {".c", ".h", ".inc", ".S", ".s"}

# value = zero-based index of the format argument
FORMAT_APIS = {
    "printf": 0,
    "vprintf": 0,
    "fprintf": 1,
    "vfprintf": 1,
    "sprintf": 1,
    "vsprintf": 1,
    "snprintf": 2,
    "vsnprintf": 2,
    "iprintf": 0,
    "viprintf": 0,
    "fiprintf": 1,
    "vfiprintf": 1,
    "siprintf": 1,
    "vsiprintf": 1,
    "sniprintf": 2,
    "vsniprintf": 2,
    "scr_printf": 0,
    "scr_vprintf": 0,
    "gs_ui_console_printf": 0,
    "gs_ui_console_vprintf": 0,
    "session_log_line": 0,
    "append_text": 3,
}

FLOAT_CONVERSIONS = set("aAeEfFgG")
VALID_CONVERSIONS = set("diouxXfFeEgGaAcspn%")
IDENT = re.compile(r"[A-Za-z_]\w*")
STRING_TOKEN = re.compile(r'(?:u8|u|U|L)?"(?:\\.|[^"\\])*"', re.S)


@dataclass
class Site:
    path: str
    line: int
    api: str
    format_expr: str
    literal: str | None
    conversions: tuple[str, ...]


def source_files(root: Path):
    for base in ROOTS:
        directory = root / base
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.suffix in SUFFIXES:
                yield path


def mask_comments(text: str) -> str:
    out = list(text)
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "/" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 1
                state = "line"
            elif ch == "/" and nxt == "*":
                out[i] = out[i + 1] = " "
                i += 1
                state = "block"
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\":
                i += 1
            elif ch == "'":
                state = "code"
        elif state == "line":
            if ch == "\n":
                state = "code"
            else:
                out[i] = " "
        elif state == "block":
            if ch == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 1
                state = "code"
            elif ch != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def matching_paren(text: str, opening: int) -> int | None:
    depth = 0
    state = "code"
    i = opening
    while i < len(text):
        ch = text[i]
        if state == "code":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    return i
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\":
                i += 1
            elif ch == "'":
                state = "code"
        i += 1
    return None


def split_args(text: str) -> list[str]:
    args: list[str] = []
    start = 0
    paren = bracket = brace = 0
    state = "code"
    i = 0
    while i < len(text):
        ch = text[i]
        if state == "code":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "(":
                paren += 1
            elif ch == ")":
                paren -= 1
            elif ch == "[":
                bracket += 1
            elif ch == "]":
                bracket -= 1
            elif ch == "{":
                brace += 1
            elif ch == "}":
                brace -= 1
            elif ch == "," and paren == bracket == brace == 0:
                args.append(text[start:i].strip())
                start = i + 1
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\":
                i += 1
            elif ch == "'":
                state = "code"
        i += 1
    args.append(text[start:].strip())
    return args


def literal_string(expr: str) -> str | None:
    pos = 0
    chunks: list[str] = []
    while pos < len(expr):
        while pos < len(expr) and expr[pos].isspace():
            pos += 1
        match = STRING_TOKEN.match(expr, pos)
        if not match:
            return None
        token = match.group(0)
        quote = token.find('"')
        body = token[quote + 1:-1]
        # Percent signs and conversion letters are ASCII, so decoding C escapes
        # is unnecessary for the contract audit. Preserve escaped percent text.
        chunks.append(body)
        pos = match.end()
    return "".join(chunks)


def conversions(fmt: str) -> tuple[str, ...]:
    result: list[str] = []
    i = 0
    while i < len(fmt):
        if fmt[i] != "%":
            i += 1
            continue
        i += 1
        if i < len(fmt) and fmt[i] == "%":
            i += 1
            continue
        while i < len(fmt) and fmt[i] in "#0- +'":
            i += 1
        if i < len(fmt) and fmt[i] == "*":
            i += 1
        else:
            while i < len(fmt) and fmt[i].isdigit():
                i += 1
        if i < len(fmt) and fmt[i] == ".":
            i += 1
            if i < len(fmt) and fmt[i] == "*":
                i += 1
            else:
                while i < len(fmt) and fmt[i].isdigit():
                    i += 1
        if fmt[i:i + 2] in ("hh", "ll"):
            i += 2
        elif i < len(fmt) and fmt[i] in "hljztL":
            i += 1
        if i >= len(fmt):
            result.append("?")
            break
        conv = fmt[i]
        result.append(conv if conv in VALID_CONVERSIONS else "?")
        i += 1
    return tuple(result)


def scan_file(path: Path, root: Path) -> list[Site]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = mask_comments(raw)
    sites: list[Site] = []
    for match in IDENT.finditer(text):
        api = match.group(0)
        fmt_index = FORMAT_APIS.get(api)
        if fmt_index is None:
            continue
        pos = match.end()
        while pos < len(text) and text[pos].isspace():
            pos += 1
        if pos >= len(text) or text[pos] != "(":
            continue
        closing = matching_paren(text, pos)
        if closing is None:
            continue
        args = split_args(text[pos + 1:closing])
        if fmt_index >= len(args):
            continue
        expr = args[fmt_index]
        lit = literal_string(expr)
        sites.append(Site(
            str(path.relative_to(root)),
            raw.count("\n", 0, match.start()) + 1,
            api,
            " ".join(expr.split()),
            lit,
            conversions(lit) if lit is not None else (),
        ))
    return sites


def selftest() -> None:
    assert literal_string('"x=%08x"') == "x=%08x"
    assert literal_string('"a" "b%llu"') == "ab%llu"
    assert literal_string("format") is None
    assert conversions("x=%08x %% %llu %s") == ("x", "u", "s")
    assert conversions("%7.2f %.*g") == ("f", "g")
    assert conversions("%zu %p %c") == ("u", "p", "c")
    print("printf-format audit selftest: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--output", default="PRINTF_FORMAT_AUDIT.txt")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0

    root = Path(args.root).resolve()
    sites = [site for path in source_files(root) for site in scan_file(path, root)]
    api_counts = Counter(site.api for site in sites)
    conv_counts = Counter(c for site in sites for c in site.conversions)
    literal_sites = [site for site in sites if site.literal is not None]
    dynamic_sites = [site for site in sites if site.literal is None]
    float_sites = [site for site in literal_sites
                   if any(c in FLOAT_CONVERSIONS for c in site.conversions)]
    malformed_sites = [site for site in literal_sites if "?" in site.conversions]

    lines = [
        "PS2 HDD Bootstrap Manager - printf-family format contract audit",
        "",
        "Epistemic status",
        "  CURRENT IMPLEMENTATION: direct source-level formatter call inventory.",
        "  INFERENCJA: integer-only policy is safe only after dynamic bridges are",
        "             traced to their callers and hardware correctness is tested.",
        "",
        f"formatter call sites: {len(sites)}",
        f"literal format sites: {len(literal_sites)}",
        f"dynamic format sites: {len(dynamic_sites)}",
        f"literal floating-conversion sites: {len(float_sites)}",
        f"malformed/unknown literal conversions: {len(malformed_sites)}",
        "",
        "API counts",
    ]
    lines += [f"  {name:28s} {count}" for name, count in sorted(api_counts.items())]
    lines += ["", "Literal conversion counts"]
    lines += [f"  %{name:3s} {count}" for name, count in sorted(conv_counts.items())]

    lines += ["", "Floating literal format sites"]
    if float_sites:
        for site in float_sites:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    lines += ["", "Dynamic format sites requiring caller review"]
    if dynamic_sites:
        for site in dynamic_sites:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    lines += ["", "Malformed/unknown literal conversion sites"]
    if malformed_sites:
        for site in malformed_sites:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:12]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
