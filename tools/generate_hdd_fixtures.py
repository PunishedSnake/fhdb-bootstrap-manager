#!/usr/bin/env python3
"""Generate sparse host-only HDD images for APA/GPT regression tests."""

import argparse
import json
import os
import struct
import zlib

SECTOR_SIZE = 512
APA_HEADER_SIZE = 1024
APA_MAGIC_OFFSET = 0x004
APA_ID_OFFSET = 0x010
APA_START_OFFSET = 0x040
APA_LENGTH_OFFSET = 0x044
APA_TYPE_OFFSET = 0x048
APA_MBR_MAGIC_OFFSET = 0x100
APA_MBR_VERSION_OFFSET = 0x120
APA_OSD_START_OFFSET = 0x130
APA_OSD_SIZE_OFFSET = 0x134
PC_MBR_SIGNATURE_OFFSET = 0x1FE
PAYLOAD_LBA = 0x2000
IMAGE_SECTORS = 0x8000
IMAGE_BYTES = IMAGE_SECTORS * SECTOR_SIZE
DEFAULT_MBR_SECTORS = 0x4000


def write_le16(buffer, offset, value):
    struct.pack_into("<H", buffer, offset, value)


def write_le32(buffer, offset, value):
    struct.pack_into("<I", buffer, offset, value)


def apa_checksum(header):
    return sum(struct.unpack_from("<I", header, i * 4)[0]
               for i in range(1, 256)) & 0xFFFFFFFF


def make_gpt_header(current_lba, backup_lba, total_sectors):
    entries_crc = zlib.crc32(bytes(128 * 128)) & 0xFFFFFFFF
    header = bytearray(SECTOR_SIZE)
    header[:8] = b"EFI PART"
    struct.pack_into("<I", header, 8, 0x00010000)
    struct.pack_into("<I", header, 12, 92)
    struct.pack_into("<Q", header, 24, current_lba)
    struct.pack_into("<Q", header, 32, backup_lba)
    struct.pack_into("<Q", header, 40, 34)
    struct.pack_into("<Q", header, 48, total_sectors - 34)
    header[56:72] = bytes.fromhex("00112233445566778899aabbccddeeff")
    struct.pack_into("<Q", header, 72, 2)
    struct.pack_into("<I", header, 80, 128)
    struct.pack_into("<I", header, 84, 128)
    struct.pack_into("<I", header, 88, entries_crc)
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]) & 0xFFFFFFFF)
    return header


def make_apa_header(start=0, sectors=0, pc_signature=False, gpt_header=False):
    header = bytearray(APA_HEADER_SIZE)
    header[APA_MAGIC_OFFSET:APA_MAGIC_OFFSET + 4] = b"APA\0"
    header[APA_ID_OFFSET:APA_ID_OFFSET + 5] = b"__mbr"
    write_le32(header, APA_START_OFFSET, 0)
    write_le32(header, APA_LENGTH_OFFSET, DEFAULT_MBR_SECTORS)
    write_le16(header, APA_TYPE_OFFSET, 1)
    header[APA_MBR_MAGIC_OFFSET:APA_MBR_MAGIC_OFFSET + 32] = \
        b"Sony Computer Entertainment Inc."
    write_le32(header, APA_MBR_VERSION_OFFSET, 2)
    write_le32(header, APA_OSD_START_OFFSET, start)
    write_le32(header, APA_OSD_SIZE_OFFSET, sectors)
    if pc_signature:
        header[PC_MBR_SIGNATURE_OFFSET:PC_MBR_SIGNATURE_OFFSET + 2] = b"\x55\xaa"
    if gpt_header:
        gpt = make_gpt_header(1, IMAGE_SECTORS - 1, IMAGE_SECTORS)
        header[SECTOR_SIZE:SECTOR_SIZE + len(gpt)] = gpt
    write_le32(header, 0, apa_checksum(header))
    return header


def make_valid_kelf_sector():
    header_size = 72
    elf_size = 16
    payload = bytearray(SECTOR_SIZE)
    payload[:8] = b"KELFTEST"
    write_le32(payload, 0x10, elf_size)
    write_le16(payload, 0x14, header_size)
    write_le16(payload, 0x18, 0)
    write_le16(payload, 0x1A, 0)
    payload[header_size:header_size + elf_size] = b"PAYLOAD-TEST-1234"
    return payload


