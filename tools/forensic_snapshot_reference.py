#!/usr/bin/env python3
"""Independent APAMETA1 reference serializer/vector for Phase-5 work.

This tool intentionally does not import or materialize the runtime C serializer.
It encodes the documented version-1 snapshot contract directly and pins a small
deterministic vector. Future streaming implementations must reproduce the same
bytes before they are eligible for PS2 hardware testing.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

MAGIC = b"APAMETA1"
VERSION = 1
HEADER_BYTES = 64
APA_HEADER_BYTES = 1024
ENTRY_BYTES = 4 + 32 + APA_HEADER_BYTES
TRAILER_BYTES = 32


def le32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def patch_bit_distance(old_next: int, old_prev: int,
                       new_next: int, new_prev: int) -> int:
    return (old_next ^ new_next).bit_count() + (old_prev ^ new_prev).bit_count()


def serialize(total_sectors: int, map_index: int, confidence: int,
              corroborated_count: int, speculative_count: int,
              entries: list[dict[str, object]]) -> bytes:
    if not entries:
        raise ValueError("at least one APAMETA1 entry is required")

    one_or_two = sum(
        patch_bit_distance(
            int(entry["old_next"]), int(entry["old_prev"]),
            int(entry["new_next"]), int(entry["new_prev"]),
        ) in (1, 2)
        for entry in entries
    )

    image = bytearray(HEADER_BYTES)
    image[0:8] = MAGIC
    image[8:12] = le32(VERSION)
    image[12:16] = le32(total_sectors)
    image[16:20] = le32(map_index)
    image[20:24] = le32(confidence)
    image[24:28] = le32(len(entries))
    image[28:32] = le32(corroborated_count)
    image[32:36] = le32(speculative_count)
    image[36:40] = le32(one_or_two)

    for entry in entries:
        header = bytes(entry["header"])
        if len(header) != APA_HEADER_BYTES:
            raise ValueError("APA header must be exactly 1024 bytes")
        image += le32(int(entry["lba"]))
        image += hashlib.sha256(header).digest()
        image += header

    image += hashlib.sha256(image).digest()
    return bytes(image)


def reference_entries() -> list[dict[str, object]]:
    return [
        {
            "lba": 0x1000,
            "header": bytes(i & 0xFF for i in range(APA_HEADER_BYTES)),
            "old_next": 0x100,
            "old_prev": 0x80,
            "new_next": 0x101,
            "new_prev": 0x80,
        },
        {
            "lba": 0x2000,
            "header": bytes((255 - i) & 0xFF for i in range(APA_HEADER_BYTES)),
            "old_next": 0x200,
            "old_prev": 0x40,
            "new_next": 0x202,
            "new_prev": 0x41,
        },
    ]


def build_reference() -> bytes:
    return serialize(
        total_sectors=0x12345678,
        map_index=1,
        confidence=88,
        corroborated_count=1,
        speculative_count=1,
        entries=reference_entries(),
    )


def selftest() -> dict[str, object]:
    image = build_reference()
    entry0 = reference_entries()[0]
    entry1 = reference_entries()[1]
    header0_sha = hashlib.sha256(bytes(entry0["header"])).hexdigest()
    header1_sha = hashlib.sha256(bytes(entry1["header"])).hexdigest()
    trailer = image[-TRAILER_BYTES:].hex()
    image_sha = hashlib.sha256(image).hexdigest()

    expected = {
        "bytes": 2216,
        "header0_sha256": "785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9",
        "header1_sha256": "3af6dbef8362452d2b45ad97deb9e43180fb90aac309860e26e123860cce62a7",
        "trailer_sha256": "2654254d3abccac8459893f15c96b9f3e186fdf2abf423bdbb2150997431773b",
        "image_sha256": "601ba74fc619738dac19baa2a6cb53054b67803e00b1fccb6bf89c69ef4bab6f",
    }

    assert len(image) == expected["bytes"]
    assert image[0:8] == MAGIC
    assert struct.unpack_from("<I", image, 8)[0] == VERSION
    assert struct.unpack_from("<I", image, 12)[0] == 0x12345678
    assert struct.unpack_from("<I", image, 16)[0] == 1
    assert struct.unpack_from("<I", image, 20)[0] == 88
    assert struct.unpack_from("<I", image, 24)[0] == 2
    assert struct.unpack_from("<I", image, 28)[0] == 1
    assert struct.unpack_from("<I", image, 32)[0] == 1
    assert struct.unpack_from("<I", image, 36)[0] == 2
    assert image[40:64] == bytes(24)
    assert header0_sha == expected["header0_sha256"]
    assert header1_sha == expected["header1_sha256"]
    assert trailer == expected["trailer_sha256"]
    assert image_sha == expected["image_sha256"]

    # Trailer is SHA-256 of every preceding byte, not of the completed image.
    assert hashlib.sha256(image[:-TRAILER_BYTES]).digest() == image[-TRAILER_BYTES:]

    return {
        "format": "APAMETA1",
        "version": VERSION,
        "entry_bytes": ENTRY_BYTES,
        "reference": expected,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--json", type=Path,
                        help="write reference metadata JSON after validation")
    parser.add_argument("--binary", type=Path,
                        help="write the exact reference APAMETA1 image")
    args = parser.parse_args()

    result = selftest()
    if args.json is not None:
        args.json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    if args.binary is not None:
        args.binary.write_bytes(build_reference())
    if args.selftest or (args.json is None and args.binary is None):
        print("forensic snapshot reference selftest: PASS")
        print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
