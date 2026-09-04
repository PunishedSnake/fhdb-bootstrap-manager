#!/usr/bin/env python3
"""Materialize the isolated source-fingerprint malloc experiment.

Pinned PS2SDK fileXioRead accepts unaligned EE caller buffers, so 64-byte heap
alignment is not a correctness/API requirement for source_fingerprint(). This
experiment replaces only that helper's memalign(64, 64 KiB) with malloc(64 KiB).
It does not touch the custom hdl0: SIF/DMA buffers, whose 64-byte contract stays
in force.
"""

from __future__ import annotations

import argparse
from pathlib import Path

MARKER = "HDL source fingerprint ordinary-heap experiment"


class MaterializeError(RuntimeError):
    pass


def function_span(text: str, name: str) -> tuple[int, int]:
    token = f"static int {name}("
    start = text.find(token)
    if start < 0:
        raise MaterializeError(f"missing function {name}")
    next_start = text.find("\nstatic int ", start + len(token))
    return start, len(text) if next_start < 0 else next_start


def transform(text: str) -> str:
    if MARKER in text:
        raise MaterializeError("source already contains fingerprint malloc experiment")
    start, end = function_span(text, "source_fingerprint")
    body = text[start:end]
    old = "    buffer = memalign(64, HDL_INSTALL_IO_BYTES);\n"
    if body.count(old) != 1:
        raise MaterializeError(
            f"source_fingerprint: expected one memalign site, found {body.count(old)}"
        )
    body = body.replace(
        old,
        f"    /* {MARKER}. */\n"
        "    buffer = malloc(HDL_INSTALL_IO_BYTES);\n",
        1,
    )
    if "memalign(64, HDL_INSTALL_IO_BYTES)" in body:
        raise MaterializeError("source_fingerprint memalign survived transform")
    return text[:start] + body + text[end:]


def selftest() -> None:
    fixture = r'''static int source_fingerprint(hdl_file_source_t *source,
                              unsigned char digest[32])
{
    unsigned char *buffer;
    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    free(buffer);
    return 0;
}

static int sentinel(void) { return 0; }
'''
    out = transform(fixture)
    assert MARKER in out
    assert "buffer = malloc(HDL_INSTALL_IO_BYTES);" in out
    assert "memalign(64, HDL_INSTALL_IO_BYTES)" not in out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if not args.files:
        parser.error("at least one source file is required unless --selftest is used")
    for path in args.files:
        path.write_text(transform(path.read_text(encoding="utf-8")), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
