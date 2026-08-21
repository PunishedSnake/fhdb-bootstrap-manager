# Synthetic HDD fixture and repair suite

Michishirube exercises portable APA, KELF, pointer-bounds, mutation, and repair policy against complete synthetic raw-disk images without requiring a physical PlayStation 2 HDD. The images are generated deterministically by `tools/generate_hdd_fixtures.py` and are deliberately **not** committed to Git.

`make test-host` regenerates the suite under `tests/generated_hdds/`. Every image has a logical size of 16 MiB so sector `0x2000` exists, but the files are sparse and consume only a small amount of physical host storage on filesystems that support sparse allocation.

The generator also writes `manifest.json`. The C regression tables are authoritative for executable expectations; the manifest is for inspection and future tooling.

## Current matrix

The suite currently contains **30 raw HDD fixtures**.

| Fixture | Disk state | Repair policy |
|---|---|---|
| `valid_disabled.raw` | Valid APA `__mbr`, zero OSD pointer | no repair |
| `valid_enabled.raw` | Valid pointer to a structurally valid synthetic KELF at `0x2000` | no repair |
| `garbage_payload.raw` | Valid pointer to deterministic non-KELF bytes | clear pointer through the normal disable workflow |
| `bad_checksum.raw` | Otherwise recognizable master header with unexplained checksum mismatch | blocked |
| `bad_apa_magic.raw` | Noncanonical `APA\0` marker with checksum recomputed afterward | blocked |
| `bad_mbr_id.raw` | Noncanonical `__mbr` identifier with checksum recomputed afterward | blocked |
| `bad_sony_magic.raw` | Noncanonical Sony MBR marker with checksum recomputed afterward | blocked |
| `bitflip_apa_magic.raw` | Physical-style bit flip in `APA\0`, old checksum retained | guarded raw header repair |
| `bitflip_mbr_id.raw` | Physical-style bit flip in `__mbr`, old checksum retained | guarded raw header repair |
| `bitflip_sony_magic.raw` | Physical-style bit flip in the Sony MBR marker, old checksum retained | guarded raw header repair |
| `bad_master_start.raw` | Nonzero master `start`, checksum recomputed afterward | blocked |
| `bad_master_type.raw` | Non-MBR master type, checksum recomputed afterward | blocked |
| `bad_mbr_version.raw` | Noncanonical MBR version, checksum recomputed afterward | blocked |
| `bitflip_master_start.raw` | Master `start` corrupted while old checksum is retained | guarded raw header repair |
| `bitflip_master_type.raw` | Master type corrupted while old checksum is retained | guarded raw header repair |
| `bitflip_mbr_version.raw` | MBR version corrupted while old checksum is retained | guarded raw header repair |
| `pointer_start_only.raw` | `osdStart != 0`, `osdSize == 0` | clear pointer |
| `pointer_size_only.raw` | `osdStart == 0`, `osdSize != 0` | clear pointer |
| `pointer_before_reserved.raw` | Pointer begins before reserved sector `0x2000` | clear pointer |
| `pointer_too_large.raw` | Payload exceeds the 4 MiB policy limit | clear pointer |
| `pointer_outside_mbr.raw` | Payload crosses the supplied `__mbr` geometry | clear pointer |
| `apa_pc_signature_only.raw` | Valid APA plus PC `0x55AA`, no `EFI PART` | blocked by conservative hybrid guard |
| `hybrid_apa_gpt.raw` | Valid APA plus `0x55AA` and GPT header at LBA 1 | blocked |
| `gpt_only.raw` | Protective PC MBR and GPT, no APA | blocked |
| `deterministic_garbage.raw` | Reproducible pseudo-random first 1024 bytes | blocked |
| `interrupted_payload_written_pointer_zero.raw` | Valid new payload exists but pointer is still disabled | no repair; safe interrupted state |
| `interrupted_partial_payload_pointer_zero.raw` | Partial/invalid latent payload while pointer remains disabled | no repair; safe interrupted state |
| `enabled_zeroed_payload.raw` | Active pointer references a zeroed payload | clear pointer |
| `enabled_partial_overwrite.raw` | Active pointer references a partially overwritten payload | clear pointer |
| `torn_disable_stale_checksum.raw` | Pointer bytes reached zero but old checksum remained | blocked; ambiguous raw-header damage |

The final repair-policy regression currently divides the 30 cases into:

- **4 no-repair** states;
- **6 guarded raw header repairs**;
- **8 pointer-clear repairs** using the normal disable semantics;
- **12 blocked** states where the available evidence is not strong enough for an automatic write.

## What the repair test actually proves

`tests/test_hdd_repair_fixtures.c` runs the production portable parser, repair planner, bounds policy, and KELF parser over every generated image.

For each of the six raw-header repair cases it does not merely check that a repair was suggested. It builds the repaired 1024-byte header and requires all of the following postconditions:

1. the result is a standard internally consistent APA header;
2. master `start` is sector 0;
3. master type is MBR;
4. MBR version is canonical;
5. no hybrid/GPT guard is present;
6. a blocked plan cannot be passed to `apa_repair_build_header()` successfully.

