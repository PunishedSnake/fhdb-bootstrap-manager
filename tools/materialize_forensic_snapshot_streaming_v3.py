#!/usr/bin/env python3
"""Streaming APAMETA1 v3: keep the cold serializer out of repair_plan_screen.

V2 proved the bounded memory model but LTO inlined part of the expanded forensic
snapshot path into the repair UI controller. V3 changes only code placement:
`forensic_snapshot_save()` is explicitly cold and noinline. Data representation,
workspace ownership, fileXio sequencing and verification semantics are identical
to streaming v2.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import materialize_forensic_snapshot_streaming_v2 as v2

MARKER = "streaming APAMETA1 v3 cold noinline boundary"
ATTRIBUTE_SIGNATURE = (
    "__attribute__((noinline, cold))\n"
    "int forensic_snapshot_save("
)


def materialize(text: str) -> str:
    out = v2.materialize(text)
    if MARKER in out:
        raise v2.v1.MaterializeError("streaming v3 already materialized")

    start, end = v2.v1.function_span(out, "forensic_snapshot_save")
    body = out[start:end]
    old = "int forensic_snapshot_save(unsigned int storage,\n"
    new = (
        "/* " + MARKER + ". */\n"
        "__attribute__((noinline, cold))\n"
        "int forensic_snapshot_save(unsigned int storage,\n"
    )
    if body.count(old) != 1:
        raise v2.v1.MaterializeError("forensic_snapshot_save signature mismatch")
    body = body.replace(old, new, 1)
    out = out[:start] + body + out[end:]

    # function_span() intentionally starts at the function declaration. Once the
    # attribute is added on the preceding line it is therefore outside a second
    # span lookup. Validate the generated source directly instead of treating
    # that parser behaviour as a missing attribute.
    if out.count(ATTRIBUTE_SIGNATURE) != 1:
        raise v2.v1.MaterializeError("cold/noinline boundary missing or duplicated")
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
    assert out.count(ATTRIBUTE_SIGNATURE) == 1
    assert "workspace = malloc(workspace_bytes);" in out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("forensic snapshot streaming v3 materializer selftest: PASS")
        return 0
    if args.source is None:
        parser.error("source is required unless --selftest is used")
    args.source.write_text(materialize(args.source.read_text(encoding="utf-8")),
                           encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
