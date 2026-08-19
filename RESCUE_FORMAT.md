# HDD Rescue Capsule Format

`HDDRESCUE.BIN` and `HDDRESCUE2.BIN` use a small versioned container so a complete PS2 HDD bootstrap can be checked independently of the console program that created it.

All integers are unsigned 32-bit little-endian values. All offsets are from the beginning of the file.

## File layout

| Offset | Size | Contents |
|---:|---:|---|
| `0x0000` | `0x0100` | Versioned capsule metadata |
| `0x0100` | `0x0400` | Exact 1024-byte APA `__mbr` master header |
| `0x0500` | variable | Exact sector-aligned active payload image, if present |

The complete file is therefore `1280 + payload_bytes` bytes. A header-only capsule is exactly 1280 bytes.

## Version 1 metadata

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x000` | 8 | magic | `PS2HBRC` followed by NUL |
| `0x008` | 4 | version | `1` |
| `0x00c` | 4 | metadata size | `256` |
| `0x010` | 4 | complete size | Exact complete file size |
| `0x014` | 4 | flags | Bit field described below |
| `0x018` | 4 | payload start | Saved `osdStart` sector |
| `0x01c` | 4 | payload sectors | Saved `osdSize` sector count |
| `0x020` | 4 | payload bytes | `payload_sectors * 512` |
| `0x024` | 4 | APA header bytes | `1024` |
| `0x028` | 32 | APA SHA-256 | Digest of the exact bytes at `0x0100..0x04ff` |
| `0x048` | 32 | payload SHA-256 | Digest of the complete sector image at `0x0500`, including sector padding |
| `0x068` | 16 | ROMVER | NUL-terminated console ROMVER where available |
| `0x078` | 32 | probable family | NUL-terminated diagnostic classification |
| `0x098` | 16 | confidence | NUL-terminated confidence label |
| `0x0a8` | 4 | KELF file bytes | Unpadded structural KELF length, or zero |
| `0x0ac` | 84 | reserved | Zero in version 1 |

## Flags

| Bit | Name | Meaning |
|---:|---|---|
| `0x00000001` | `VALID_APA` | The embedded header passed APA validation when saved |
| `0x00000002` | `HAS_PAYLOAD` | A sector-aligned payload follows the header |
| `0x00000004` | `VALID_KELF` | The payload begins with a structurally valid KELF whose unpadded length is recorded |

Unknown flag bits are rejected. `VALID_KELF` is invalid without `HAS_PAYLOAD` and a non-zero KELF length.

When the bootstrap pointer is disabled, `payload_start`, `payload_sectors`, `payload_bytes`, and `KELF file bytes` are zero; `HAS_PAYLOAD` and `VALID_KELF` are clear. The payload SHA-256 field remains zero-filled.

## Validation and restoration rules

Before a capsule can be used, the manager verifies:

1. magic, version, fixed sizes, flags, and all size relationships;
2. the exact complete file length;
3. the embedded APA header structure and SHA-256;
4. the exact sector-image SHA-256 when a payload is present;
5. the recorded KELF length when `VALID_KELF` is set;
6. that the saved header belongs to the connected disk, ignoring only the APA checksum and mutable `osdStart`/`osdSize` fields;
7. that the saved payload range fits the current `__mbr` partition.

Full restoration writes the payload image first, flushes the HDD, compares every written sector, and calls `HDIOC_SETOSDMBR` only after the comparison succeeds. A final header read verifies the restored pointer and APA checksum.

SHA-256 provides corruption detection, not authorship or authenticity. Keep capsules from untrusted systems away from write-capable restoration workflows, which is generally sound advice for any file whose purpose is to become an encrypted boot program on a raw disk.

## Compatibility policy

Readers must reject unsupported versions instead of guessing. Future format revisions may use the reserved metadata area or define a new version while retaining the same magic. Version 1 writers leave every reserved byte at zero.

The serialization and validation implementation is in `capsule_format.c`; portable tests are in `tests/test_formats.c`.