For all eight pointer-clear cases the test performs the host byte-level equivalent of a successful `HDIOC_SETOSDMBR(0, 0)` result:

1. `osdStart` becomes zero;
2. `osdSize` becomes zero;
3. the APA checksum is rebuilt;
4. the resulting master header remains valid;
5. every byte outside the checksum word and the two pointer words is unchanged.

This complements `tests/test_hdd_mutations.c`, which separately verifies the original manager's two successful disk effects: pointer disable/activation and reserved-area MBR payload replacement.

## Why checksum-valid corruption is not automatically repaired

APA protects the 1024-byte header with a simple additive 32-bit checksum: the sum of little-endian words 1 through 255. It is useful for detecting many accidental changes, but it is **not** a collision-resistant integrity primitive.

Two independent corruptions can cancel mathematically. A deliberately tested example changes one known byte by `-1` and an unrelated byte by `+1`; the stored checksum remains valid even though two bytes are wrong.

For that reason Michishirube does **not** interpret either of these states as sufficient evidence for raw repair:

- a checksum-only mismatch with no reconstructable bad field;
- a semantically noncanonical field whose checksum has already been made valid.

A raw automatic master-header repair is accepted only when all of these are true:

1. exactly one canonical identity field or master anchor is damaged under the planner's evidence rules;
2. independent identity/master evidence still identifies sector zero as the APA master;
3. the source checksum currently fails;
4. replacing only that one known field makes the **old stored checksum** correct again;
5. no GPT/hybrid blocker is present.

This does not make the additive checksum cryptographically strong. It makes the proposed one-field repair explain the observed corruption instead of blindly legalizing unknown bytes by recomputing the checksum.

The raw writer then has additional PS2-side gates: an exact 1024-byte `HDDRAW*.BIN` snapshot must be saved and verified first, the proposed header must pass the full repair contract before writing, sectors 0-1 are written as one deliberately narrow operation, the HDD is flushed, and the bytes are read back and compared exactly. A restart is required afterward so `ps2hdd` re-reads the repaired disk.

## Mutation tests: what the original tool changes

### Disabling HDD bootstrap

The PS2 HDD bootstrap does not use the classic PC partition-table `BootIndicator` as its enable flag. The ROM-facing state is the APA master pair `osdStart` / `osdSize`.

The real PS2 path uses `HDIOC_SETOSDMBR(0, 0)`. The host mutation model verifies that a successful disable:

- clears only `osdStart` and `osdSize` plus the resulting checksum change;
- leaves the MBR payload at sector `0x2000` intact;
- leaves a synthetic PC `BootIndicator = 0x80` untouched.

This prevents future code from confusing the unrelated PC boot flag with the PS2 OSD bootstrap pointer.

### Overwriting the MBR program

Normal bootstrap installation writes the payload only into the reserved `__mbr` area beginning at sector `0x2000`. The mutation test fills target sectors with old bytes and writes a new 700-byte synthetic KELF with the same sector-padding behavior as `hdd_write.c`.

It verifies that:

- sector zero and the OSD pointer remain unchanged while payload bytes are replaced;
- payload bytes are exact and the final touched sector is zero-padded;
- the following sector is untouched;
- the resulting payload parses as a valid KELF;
- the pointer is exposed only after the payload is complete;
- disabling afterward clears the pointer without deleting that payload.

Together with `tests/test_bootstrap_transaction.c`, this covers call ordering and resulting bytes.

## Interrupted transactions

The interrupted fixtures model observable bytes-on-disk, not IOP/ATA timing. The important safety distinction is whether the pointer is visible:

- payload written or partly written while pointer is `0/0` is safe from ROM execution and needs no automatic repair;
- invalid payload while the pointer is active is unsafe and is routed to the normal backup + pointer-clear workflow;
- an ambiguous torn master-header write is blocked rather than normalized speculatively.

## Why `hdd_bounds` is portable

The PS2-specific `hdd_validate_payload_bounds()` still queries live `fileXioGetStat("hdd0:__mbr")`, but deterministic pointer/geometry policy lives in `hdd_bounds.c`. The host suite therefore exercises empty, too-large, before-reserved, and outside-`__mbr` cases without a fake fileXio layer.

The live PS2 path preserves Torii's error precedence: pointer shape first, live partition geometry second, portable geometry check last.

## GPT note

The current `is_hybrid_gpt()` guard intentionally remains conservative and checks the conventional PC MBR `0x55AA` signature. The fixture suite records the actual GPT `EFI PART` signature at LBA 1 independently so a future stricter GPT parser can change behavior behind explicit regression expectations rather than silently changing the safety contract.

`apa_pc_signature_only.raw` protects the conservative current behavior, while `hybrid_apa_gpt.raw` and `gpt_only.raw` contain actual GPT-shaped bytes.

## What these tests do not prove

The host suite cannot model DEV9/ATA timing, DMA, fileXio RPC behavior, cache durability, APA journaling, controller/power interruptions, or the exact point at which a physical disk makes a write durable.

It verifies parser policy, repair admissibility, generated repair bytes, pointer-clear postconditions, transaction ordering, and mutation boundaries. **Physical-HDD validation remains the final gate** for hardware-dependent write semantics.
