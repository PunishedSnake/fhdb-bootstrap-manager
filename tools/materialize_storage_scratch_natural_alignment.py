#!/usr/bin/env python3
"""Materialize the first explicit-alignment cleanup experiment.

Only two 1024-byte static buffers are changed:
- header_backup.c::backup_scratch
- repair_snapshot.c::snapshot_verify

Both are consumed by ordinary fileXio-backed reads and CPU parsing/memcmp. Pinned
PS2SDK does not require 64-byte caller alignment for these paths. This tool does
not touch pad, GIF-DMA, hdl0: SIF/DMA, or raw custom device buffers.
"""

from __future__ import annotations

import argparse
from pathlib import Path

MARKER = "natural-alignment storage scratch experiment"

SITES = {
    "header_backup.c": (
        "static unsigned char backup_scratch[APA_HEADER_SIZE]\n"
        "    __attribute__((aligned(64)));\n",
        "static unsigned char backup_scratch[APA_HEADER_SIZE];\n"
        f"/* {MARKER}: ordinary fileXio + CPU consumer. */\n",
    ),
    "repair_snapshot.c": (
        "static unsigned char snapshot_verify[APA_HEADER_SIZE]\n"
        "    __attribute__((aligned(64)));\n",
        "static unsigned char snapshot_verify[APA_HEADER_SIZE];\n"
        f"/* {MARKER}: ordinary fileXio + CPU consumer. */\n",
    ),
}


class MaterializeError(RuntimeError):
    pass


def transform(path: Path, text: str) -> str:
    key = path.name
    if key not in SITES:
        raise MaterializeError(f"unsupported file {path}")
    if MARKER in text:
        raise MaterializeError(f"{path}: experiment already materialized")
    old, new = SITES[key]
    count = text.count(old)
    if count != 1:
        raise MaterializeError(f"{path}: expected one alignment site, found {count}")
    out = text.replace(old, new, 1)
    if old in out:
        raise MaterializeError(f"{path}: old alignment survived")
    return out


def selftest() -> None:
    for name, (old, _) in SITES.items():
        path = Path(name)
        out = transform(path, old + "static int sentinel;\n")
        assert MARKER in out
        assert "aligned(64)" not in out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if not args.files:
        parser.error("source files are required unless --selftest is used")
    for path in args.files:
        path.write_text(transform(path, path.read_text(encoding="utf-8")), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
