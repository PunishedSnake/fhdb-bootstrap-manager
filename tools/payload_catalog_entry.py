#!/usr/bin/env python3
"""Print a src/boot_payload_catalog.c entry for a known HDD bootstrap file.

Usage:
    python3 tools/payload_catalog_entry.py OSDMBR.XLF "OSDMenu MBR" 1.3.0 \
        "PSBBN Definitive Project assets" [--presigned]

The manager identifies an installed __mbr payload by two digests, matching
what boot_payload_ps2.c records:

* sector image  - SHA-256 of ceil(size / 512) whole sectors, zero padded;
                  this is exactly what osdSize exposes on the HDD;
* declared KELF - SHA-256 of KELF_header_size + content_size bytes, the
                  "unpadded KELF" digest shown in BOOTCHAIN.TXT.

Only add entries for files you have byte-for-byte. A catalogue entry is an
exact identity claim, never a guess.
"""

import argparse
import hashlib
import struct
import sys

SECTOR = 512


def c_bytes(digest):
    data = bytes.fromhex(digest)
    rows = []
    for offset in range(0, 32, 8):
        rows.append(", ".join("0x%02x" % b for b in data[offset:offset + 8]))
    return "{\n         " + ",\n         ".join(rows) + "}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("payload")
    parser.add_argument("family")
    parser.add_argument("version")
    parser.add_argument("source")
    parser.add_argument("--presigned", action="store_true",
                        help="file is distributed already bound for __mbr")
    args = parser.parse_args()

    data = open(args.payload, "rb").read()
    if len(data) < 32 or data[:4] == b"\x7fELF":
        sys.exit("not a KELF file")
    content, header = struct.unpack_from("<IH", data, 0x10)
    declared = header + content
    if declared == 0 or declared > len(data):
        sys.exit("KELF size fields do not fit the file")
    padded = data + bytes((-len(data)) % SECTOR)
    sector = hashlib.sha256(padded).hexdigest()
    kelf = hashlib.sha256(data[:declared]).hexdigest()
    flags = "BOOT_PAYLOAD_CATALOG_PRESIGNED" if args.presigned else "0u"
    print("    /* %s %s: %u bytes, %u sectors, declared KELF %u bytes. */" %
          (args.family, args.version, len(data), len(padded) // SECTOR,
           declared))
    print("    {\"%s\", \"%s\", \"%s\", %s,\n     %s,\n     %s}," %
          (args.family, args.version, args.source, flags,
           c_bytes(sector), c_bytes(kelf)))


if __name__ == "__main__":
    main()
