#!/usr/bin/env python3
"""Generate large sparse raw HDD images for APA forensic graph regression."""

import argparse
import json
import os
import struct

SECTOR_SIZE = 512
APA_HEADER_SIZE = 1024
TOTAL_SECTORS = 0x100000  # 512 MiB logical image, sparse on disk.
IMAGE_BYTES = TOTAL_SECTORS * SECTOR_SIZE

APA_MAGIC = 0x004
APA_NEXT = 0x008
APA_PREV = 0x00C
APA_ID = 0x010
APA_START = 0x040
APA_LENGTH = 0x044
APA_TYPE = 0x048
APA_FLAGS = 0x04A
APA_NSUB = 0x04C
APA_MAIN = 0x058
APA_NUMBER = 0x05C
APA_MBR_MAGIC = 0x100
APA_MBR_VERSION = 0x120
APA_SUBS = 0x200

A = 0x40000
B = 0x80000
C = 0xC0000
OFFGRID_SUB = 0x48000


def w16(buf, off, value):
    struct.pack_into("<H", buf, off, value)


def w32(buf, off, value):
    struct.pack_into("<I", buf, off, value)


def checksum(header):
    return sum(struct.unpack_from("<I", header, i * 4)[0]
               for i in range(1, 256)) & 0xFFFFFFFF


def finish(header):
    w32(header, 0, checksum(header))
    return header


def part(lba, length, prev, next_lba, ident, ptype=0x0100,
         flags=0, main=0, number=0):
    h = bytearray(APA_HEADER_SIZE)
    h[APA_MAGIC:APA_MAGIC + 4] = b"APA\0"
    ident_bytes = ident.encode("ascii")[:31]
    h[APA_ID:APA_ID + len(ident_bytes)] = ident_bytes
    w32(h, APA_NEXT, next_lba)
    w32(h, APA_PREV, prev)
    w32(h, APA_START, lba)
    w32(h, APA_LENGTH, length)
    w16(h, APA_TYPE, ptype)
    w16(h, APA_FLAGS, flags)
    w32(h, APA_MAIN, main)
    w32(h, APA_NUMBER, number)
    return finish(h)


def master(prev=C, next_lba=A):
    h = part(0, 0x4000, prev, next_lba, "__mbr", ptype=1)
    h[APA_MBR_MAGIC:APA_MBR_MAGIC + 32] = \
        b"Sony Computer Entertainment Inc."
    w32(h, APA_MBR_VERSION, 2)
    return finish(h)


def healthy_headers():
    return {
        0: master(),
        A: part(A, 0x40000, 0, B, "__system"),
        B: part(B, 0x40000, A, C, "+OPL"),
        C: part(C, 0x40000, B, 0, "PP.HDL.GAME"),
    }


def write_image(path, headers):
    with open(path, "wb") as image:
        image.truncate(IMAGE_BYTES)
        for lba, header in sorted(headers.items()):
            image.seek(lba * SECTOR_SIZE)
            image.write(header)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)
    cases = []

    def add(name, headers, expectation):
        write_image(os.path.join(args.output, name + ".raw"), headers)
        cases.append({"name": name, "expectation": expectation})

    add("healthy_chain", healthy_headers(), "forward-valid-no-repair")

    headers = healthy_headers()
    w32(headers[A], APA_NEXT, 0x90000)  # retain old checksum
    add("broken_next_stale_checksum", headers,
        "one-checksum-corroborated-link-repair")

    headers = healthy_headers()
    w32(headers[A], APA_NEXT, B ^ 0x3)  # exactly two changed bits
    add("two_bit_next_stale_checksum", headers,
        "two-bit-checksum-corroborated-link-repair")

    headers = healthy_headers()
    w32(headers[A], APA_NEXT, 0x90000)
    finish(headers[A])
    add("checksummed_wrong_next", headers,
        "high-confidence-manual-only")

    headers = {
        0: master(prev=A, next_lba=A),
        A: part(A, 0x40000, 0, 0, "MAIN"),
    }
    w32(headers[A], APA_NSUB, 1)
    w32(headers[A], APA_SUBS, OFFGRID_SUB)
    w32(headers[A], APA_SUBS + 4, 0x8000)
    finish(headers[A])
    headers[OFFGRID_SUB] = part(
        OFFGRID_SUB, 0x8000, 0, 0, "MAIN", flags=1, main=A, number=1)
    add("offgrid_subpartition", headers, "reference-chase-discovers-sub")

    headers = {
        A: part(A, 0x40000, 0, B, "A"),
        B: part(B, 0x40000, A, 0, "B"),
    }
    add("missing_master", headers, "read-only-never-writeable")

    headers = healthy_headers()
    w32(headers[A], APA_LENGTH, 0x50000)
    finish(headers[A])
    add("overlapping_geometry", headers, "overlap-blocks-write")

    headers = healthy_headers()
    w32(headers[A], APA_NEXT, 0x90000)  # stale checksum on A
    w32(headers[B], APA_PREV, 0x50000)  # stale checksum on B
    add("two_header_link_damage", headers,
        "geometry-plan-two-corroborated-patches")

    headers = healthy_headers()
    w32(headers[A], APA_NEXT, C)
    finish(headers[A])
    add("conflicting_live_target", headers,
        "existing-target-conflict-blocks-geometry-write")

    with open(os.path.join(args.output, "manifest.json"), "w",
              encoding="utf-8") as manifest:
        json.dump({
            "sector_size": SECTOR_SIZE,
            "image_sectors": TOTAL_SECTORS,
            "logical_image_bytes": IMAGE_BYTES,
            "cases": cases,
        }, manifest, indent=2, sort_keys=True)

    print("generated %d sparse forensic HDD fixtures in %s" %
          (len(cases), args.output))


if __name__ == "__main__":
    main()
