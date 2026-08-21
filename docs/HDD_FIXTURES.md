# Synthetic HDD fixture suite

Michishirube can exercise the portable APA/KELF/bounds policy against complete raw-disk images without requiring a physical PlayStation 2 HDD. The images are generated deterministically by `tools/generate_hdd_fixtures.py` and are deliberately **not** stored in Git.

`make test-host` regenerates the suite under `tests/generated_hdds/`, builds `tests/test_hdd_fixtures` and `tests/test_hdd_mutations`, and verifies both static disk states and byte-level mutation postconditions. Each generated image has a logical size of 16 MiB so sector `0x2000` exists, but it is created as a sparse file; the suite therefore consumes only a small amount of physical host storage on filesystems that support sparse files.

The generator also writes `manifest.json` with the logical image geometry and expected properties. The C regression tables remain authoritative for executable expectations; the manifest is intended for inspection and future tooling.

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
| `interrupted_payload_written_pointer_zero.raw` | New valid MBR program has reached `0x2000`, but activation has not happened yet | APA remains disabled and valid; latent payload is structurally valid |
| `interrupted_partial_payload_pointer_zero.raw` | Power loss during payload replacement while the pointer is still zero | APA remains disabled and valid; latent payload is structurally invalid |
| `enabled_zeroed_payload.raw` | Pointer is active but the referenced payload sector is zeroed | valid APA/bounds; invalid active KELF |
| `enabled_partial_overwrite.raw` | Pointer remained active while the referenced MBR program was only partly overwritten | valid APA/bounds; invalid active KELF; intentionally demonstrates the dangerous ordering avoided by the manager |
| `torn_disable_stale_checksum.raw` | `osdStart/osdSize` reached zero but the old APA checksum remained | APA rejected; models an incomplete header update rather than a successful `HDIOC_SETOSDMBR` postcondition |

The interrupted states are intentionally classified as bytes-on-disk states. They do **not** emulate when a real ATA cache, IOP RPC, or APA journal makes individual writes durable.

## Mutation tests: what the original tool actually changes

`tests/test_hdd_mutations.c` contains a small host-only byte model of the two successful disk effects used by the manager. It is not linked into the PS2 ELF.

### Disabling HDD bootstrap

The PS2 HDD bootstrap does not use the classic PC partition-table `BootIndicator` byte as its enable flag. The ROM-facing state is the APA MBR pair:

- `osdStart`;
- `osdSize`.

The real PS2 path calls `HDIOC_SETOSDMBR(0, 0)`. The host model verifies the corresponding successful byte-level postcondition:

1. `osdStart` becomes zero;
2. `osdSize` becomes zero;
3. the APA checksum is recomputed and the header remains standard/valid;
4. every other header byte is unchanged;
5. the MBR payload at sector `0x2000` is unchanged.

The test deliberately inserts a synthetic PC-style `BootIndicator = 0x80` byte before disabling and verifies that it remains `0x80`. This prevents future code or documentation from conflating the unrelated PC boot flag with the PS2 HDD OSD pointer.

### Overwriting the MBR program

The manager's raw payload write is confined to the reserved `__mbr` area beginning at sector `0x2000`; it does not raw-write sector zero. The mutation test starts with a disabled APA header, fills the target sectors with old bytes, then writes a new 700-byte structurally valid KELF using the same two-sector/zero-padding behavior as `hdd_write.c`.

The assertions prove that:

1. sector zero and the APA pointer are byte-for-byte unchanged while the payload is being replaced;
2. the new payload bytes are exact;
3. the unwritten tail of the final touched sector is zero-filled, matching `hdd_write_payload_verified()`;
4. the following untouched sector keeps its sentinel bytes;
5. the new sector image parses as a valid KELF;
6. only after the payload is complete does the model set `osdStart=0x2000` and `osdSize=2` and recompute the checksum;
7. disabling again clears only those pointer fields and still leaves the newly written MBR program intact.

Together with `tests/test_bootstrap_transaction.c`, this covers both the abstract call ordering and the expected resulting bytes on a synthetic disk.

## Rescue metadata interruption/staleness

The portable rescue tests also reject an obsolete capsule version and verify that a protected rescue slot with a different APA SHA-256 or payload SHA-256 is not treated as the current state. These tests complement the raw-HDD cases without inventing a fake fileXio layer.

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

These host fixtures improve parser, state-transition, and byte-level write-contract coverage, but they do not model DEV9/ATA timing, DMA, fileXio RPC behavior, cache durability, APA journaling, or the exact effects of a physical power loss. Physical-HDD validation remains the final gate for hardware-dependent write semantics.
