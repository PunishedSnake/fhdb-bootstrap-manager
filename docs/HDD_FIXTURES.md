# Synthetic HDD fixture and repair suite

Michishirube exercises portable APA, KELF, pointer-bounds, mutation, health, and repair policy against complete synthetic raw-disk images without requiring a physical PlayStation 2 HDD. The images are generated deterministically by `tools/generate_hdd_fixtures.py` and are deliberately **not** committed to Git.

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

`tests/test_hdd_repair_fixtures.c` runs the production portable parser, `apa_repair` planner, `repair_health` mounted-disk recommendation policy, bounds policy, and KELF parser over every generated image. Device I/O is kept outside `repair_health`, so the same decision layer used by the PS2 `L2` health screen is exercised directly on the host.

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

For that reason a semantic mismatch is not sufficient for raw automatic repair. The source header must have a checksum mismatch and correction of exactly the one planner-approved canonical field must restore the **old stored checksum**. This provides an independent consistency check for a physical-style stale-checksum bit flip. It does not make the checksum cryptographically trustworthy; it deliberately narrows the set of states we are willing to write automatically.

Checksum-only mismatch, checksum-valid noncanonical master fields, multiple identity/anchor defects, GPT/protective layouts, random data, and torn ambiguous headers remain blocked.

## Normal mutation tests

`tests/test_hdd_mutations.c` models the successful byte-level postconditions used by the normal manager. It does not emulate DEV9, ATA, fileXio RPC, journaling, or cache durability.

### Disabling HDD bootstrap

The ROM-facing enable state is the APA MBR pair `osdStart` / `osdSize`, not the PC partition-table `BootIndicator`. The model of successful `HDIOC_SETOSDMBR(0, 0)` proves that:

1. both OSD pointer words become zero;
2. APA checksum is recomputed;
3. every unrelated byte stays unchanged;
4. the bootstrap program at sector `0x2000` stays unchanged;
5. a deliberately inserted PC `BootIndicator=0x80` remains `0x80`.

### Overwriting the bootstrap program

The normal manager's raw payload writer begins at reserved sector `0x2000`; it does not write the APA master. The mutation test writes a 700-byte synthetic KELF over old bytes and proves exact payload content, final-sector zero padding, no following-sector damage, valid KELF structure, and pointer-last activation. Disabling afterward clears the pointer while preserving the newly written program.

## Interrupted-state scope

The fixture suite models bytes that could be observed before, during, or after the transaction. It intentionally does **not** claim to model when a real ATA cache, fileXio RPC, DMA transfer, or APA journal makes a write durable.

The important logical distinction is still testable:

- complete or partial new payload with `osdStart=osdSize=0` remains unexposed to ROM;
- an active pointer to a zeroed/partial invalid payload is unsafe and should be cleared;
- a torn master checksum is not treated as equivalent to a successful `HDIOC_SETOSDMBR` update.

Physical-HDD validation remains required for hardware timing, cache/flush behavior, journaling, and real power-loss semantics.

## GPT note

The current `is_hybrid_gpt()` guard remains intentionally conservative and recognizes the PC `0x55AA` signature in the master area. The fixtures separately track the actual `EFI PART` signature at LBA 1 so a future real GPT parser can be introduced as an explicit behavior change rather than silently altering existing expectations.

## Adding a case

Extend the deterministic generator instead of committing a binary disk image. A useful case should isolate one state or corruption mode, record expected metadata in `manifest.json`, and have an explicit parser/repair assertion. New recovery algorithms should first gain raw-image counterexamples that demonstrate where the old evidence was insufficient.
