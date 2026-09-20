#!/usr/bin/env python3
"""Enforce the source contract required by printf_policy_ps2.c.

All formatting that reaches the wrapped snprintf/vsnprintf path must remain
integer/string-only. A deliberately small set of opaque variadic pass-through
sites is allowed only because their callers are themselves covered by the format
audit. Any new opaque bridge forces review rather than silently inheriting this
policy.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

import printf_format_audit as audit

EXPECTED_OPAQUE = Counter({
    ("src/boot_report.c", "vsnprintf", "format"): 1,
    ("src/forensic_controller_ps2.c", "vsnprintf", "format"): 1,
    ("src/gs_debug_compat_ps2.c", "gs_ui_console_vprintf", "format"): 2,
    ("src/gs_ui_ps2.c", "vsnprintf", "format"): 1,
    ("src/gs_ui_ps2.c", "gs_ui_console_vprintf", "format"): 1,
    ("src/printf_policy_ps2.c", "vsniprintf", "format"): 2,
    ("src/session_log.c", "vsnprintf", "format"): 2,
})


def app_name_is_safe(root: Path) -> bool:
    header = (root / "include" / "app_identity.h").read_text(
        encoding="utf-8", errors="replace")
    match = re.search(r'^\s*#\s*define\s+APP_NAME\s+"((?:\\.|[^"\\])*)"\s*$',
                      header, re.M)
    return bool(match) and "%" not in match.group(1)


def selftest() -> int:
    if sum(EXPECTED_OPAQUE.values()) != 10:
        print("selftest failed: opaque bridge baseline changed", file=sys.stderr)
        return 1
    print("integer formatter policy checker selftest: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()

    root = Path(args.root).resolve()
    sites = [site for path in audit.source_files(root)
             for site in audit.scan_file(path, root)]
    floats = [site for site in sites
              if any(c in audit.FLOAT_CONVERSIONS for c in site.conversions)]
    malformed = [site for site in sites if "?" in site.conversions]
    opaque = Counter((site.path, site.api, site.format_expr) for site in sites
                     if site.literal is None and not site.fragment_text)
    partial = [site for site in sites
               if site.literal is None and site.fragment_text]
    unexpected_partial = [site for site in partial
                          if not site.format_expr.startswith("APP_NAME ")]

    failed = False
    if floats:
        failed = True
        print("ERROR: floating printf conversion under integer-only policy:",
              file=sys.stderr)
        for site in floats:
            print(f"  {site.path}:{site.line}: {site.api}({site.format_expr})",
                  file=sys.stderr)
    if malformed:
        failed = True
        print("ERROR: malformed/unknown printf conversion:", file=sys.stderr)
        for site in malformed:
            print(f"  {site.path}:{site.line}: {site.api}({site.format_expr})",
                  file=sys.stderr)
    if opaque != EXPECTED_OPAQUE:
        failed = True
        print("ERROR: opaque formatter bridge set changed:", file=sys.stderr)
        for key in sorted(set(opaque) | set(EXPECTED_OPAQUE)):
            if opaque[key] != EXPECTED_OPAQUE[key]:
                print(f"  {key}: actual={opaque[key]} expected={EXPECTED_OPAQUE[key]}",
                      file=sys.stderr)
    if unexpected_partial:
        failed = True
        print("ERROR: new macro/partially literal formatter expression:",
              file=sys.stderr)
        for site in unexpected_partial:
            print(f"  {site.path}:{site.line}: {site.api}({site.format_expr})",
                  file=sys.stderr)
    if not app_name_is_safe(root):
        failed = True
        print("ERROR: APP_NAME must remain a percent-free string literal while it is used in format concatenation.",
              file=sys.stderr)

    if failed:
        print("Review src/printf_policy_ps2.c before changing the format contract.",
              file=sys.stderr)
        return 1

    print(f"integer formatter policy: PASS ({len(sites)} audited sites, no floating conversions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
