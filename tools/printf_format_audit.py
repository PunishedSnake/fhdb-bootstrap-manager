#!/usr/bin/env python3
"""Audit application printf-family format contracts.

This is a Phase-1 evidence tool, not a formatter replacement. It scans runtime
source, extracts direct calls to libc/debug/application formatting APIs and also
follows the small local variadic append bridges whose format argument is passed
through to vsnprintf(). The goal is to establish the actual conversion contract
before any integer-only Newlib path is considered.
"""

from __future__ import annotations

import argparse
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

ROOTS = ("src", "iop")
SUFFIXES = {".c", ".inc", ".S", ".s"}

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
}

# Static local wrappers with different signatures but the same name are keyed by
# file. These are intentionally explicit so a new bridge appears as an unresolved
# dynamic vsnprintf site instead of being silently guessed by the audit.
FILE_FORMAT_APIS = {
    ("src/session_log.c", "append_text"): 3,
    ("src/boot_report.c", "report_append"): 3,
    ("src/forensic_controller_ps2.c", "report_append"): 1,
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
    fragment_text: str
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


def string_token_bodies(expr: str) -> list[str]:
    bodies: list[str] = []
    for token in STRING_TOKEN.finditer(expr):
        raw = token.group(0)
        quote = raw.find('"')
        bodies.append(raw[quote + 1:-1])
    return bodies


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
        chunks.append(token[quote + 1:-1])
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
    rel = str(path.relative_to(root))
    sites: list[Site] = []
    for match in IDENT.finditer(text):
        api = match.group(0)
        fmt_index = FILE_FORMAT_APIS.get((rel, api), FORMAT_APIS.get(api))
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

        # Function definitions are not calls. Excluding them removes the most
        # misleading kind of dynamic-format noise without trying to parse all C.
        after = closing + 1
        while after < len(text) and text[after].isspace():
            after += 1
        if after < len(text) and text[after] == "{":
            continue

        args = split_args(text[pos + 1:closing])
        if fmt_index >= len(args):
            continue
        expr = args[fmt_index]
        lit = literal_string(expr)
        fragments = "".join(string_token_bodies(expr))
        conv_source = lit if lit is not None else fragments
        sites.append(Site(
            rel,
            raw.count("\n", 0, match.start()) + 1,
            api,
            " ".join(expr.split()),
            lit,
            fragments,
            conversions(conv_source),
        ))
    return sites


def selftest() -> None:
    assert literal_string('"x=%08x"') == "x=%08x"
    assert literal_string('"a" "b%llu"') == "ab%llu"
    assert literal_string('APP_NAME " %s"') is None
    assert string_token_bodies('APP_NAME " %s"') == [" %s"]
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
    float_sites = [site for site in sites
                   if any(c in FLOAT_CONVERSIONS for c in site.conversions)]
    malformed_sites = [site for site in sites if "?" in site.conversions]
    opaque_dynamic = [site for site in dynamic_sites if not site.fragment_text]

    lines = [
        "PS2 HDD Bootstrap Manager - printf-family format contract audit",
        "",
        "Epistemic status",
        "  CURRENT IMPLEMENTATION: source-level formatter and local bridge inventory.",
        "  INFERENCJA: integer-only policy is safe only after opaque dynamic bridges",
        "             are traced to callers and hardware correctness is tested.",
        "",
        f"formatter call sites: {len(sites)}",
        f"fully literal format sites: {len(literal_sites)}",
        f"dynamic/macro format sites: {len(dynamic_sites)}",
        f"opaque dynamic format sites: {len(opaque_dynamic)}",
        f"floating-conversion sites in visible string tokens: {len(float_sites)}",
        f"malformed/unknown visible conversions: {len(malformed_sites)}",
        "",
        "API counts",
    ]
    lines += [f"  {name:28s} {count}" for name, count in sorted(api_counts.items())]
    lines += ["", "Visible conversion counts"]
    lines += [f"  %{name:3s} {count}" for name, count in sorted(conv_counts.items())]

    lines += ["", "Floating conversion sites"]
    if float_sites:
        for site in float_sites:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    lines += ["", "Opaque dynamic format sites requiring caller review"]
    if opaque_dynamic:
        for site in opaque_dynamic:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    lines += ["", "Macro/partially literal format sites"]
    partial = [site for site in dynamic_sites if site.fragment_text]
    if partial:
        for site in partial:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    lines += ["", "Malformed/unknown visible conversion sites"]
    if malformed_sites:
        for site in malformed_sites:
            lines.append(f"  {site.path}:{site.line}: {site.api}({site.format_expr})")
    else:
        lines.append("  none")

    Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:14]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
