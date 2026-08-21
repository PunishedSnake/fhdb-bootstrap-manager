# Synthetic HDD fixture and repair suite

Michishirube exercises portable APA, KELF, pointer-bounds, mutation, health, deterministic repair, and forensic graph policy without requiring a physical PlayStation 2 HDD. Synthetic raw-disk fixtures are generated deterministically and are deliberately **not** committed to Git.

There are now two complementary raw-image laboratories:

- `tools/generate_hdd_fixtures.py` creates **30 sparse 16 MiB images** for APA master, bootstrap pointer, KELF, interrupted-state, mutation, and mounted repair-policy coverage under `tests/generated_hdds/`;
- `tools/generate_forensic_fixtures.py` creates **9 sparse 512 MiB images** for end-to-end forensic graph reconstruction on realistic 128 MiB APA grid LBAs under `tests/generated_forensic_hdds/`.

Both generators write a `manifest.json`. Executable C regression tables remain authoritative for expected behavior; manifests exist for inspection and future tooling. Sparse allocation keeps physical host storage small where supported even though the forensic images have a 512 MiB logical size.

## Current 30-image mounted/deterministic matrix

The original generated suite contains **30 raw HDD fixtures**.

| Fixture | Disk state | Mounted/deterministic repair policy |
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
| `bitflip_sony_magic.raw` | Physical-style bit flip in Sony MBR marker, old checksum retained | guarded raw header repair |
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
| `pointer_outside_mbr.raw` | Payload crosses supplied `__mbr` geometry | clear pointer |
| `apa_pc_signature_only.raw` | Valid APA plus PC `0x55AA`, no `EFI PART` | blocked by conservative hybrid guard |
| `hybrid_apa_gpt.raw` | Valid APA plus `0x55AA` and GPT header at LBA 1 | blocked |
| `gpt_only.raw` | Protective PC MBR and GPT, no APA | blocked |
| `deterministic_garbage.raw` | Reproducible pseudo-random first 1024 bytes | blocked |
| `interrupted_payload_written_pointer_zero.raw` | Valid new payload exists but pointer is disabled | no repair; safe interrupted state |
| `interrupted_partial_payload_pointer_zero.raw` | Partial/invalid latent payload while pointer remains disabled | no repair; safe interrupted state |
| `enabled_zeroed_payload.raw` | Active pointer references a zeroed payload | clear pointer |
| `enabled_partial_overwrite.raw` | Active pointer references a partially overwritten payload | clear pointer |
| `torn_disable_stale_checksum.raw` | Pointer bytes reached zero but old checksum remained | blocked; ambiguous raw-header damage |

The final mounted/deterministic repair-policy split is:

- **4 no-repair** states;
- **6 guarded raw header repairs**;
- **8 pointer-clear repairs** using normal disable semantics;
- **12 blocked** states.

## What the 30-image repair test proves

`tests/test_hdd_repair_fixtures.c` runs production portable parsing, `apa_repair`, `repair_health`, bounds policy, and KELF parsing over every generated image. Device I/O remains outside `repair_health`, so the same decision layer used by **Recovery -> Deterministic structure health / repair** is exercised directly on the host.

For each of the six raw-header repair cases, the test builds the repaired 1024-byte header and requires:

1. standard internally consistent APA;
2. master `start == 0`;
3. master type MBR;
4. canonical MBR version;
5. no hybrid/GPT guard;
6. blocked plans cannot be passed through `apa_repair_build_header()` successfully.

For all eight pointer-clear cases, the test performs the host byte-level equivalent of a successful `HDIOC_SETOSDMBR(0,0)` postcondition:

1. `osdStart == 0`;
2. `osdSize == 0`;
3. APA checksum rebuilt;
4. resulting master valid;
5. every byte outside checksum and the two OSD pointer words unchanged.

This complements `tests/test_hdd_mutations.c`, which separately verifies the original manager's successful disk effects: pointer disable/activation and reserved-area MBR payload replacement.

