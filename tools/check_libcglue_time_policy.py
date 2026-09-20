#!/usr/bin/env python3
"""Guard the Phase-1 no-local-time libcglue startup policy.

The application overrides PS2SDK's weak _libcglue_timezone_update() because the
manager has no local civil-time contract. This checker prevents future runtime
code from silently adding APIs that require that startup timezone state.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOTS = ("src", "include")
SUFFIXES = {".c", ".h", ".inc", ".s", ".S"}
FORBIDDEN = (
    "localtime", "localtime_r", "mktime", "ctime", "ctime_r", "strftime",
    "tzset", "_tzset_r", "_tzset_unlocked_r",
    "ps2sdk_setTimezone", "ps2sdk_setDaylightSaving",
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
    policy_file = root / "src" / "filexio_fdman_policy_ps2.c"
    for path in source_files(root):
        if path == policy_file:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = mask_non_code(text)
        for match in CALL.finditer(masked):
            line = masked.count("\n", 0, match.start()) + 1
            findings.append((str(path.relative_to(root)), line, match.group(1)))
    return findings


def selftest() -> int:
    sample = r'''
        GetTimerSystemTime();
        snprintf(buf, sizeof(buf), "localtime(x)");
        /* mktime(&tm); */
        // strftime(buf, n, fmt, &tm);
        int localtime_counter = 0;
        localtime(&now);
        ps2sdk_setTimezone(60);
    '''
    masked = mask_non_code(sample)
    hits = [m.group(1) for m in CALL.finditer(masked)]
    if hits != ["localtime", "ps2sdk_setTimezone"]:
        print(f"selftest failed: {hits}", file=sys.stderr)
        return 1
    print("libcglue time policy checker selftest: PASS")
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
        print("ERROR: no-local-time libcglue policy violated:", file=sys.stderr)
        for path, line, name in findings:
            print(f"  {path}:{line}: {name}()", file=sys.stderr)
        print("Review the _libcglue_timezone_update override before using these APIs.",
              file=sys.stderr)
        return 1

    print("libcglue time policy: PASS (no local-time consumers in src/include)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
