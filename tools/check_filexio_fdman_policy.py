#!/usr/bin/env python3
"""Reject EE runtime call sites that require PS2SDK's Newlib fileXio fd-manager.

The corpus-v2 performance branch deliberately keeps application filesystem I/O on
fileXio's direct RPC API.  A tiny application policy object therefore prevents
fileXioInit() from replacing Newlib's generic POSIX pathname backend.  This saves
code only while src/include do not depend on fopen/open/stat/opendir-style file
access.

This checker turns that runtime contract into a CI invariant.  It scans C-family
project source after masking comments and string/character literals, so prose and
format strings do not create false positives.  If a POSIX/stdio file call is
needed later, review the policy instead of adding an exemption by reflex.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOTS = ("src", "include")
SUFFIXES = {".c", ".h", ".inc", ".s", ".S"}
FORBIDDEN = (
    "fopen", "freopen", "fclose", "fread", "fwrite",
    "open", "close", "stat", "fstat", "lstat",
    "opendir", "readdir", "closedir",
    "remove", "rename", "mkdir", "rmdir", "chdir", "unlink",
)
CALL = re.compile(r"\b(" + "|".join(map(re.escape, FORBIDDEN)) + r")\s*\(")


def mask_non_code(text: str) -> str:
    out = list(text)
    state = "code"
    i = 0
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = "line"
                i += 1
            elif ch == "/" and nxt == "*":
                out[i] = out[i + 1] = " "
                state = "block"
                i += 1
            elif ch == '"':
                out[i] = " "
                state = "string"
            elif ch == "'":
                out[i] = " "
                state = "char"
        elif state == "line":
            if ch == "\n":
                state = "code"
            else:
                out[i] = " "
        elif state == "block":
            if ch == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = "code"
                i += 1
            elif ch != "\n":
                out[i] = " "
        elif state in ("string", "char"):
            if ch == "\\":
                out[i] = " "
                if i + 1 < len(text):
                    if text[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 1
            elif (state == "string" and ch == '"') or (
                    state == "char" and ch == "'"):
                out[i] = " "
                state = "code"
            elif ch != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def source_files(root: Path):
    for dirname in ROOTS:
        directory = root / dirname
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.suffix in SUFFIXES:
                yield path


def scan(root: Path):
    findings = []
    for path in source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = mask_non_code(text)
        for match in CALL.finditer(masked):
            line = masked.count("\n", 0, match.start()) + 1
            findings.append((str(path.relative_to(root)), line, match.group(1)))
    return findings


def selftest() -> int:
    sample = r'''
        fileXioOpen("mass:/x", 1, 0);
        snprintf(buf, sizeof(buf), "fopen(x)");
        /* stat(path); */
        // fwrite(data, 1, n, f);
        int fopen_counter = 0;
        fopen(path, "rb");
        stat(path, &st);
    '''
    masked = mask_non_code(sample)
    hits = [m.group(1) for m in CALL.finditer(masked)]
    if hits != ["fopen", "stat"]:
        print(f"selftest failed: {hits}", file=sys.stderr)
        return 1
    print("fileXio fdman policy checker selftest: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()

    root = Path(args.root).resolve()
    findings = scan(root)
    if findings:
        print("ERROR: direct-fileXio policy violated by POSIX/stdio file calls:",
              file=sys.stderr)
        for path, line, name in findings:
            print(f"  {path}:{line}: {name}()", file=sys.stderr)
        print("Review src/filexio_fdman_policy_ps2.c before using these APIs.",
              file=sys.stderr)
        return 1

    print("fileXio fdman policy: PASS (no POSIX/stdio file calls in src/include)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