## Portable forensic graph suite

Broader forensic reconstruction has a regression suite in `tests/test_apa_forensic.c`. It deliberately uses an abstract raw-reader callback instead of PS2SDK so graph policy can be tested independently of hardware transport.

Current graph cases include:

### Healthy linked graph

A valid master and linked partitions must produce a complete forward map with maximum confidence and no repair plan.

### Physical-style stale-checksum broken link

A partition link is changed while retaining its old checksum. Surviving neighboring graph evidence determines the correct target. The repair plan must identify exactly one patch, checksum-corroborate it, and classify it automatic-safe.

### Checksum-valid wrong link

The same semantic link error is followed by recomputing the checksum. The graph may still strongly prefer one topology, but checksum no longer independently corroborates the change. The plan therefore remains an explicit manual/expert path rather than automatic-safe.

### Off-grid referenced subpartition

A subpartition header exists away from the coarse 128 MiB scan grid. The scanner must discover it by following a surviving main-partition `subs[]` reference. This proves the coarse scan is only a discovery seed, not the complete search space.

### Missing-master write gate

Credible non-master headers can still form useful read-only evidence, but no candidate lacking a reliable LBA-0 master may become writeable. This preserves the distinction between a **shadow APA** and a disk safe to repair.

### Exact two-bit link corruption

A link value has **exactly two flipped bits** relative to the graph-derived expected neighbor while retaining the old checksum. Regression requires:

1. the correct neighbor is reconstructed from independent graph evidence;
2. `bit_distance == 2`;
3. correcting the graph-derived value restores the old stored checksum;
4. the patch is checksum-corroborated;
5. the final plan satisfies the automatic-safe gate.

The implementation does not brute-force arbitrary two-bit combinations. The graph derives an exact expected value first; bit distance and checksum are evidence about that candidate.

## 9-image sparse forensic raw-HDD E2E suite

`tests/test_forensic_fixtures.c` runs the **same production `apa_forensic_scan()`** against actual file-backed sparse raw HDD images rather than mock header slots. Each image is logically 512 MiB, putting primary partitions at realistic APA grid LBAs `0x40000`, `0x80000`, and `0xC0000` while keeping the files sparse on CI storage.

The nine cases are:

| Fixture | Forensic contract |
|---|---|
| `healthy_chain.raw` | complete forward graph, 100% confidence, no repair |
| `broken_next_stale_checksum.raw` | one graph-derived checksum-corroborated link repair |
| `two_bit_next_stale_checksum.raw` | exact two-bit link delta, checksum corroboration, automatic-safe |
| `checksummed_wrong_next.raw` | high-confidence topology but manual/expert only |
| `offgrid_subpartition.raw` | `subs[]` reference chase discovers LBA `0x48000` outside the coarse grid |
| `missing_master.raw` | useful read-only nodes, no writeable candidate |
| `overlapping_geometry.raw` | overlap must be exposed and every affected retained map remains non-writeable |
| `two_header_link_damage.raw` | geometry reconstruction produces two checksum-corroborated patches |
| `conflicting_live_target.raw` | live-link conflict must be exposed and every affected retained map remains non-writeable |

Candidate maps with identical node order are intentionally deduplicated by production code. Tests therefore verify **safety properties of the retained hypothesis** (overlap/conflict detected and write blocked) rather than requiring a redundant map-kind label to survive deduplication.

This raw-image suite closes an important gap between small portable mock graphs and physical hardware: it proves real seek offsets, sparse blank-space rejection, 128 MiB grid discovery, direct off-grid reference chasing, map deduplication behavior, repair-plan classification, and multi-header corruption on complete raw-disk files. It still cannot model DEV9/ATA/fileXio/DMA/cache/journal timing.

## Forensic write postconditions

