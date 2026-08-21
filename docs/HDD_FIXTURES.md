# Synthetic HDD fixture suite

Michishirube can exercise the portable APA/KELF/bounds policy against complete raw-disk images without requiring a physical PlayStation 2 HDD. The images are generated deterministically by `tools/generate_hdd_fixtures.py` and are deliberately **not** stored in Git.

`make test-host` regenerates the suite under `tests/generated_hdds/`, builds `tests/test_hdd_fixtures`, and verifies the expected result for every case. Each image has a logical size of 16 MiB so sector `0x2000` exists, but it is created as a sparse file; the generated suite therefore consumes only a small amount of physical host storage on filesystems that support sparse files.

The generator also writes `manifest.json` with the logical image geometry and expected properties. The C regression table remains authoritative for executable expectations; the manifest is intended for inspection and future tooling.

## Current cases

| Fixture | Purpose | Expected portable result |
|---|---|---|
| `valid_disabled.raw` | Valid APA `__mbr`, zero pointer | valid APA; empty-pointer bounds result |
| `valid_enabled.raw` | Valid APA pointer to sector `0x2000` with a structurally valid synthetic KELF | valid APA, valid bounds, valid KELF image |
| `garbage_payload.raw` | Valid pointer whose referenced sector is deterministic non-KELF data | valid APA/bounds; invalid KELF image |
| `bad_checksum.raw` | Valid-looking APA with one byte changed after checksum calculation | APA rejected |
| `bad_apa_magic.raw` | Correct checksum but corrupted `APA\0` magic | APA rejected |
| `bad_mbr_id.raw` | Correct checksum but corrupted `__mbr` identifier | APA rejected |
| `bad_sony_magic.raw` | Correct checksum but corrupted Sony MBR signature | APA rejected |
| `pointer_start_only.raw` | `osdStart != 0`, `osdSize == 0` | empty/inconsistent pointer |
| `pointer_size_only.raw` | `osdStart == 0`, `osdSize != 0` | empty/inconsistent pointer |
| `pointer_before_reserved.raw` | Pointer starts before sector `0x2000` | `HDD_PAYLOAD_ERR_BEFORE_RESERVED_AREA` |
| `pointer_too_large.raw` | Payload exceeds the 4 MiB policy limit | `HDD_PAYLOAD_ERR_TOO_LARGE` |
| `pointer_outside_mbr.raw` | Payload crosses the supplied `__mbr` geometry | `HDD_PAYLOAD_ERR_OUTSIDE_MBR` |
| `apa_pc_signature_only.raw` | Valid APA plus `0x55AA`, but no GPT header | conservatively classified by current hybrid guard |
| `hybrid_apa_gpt.raw` | Valid checksummed APA, protective `0x55AA`, and `EFI PART` at LBA 1 | valid APA plus hybrid/GPT rejection signal |
| `gpt_only.raw` | Protective PC MBR and primary GPT header, no APA | APA rejected; GPT/protective-MBR markers present |
| `deterministic_garbage.raw` | Reproducible pseudo-random first 1024 bytes | APA rejected |

## Why `hdd_bounds` is portable

The old `hdd_validate_payload_bounds()` combined two responsibilities: pure pointer/geometry policy and the PS2-specific `fileXioGetStat("hdd0:__mbr")` query. Michishirube now keeps the live query in `hdd_read.c` and moves the deterministic policy to `hdd_bounds.c`.

The PS2 path preserves Torii's error precedence: pointer shape is validated first, then the live partition geometry is queried, then the same portable geometry function used by the host fixtures completes the bounds check. This lets CI test `empty`, `too large`, `before reserved`, and `outside __mbr` without inventing a fake fileXio implementation.

## GPT note

The current `is_hybrid_gpt()` guard is intentionally conservative and checks the conventional PC MBR `0x55AA` signature in the APA header. It does not parse the GPT header itself. The fixture suite therefore tracks two signals independently:

- the current conservative `0x55AA` guard;
- the actual `EFI PART` signature at LBA 1.

`apa_pc_signature_only.raw` protects the existing conservative behavior, while `hybrid_apa_gpt.raw` and `gpt_only.raw` ensure real GPT-shaped data is also represented. A future stricter GPT parser can be added behind new regression expectations rather than silently changing this behavior.

## Adding a case

Prefer extending the generator rather than committing a binary image. A useful fixture should identify exactly one boundary or corruption mode, use deterministic bytes, record its expected metadata in the generated manifest, and have an explicit assertion in `tests/test_hdd_fixtures.c`.

These host fixtures improve parser and policy coverage, but they do not model DEV9/ATA timing, DMA, fileXio RPC behavior, cache flushes, or power-loss behavior on a real device. Physical-HDD validation remains the final gate for hardware-dependent write semantics.
