# KELF structural validation

PS2 HDD Bootstrap Manager does not decrypt or identify KELF payloads. It performs a deliberately small structural check before a bootstrap is accepted for signing, installation, fingerprinting, or rescue metadata.

The portable implementation lives in `src/kelf.c` and is shared by the PS2 build and host-side tests. It mirrors the public `SecrKELFHeader_t` and `SecrBitBlockData_t` layout exposed by PS2SDK, but decodes the wire fields explicitly as little-endian bytes instead of casting the input buffer to a target-native structure.

## Fixed header fields used by the manager

The fixed KELF header is 32 bytes. Only fields required for structural validation are interpreted:

| Offset | Size | Field | Use |
|---|---:|---|---|
| `0x10` | 4 | `ELF_size` | Size of the encrypted/decrypted ELF data area described by the KELF container. |
| `0x14` | 2 | `KELF_header_size` | Size of the complete KELF header before the ELF data area. |
| `0x18` | 2 | `flags` | Selects optional header layout elements. |
| `0x1A` | 2 | `BIT_count` | Number of 16-byte BIT block descriptors. PS2SDK exposes a maximum of 63 entries. |

The 16-byte `UserHeader`, `unknown5`, and `mg_zones` fields are intentionally left opaque because the manager does not need their meaning to prove that the container is internally bounded.

## Validation order

`kelf_validate_layout()` preserves the Torii validation order and numeric results:

1. Require at least the 32-byte fixed header.
2. Reject a buffer beginning with the normal `0x7F 'E' 'L' 'F'` ELF signature. Renaming an ordinary ELF to `MBR.XIN` or `MBR.XLF` must never turn it into a KELF.
3. Require `KELF_header_size` to fit inside the supplied file and to be at least 32 bytes.
4. Reject more than 63 BIT entries.
5. Require `KELF_header_size + ELF_size` to equal the supplied unpadded file size and require a non-zero ELF area.
6. Require all `BIT_count` 16-byte descriptors to fit inside the KELF header.
7. When flag bit 0 is set, require the following length-prefixed variable section to begin inside the header.
8. For layouts whose high flag nibble is zero, account for the additional eight-byte header area used by the existing PS2SDK/secrman layout.
9. Require the remaining 32-byte key/check area to fit inside `KELF_header_size`.

This is a bounds/shape check, not a cryptographic authenticity check. Successful structural validation does not prove which bootstrap family the encrypted payload belongs to.

## Stable validation results

The names were introduced in Michishirube, but the values intentionally remain the same as the earlier `main.c` implementation so logs and error reports do not change meaning during modularization.

| Result | Value | Meaning |
|---|---:|---|
| `KELF_VALID` | `0` | Structurally acceptable complete KELF file. |
| `KELF_ERR_TOO_SMALL` | `-1` | Fixed header is truncated. |
| `KELF_ERR_PLAIN_ELF` | `-2` | Input is an ordinary ELF rather than a KELF. |
| `KELF_ERR_HEADER_SIZE` | `-3` | Header-size field is impossible for the supplied buffer. |
| `KELF_ERR_BIT_COUNT` | `-4` | BIT count exceeds the PS2SDK maximum. |
| `KELF_ERR_FILE_SIZE` | `-5` | Header and ELF size fields do not describe exactly the supplied file. |
| `KELF_ERR_BIT_TABLE` | `-6` | BIT descriptors extend beyond the KELF header. |
| `KELF_ERR_VARIABLE_SECTION` | `-7` | A requested length-prefixed section has no length byte inside the header. |
| `KELF_ERR_KEY_AREA` | `-8` | The remaining fixed key/check area does not fit in the header. |

## Sector-aligned HDD images

The bootstrap payload stored in the APA `__mbr` reserved area is read in complete sectors, so the in-memory HDD image can be larger than the original KELF file. `kelf_size_from_disk_image()` reads `KELF_header_size` and `ELF_size`, proves that their sum fits inside the sector image, validates only those real KELF bytes, and returns the unpadded length.

This distinction matters for SHA-256 diagnostics: the manager records both the complete sector image and the unpadded KELF fingerprint instead of silently treating sector padding as part of the file.

## What this module does not own

`kelf.c` has no PS2SDK, fileXio, HDD, MagicGate, memory-card, or UI dependency. It does not:

- decrypt KELFs;
- decide whether a payload is FHDB, PSBBN, HDD-OSD, or another family;
- call `SecrDownloadFile()`;
- sign a payload;
- read or write HDD sectors;
- update `osdStart` / `osdSize`;
- execute a KELF.

Those boundaries are intentional. MagicGate signing remains in the PS2-specific installation workflow, while boot-family classification remains evidence-based in the boot-chain modules.

## Regression fixtures

`tests/test_kelf.c` covers:

- normal low-layout and high-layout headers;
- the optional length-prefixed section;
- the maximum 63-entry BIT table;
- plain ELF rejection;
- truncated and impossible header/file sizes;
- BIT-table overflow;
- a missing variable-section length byte;
- insufficient key/check area;
- recovery of an unpadded KELF from a sector-padded image;
- malformed sector-image size fields and invalid embedded layouts.

Run the complete portable suite with:

```sh
make test-host
```