The portable graph and sparse raw-image suites validate scan/plan construction and exact patched-header semantics. The PS2-only writer adds runtime gates that host raw-image tests cannot emulate:

- every current header must still match the bytes observed during the scan immediately before write;
- non-master headers are written before LBA 0;
- every write is flushed and read back exactly;
- the LBA-0 candidate must remain a complete standard non-hybrid APA master;
- every touched header is re-read after the whole transaction;
- restart is mandatory after success or partial failure.

Before those writes, `forensic_snapshot` creates `HDDMETA.BIN` / `HDDMETA2.BIN` containing every exact pre-repair header plus SHA-256 evidence. The APAMETA1 header also records how many planned topology patches change exactly one or two bits.

## Why checksum-valid corruption is not automatically repaired

APA protects a 1024-byte header with a simple additive 32-bit checksum: the sum of little-endian words 1 through 255. It is useful for many accidental changes but is **not** collision-resistant.

Two independent corruptions can cancel mathematically. A regression case changes one known byte by `-1` and an unrelated byte by `+1`; the stored checksum remains valid even though two bytes are wrong.

For deterministic single-master repair, the source must have a checksum mismatch and correction of exactly the one planner-approved canonical field must restore the **old stored checksum**.

For forensic topology repair, the expected `prev`/`next` value comes from the graph. A stale checksum can then independently corroborate that exact graph-derived correction. A checksummed semantic error may remain a high-confidence hypothesis, but it is not promoted to automatic-safe merely by recalculating its checksum.

## Normal mutation tests

`tests/test_hdd_mutations.c` models successful byte-level postconditions used by normal manager writes. It does not emulate DEV9, ATA, fileXio RPC, journaling, or cache durability.

### Disabling HDD bootstrap

The ROM-facing enable state is APA MBR `osdStart` / `osdSize`, not the PC partition-table `BootIndicator`.

The model of successful `HDIOC_SETOSDMBR(0,0)` proves:

1. both OSD pointer words become zero;
2. APA checksum is recomputed;
3. every unrelated byte remains unchanged;
4. the bootstrap program at sector `0x2000` remains unchanged;
5. a deliberately inserted PC `BootIndicator=0x80` remains `0x80`.

### Overwriting the bootstrap program

The normal raw payload writer begins at reserved sector `0x2000`; it does not write the APA master. The mutation test writes a 700-byte synthetic KELF over old bytes and proves exact payload content, final-sector zero padding, no following-sector damage, valid KELF structure, and pointer-last activation. Disabling afterward clears the pointer while preserving the newly written program.

## Interrupted-state scope

The suite models bytes that could be observed before, during, or after a transaction. It intentionally does **not** claim to model when real ATA cache, fileXio RPC, DMA transfer, or APA journal state becomes durable.

The important logical distinctions remain testable:

- complete or partial new payload with `osdStart=osdSize=0` remains unexposed to ROM;
- active pointer to a zeroed/partial invalid payload is unsafe and should be cleared;
- torn master checksum is not equivalent to a successful `HDIOC_SETOSDMBR` update;
- a read-only forensic candidate may be useful even when it is not safe to write;
- multi-header write ordering and power-loss durability still require physical testing.

## GPT note

The current `is_hybrid_gpt()` guard remains intentionally conservative and recognizes the PC `0x55AA` signature in the master area. Fixtures separately track actual `EFI PART` at LBA 1 so a future real GPT parser can be introduced as an explicit behavior change rather than silently changing existing expectations.

## Adding a case

Extend deterministic generators or portable mock disks instead of committing opaque binary images.

A useful new recovery case should:

1. isolate one state/corruption mode;
2. state which evidence survives;
3. state whether the expected outcome is read-only reconstruction, deterministic repair, expert-only repair, pointer clear, or block;
4. verify final postconditions, not only the initial classification.

Every physical-HDD bug or surprising adapter behavior found during hardware validation should become a deterministic host regression before its fix is considered complete.
