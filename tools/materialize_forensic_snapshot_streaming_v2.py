#!/usr/bin/env python3
"""Active wrapper for the isolated streaming APAMETA1 materializer.

The first prototype used the identifier `result` both for the forensic-result
parameter and for the local write return code in generated `snapshot_write_streamed`.
That prototype is retained as review history; this wrapper materializes it and
renames only the conflicting local state before the generated C is compiled.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import materialize_forensic_snapshot_streaming as v1

MARKER = "streaming APAMETA1 snapshot experiment v2 scope fix"


def materialize(text: str) -> str:
    out = v1.materialize(text)
    if MARKER in out:
        raise v1.MaterializeError("streaming v2 already materialized")

    start, end = v1.function_span(out, "snapshot_write_streamed")
    body = out[start:end]
    replacements = [
        ("        int result;\n", "        int write_result;\n"),
        ("        result = snapshot_write_exact(fd, chunk, bytes);\n",
         "        write_result = snapshot_write_exact(fd, chunk, bytes);\n"),
        ("        if (result < 0) {\n", "        if (write_result < 0) {\n"),
        ("            return result;\n", "            return write_result;\n"),
    ]
    for old, new in replacements:
        count = body.count(old)
        if count != 1:
            raise v1.MaterializeError(
                f"streaming v2 scope fix expected one {old!r}, found {count}"
            )
        body = body.replace(old, new, 1)

    body = body.replace(
        "{\n    unsigned int offset = 0;\n",
        "{\n    /* " + MARKER + ". */\n    unsigned int offset = 0;\n",
        1,
    )
    out = out[:start] + body + out[end:]

    fixed_start, fixed_end = v1.function_span(out, "snapshot_write_streamed")
    fixed = out[fixed_start:fixed_end]
    if "        int result;\n" in fixed:
        raise v1.MaterializeError("conflicting write result identifier survived")
    if "int write_result;" not in fixed:
        raise v1.MaterializeError("write_result fix missing")
    return out


def selftest() -> None:
    fixture = r'''#define SNAPSHOT_HEADER_BYTES 64u
#define SNAPSHOT_ENTRY_BYTES (4u + 32u + APA_HEADER_SIZE)
#define SNAPSHOT_TRAILER_BYTES 32u
static int build_snapshot_image(const void *result, const void *plan,
                                unsigned char **image_out,
                                unsigned int *size_out)
{
    unsigned char *image = malloc(1024);
    free(image);
    return 0;
}
int forensic_snapshot_save(unsigned int storage, const void *result,
                           const void *plan, char path_out[64])
{
    unsigned char *image = malloc(1024);
    free(image);
    return 0;
}
'''
    out = materialize(fixture)
    assert MARKER in out
    start, end = v1.function_span(out, "snapshot_write_streamed")
    body = out[start:end]
    assert "int write_result;" in body
    assert "write_result = snapshot_write_exact" in body
    assert "return write_result;" in body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("forensic snapshot streaming v2 materializer selftest: PASS")
        return 0
    if args.source is None:
        parser.error("source is required unless --selftest is used")
    args.source.write_text(materialize(args.source.read_text(encoding="utf-8")),
                           encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