def make_partial_kelf_sector():
    old_sector = bytearray([0xA5] * SECTOR_SIZE)
    valid = make_valid_kelf_sector()
    old_sector[:20] = valid[:20]
    return old_sector


def make_protective_mbr():
    sector = bytearray(SECTOR_SIZE)
    entry = 446
    sector[entry + 4] = 0xEE
    struct.pack_into("<I", sector, entry + 8, 1)
    struct.pack_into("<I", sector, entry + 12, IMAGE_SECTORS - 1)
    sector[510:512] = b"\x55\xaa"
    return sector


def write_sparse_image(path, patches):
    with open(path, "wb") as image:
        image.truncate(IMAGE_BYTES)
        for offset, data in patches:
            image.seek(offset)
            image.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="directory for generated *.raw fixtures")
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)
    cases = []

    def add(name, header=None, patches=(), **metadata):
        image_patches = []
        if header is not None:
            image_patches.append((0, header))
        image_patches.extend(patches)
        write_sparse_image(os.path.join(args.output, name + ".raw"), image_patches)
        cases.append({"name": name, **metadata})

    add("valid_disabled", make_apa_header(), standard=True, hybrid=False,
        gpt=False, start=0, sectors=0, bounds=-170,
        mbr_size=DEFAULT_MBR_SECTORS)
    add("valid_enabled", make_apa_header(PAYLOAD_LBA, 1),
        [(PAYLOAD_LBA * SECTOR_SIZE, make_valid_kelf_sector())],
        standard=True, hybrid=False, gpt=False, start=PAYLOAD_LBA, sectors=1,
        bounds=0, kelf=0, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)
    add("garbage_payload", make_apa_header(PAYLOAD_LBA, 1),
        [(PAYLOAD_LBA * SECTOR_SIZE, b"garbage" + bytes(SECTOR_SIZE - 7))],
        standard=True, hybrid=False, gpt=False, start=PAYLOAD_LBA, sectors=1,
        bounds=0, kelf=-3, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)

    header = make_apa_header(PAYLOAD_LBA, 1)
    header[0x220] ^= 1
    add("bad_checksum", header, standard=False, hybrid=False, gpt=False,
        start=PAYLOAD_LBA, sectors=1, bounds=0, mbr_size=DEFAULT_MBR_SECTORS)

    for name, offset in (("bad_apa_magic", APA_MAGIC_OFFSET),
                         ("bad_mbr_id", APA_ID_OFFSET),
                         ("bad_sony_magic", APA_MBR_MAGIC_OFFSET)):
        header = make_apa_header()
        header[offset] ^= 1
        write_le32(header, 0, apa_checksum(header))
        add(name, header, standard=False, hybrid=False, gpt=False,
            start=0, sectors=0, bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    # A physical bit flip in a known canonical field invalidates the checksum.
    # The repair planner may fix it only when the proposed canonical value makes
    # the stored pre-flip checksum valid again.
    header = make_apa_header()
    header[APA_MAGIC_OFFSET] ^= 1
    add("bitflip_apa_magic", header, standard=False, hybrid=False, gpt=False,
        start=0, sectors=0, bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    # Checksummed-but-noncanonical master anchors are distinct from raw checksum
    # corruption: their correct values are fixed by the APA MBR format.
    header = make_apa_header()
    write_le32(header, APA_START_OFFSET, 0x1234)
    write_le32(header, 0, apa_checksum(header))
    add("bad_master_start", header, standard=True, hybrid=False, gpt=False,
        start=0, sectors=0, bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    header = make_apa_header()
    write_le16(header, APA_TYPE_OFFSET, 0x1337)
    write_le32(header, 0, apa_checksum(header))
    add("bad_master_type", header, standard=True, hybrid=False, gpt=False,
        start=0, sectors=0, bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    header = make_apa_header()
    write_le32(header, APA_MBR_VERSION_OFFSET, 99)
    write_le32(header, 0, apa_checksum(header))
    add("bad_mbr_version", header, standard=True, hybrid=False, gpt=False,
        start=0, sectors=0, bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    add("pointer_start_only", make_apa_header(PAYLOAD_LBA, 0),
        standard=True, hybrid=False, gpt=False, start=PAYLOAD_LBA, sectors=0,
        bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)
    add("pointer_size_only", make_apa_header(0, 1),
        standard=True, hybrid=False, gpt=False, start=0, sectors=1,
        bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)
    add("pointer_before_reserved", make_apa_header(0x1000, 1),
        standard=True, hybrid=False, gpt=False, start=0x1000, sectors=1,
        bounds=-172, mbr_size=DEFAULT_MBR_SECTORS)
    add("pointer_too_large", make_apa_header(PAYLOAD_LBA, 8193),
        standard=True, hybrid=False, gpt=False, start=PAYLOAD_LBA, sectors=8193,
        bounds=-171, mbr_size=0x6000)
    add("pointer_outside_mbr", make_apa_header(0x3FFF, 2),
        standard=True, hybrid=False, gpt=False, start=0x3FFF, sectors=2,
        bounds=-173, mbr_size=0x4000)

    add("apa_pc_signature_only", make_apa_header(pc_signature=True),
        standard=True, hybrid=True, gpt=False, start=0, sectors=0,
        bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)
    add("hybrid_apa_gpt",
        make_apa_header(PAYLOAD_LBA, 1, pc_signature=True, gpt_header=True),
        [(PAYLOAD_LBA * SECTOR_SIZE, make_valid_kelf_sector())],
        standard=True, hybrid=True, gpt=True, start=PAYLOAD_LBA, sectors=1,
        bounds=0, kelf=0, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)

    add("gpt_only", make_protective_mbr(),
        [(SECTOR_SIZE, make_gpt_header(1, IMAGE_SECTORS - 1, IMAGE_SECTORS))],
        standard=False, hybrid=True, gpt=True, start=0, sectors=0,
        bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    garbage = bytearray(APA_HEADER_SIZE)
    state = 0x12345678
    for index in range(APA_HEADER_SIZE):
        state = (1103515245 * state + 12345) & 0xFFFFFFFF
        garbage[index] = (state >> 16) & 0xFF
    add("deterministic_garbage", garbage, standard=False, hybrid=False,
        gpt=False)

    # Power-loss / interrupted-transaction states. These are disk states, not
    # simulated IOP failures: the portable test suite classifies the bytes that
    # would remain if execution stopped at the named point.
    add("interrupted_payload_written_pointer_zero", make_apa_header(),
        [(PAYLOAD_LBA * SECTOR_SIZE, make_valid_kelf_sector())],
        standard=True, hybrid=False, gpt=False, start=0, sectors=0,
        bounds=-170, kelf=0, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)
    add("interrupted_partial_payload_pointer_zero", make_apa_header(),
        [(PAYLOAD_LBA * SECTOR_SIZE, make_partial_kelf_sector())],
        standard=True, hybrid=False, gpt=False, start=0, sectors=0,
        bounds=-170, kelf=-2, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)
    add("enabled_zeroed_payload", make_apa_header(PAYLOAD_LBA, 1),
        standard=True, hybrid=False, gpt=False, start=PAYLOAD_LBA, sectors=1,
        bounds=0, kelf=-3, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)
    add("enabled_partial_overwrite", make_apa_header(PAYLOAD_LBA, 1),
        [(PAYLOAD_LBA * SECTOR_SIZE, make_partial_kelf_sector())],
        standard=True, hybrid=False, gpt=False, start=PAYLOAD_LBA, sectors=1,
        bounds=0, kelf=-2, payload_lba=PAYLOAD_LBA,
        mbr_size=DEFAULT_MBR_SECTORS)

    torn = make_apa_header(PAYLOAD_LBA, 1)
    write_le32(torn, APA_OSD_START_OFFSET, 0)
    write_le32(torn, APA_OSD_SIZE_OFFSET, 0)
    add("torn_disable_stale_checksum", torn,
        standard=False, hybrid=False, gpt=False, start=0, sectors=0,
        bounds=-170, mbr_size=DEFAULT_MBR_SECTORS)

    with open(os.path.join(args.output, "manifest.json"), "w", encoding="utf-8") as manifest:
        json.dump({"sector_size": SECTOR_SIZE,
                   "image_sectors": IMAGE_SECTORS,
                   "cases": cases}, manifest, indent=2, sort_keys=True)

    print("generated %d synthetic HDD fixtures in %s" % (len(cases), args.output))


if __name__ == "__main__":
    main()
