# Forensic APA recovery

This document defines the `0.4.x` **Michishirube** forensic recovery contract. It covers raw read-only reconstruction, candidate-map scoring, shadow-map browsing, topology repair planning, `HDDMETA` snapshots, write ordering, and hardware-validation requirements.

It does **not** define a filesystem repair tool. Recovering APA topology is not the same as recreating lost PFS/HDL/file data.

## Goals

Forensic recovery exists for disks that are still block-readable but whose APA metadata is damaged enough that normal `ps2hdd` admission is unreliable or impossible.

The system should:

- extract as much structure as possible without writing;
- make uncertainty visible instead of silently picking one answer;
- permit read-only use of coherent reconstructed geometry before repair;
- generate exact field-level repair plans;
- preserve every original metadata block before a write;
- keep normal Torii-compatible write semantics isolated from forensic recovery;
- fail closed when evidence is insufficient for a bounded topology repair.

## Non-goals

The current forensic engine does not:

- format the disk;
- create arbitrary new partitions;
- guess missing payload/file contents;
- reconstruct PFS trees or HDL game data from nothing;
- invent unknown IDs, passwords, timestamps, lengths, or filesystem metadata;
- silently feed speculative geometry into normal writable `ps2hdd`/PFS paths;
- declare a candidate map healthy merely because its checksum is valid.

## Trust levels

The implementation treats recovery as a ladder rather than one boolean `valid` flag.

### Level 1 — raw readable

Raw sectors can be read. No APA claim is made.

### Level 2 — forensic candidate

Enough candidate headers survive to build one or more plausible APA graphs.

### Level 3 — read-only shadow map

A candidate is coherent enough for browsing/reporting. The map exists in RAM only and does not modify `ps2hdd` state.

### Level 4 — repair candidate

An exact topology patch list exists and every touched original header can be preserved externally.

### Level 5 — repaired and raw revalidated

All planned writes completed, every touched header matches the planned final bytes, and the session requires restart.

### Level 6 — normal admission after restart

`ps2hdd` sees the repaired disk through its normal initialization path. Only then should normal writable workflows resume.

## Discovery algorithm

`apa_forensic_scan()` is portable and receives a raw-reader callback.

It performs two discovery phases.

### Coarse grid scan

Standard APA allocations are aligned to at least `0x40000` sectors (128 MiB), so the scanner samples:

```text
LBA 0
0x40000
0x80000
0xC0000
...
```

A sampled sector pair is not accepted merely because zero-filled fields happen to satisfy weak checks. Candidate admission requires meaningful semantic anchors such as recognizable APA magic/ID or credible nonzero geometry.

### Reference chasing

Every credible discovered header can seed direct reads of:

```text
next
prev
main
subs[i].start
```

This allows recovery to find:

- subpartitions outside the coarse scan grid;
- partitions reachable from one surviving side of a broken chain;
- structures whose master link is damaged but which are still referenced elsewhere.

## Node evidence

Each discovered header retains both raw bytes and parsed evidence, including:

- physical LBA;
- stored and calculated APA checksum;
- `next` / `prev`;
- `start` / `length`;
- type and flags;
- `main`, `number`, `nsub`;
- ID;
- source evidence (grid or direct reference);
- confidence score.

Raw bytes are retained because a later repair transaction must prove that the source has not changed since the scan.

## Candidate maps

The scanner can expose up to three map hypotheses.

### Forward map

Starts from LBA 0 and follows `next`, supplementing a broken direct link only when reciprocal evidence identifies a unique successor.

### Reverse map

Uses the master/tail and `prev` evidence to reconstruct the chain from the opposite direction.

### Geometry map

Uses credible physical extents sorted by LBA as an independent topology hypothesis.

These maps intentionally use different evidence. Agreement between them is informative; disagreement remains visible.

## Map scoring

A map records:

- node count;
- reciprocal neighbor links;
- inferred links;
- conflicts;
- overlaps;
- weak/unexplained nodes;
- confidence;
- repairability gate.

A map with overlaps or unresolved conflicts cannot become a write candidate.

A map without a reliable LBA-0 master may still be useful read-only evidence, but it cannot become writeable through the current topology repair path.

## Shadow APA

The selected candidate map is a **shadow APA**: an in-memory interpretation of disk geometry.

It may be used to:

- list likely partitions;
- inspect raw APA header details;
- export a human-readable forensic report;
- compare alternative candidate maps;
- form the basis of a future read-only extent/filesystem adapter.

It does not:

- overwrite on-disk metadata;
- return a fake healthy `HDIOC_STATUS`;
- make speculative geometry available to normal writable PFS operations.

## `FORENSIC.TXT`

The forensic controller can save a human-readable report to the currently selected storage target:

```text
FORENSIC.TXT
```

The report includes:

- total disk sectors;
- grid/reference/raw-read statistics;
- truncation/unreadable-read counters;
- candidate maps and confidence;
- reciprocal/inferred/conflict/overlap counts;
- repair-plan classification;
- discovered header LBAs and IDs;
- start/length;
- `prev`/`next`;
- type/flags;
- `main`/`number`/`nsub`;
- checksum status.

This report is evidence for human review. It is not currently a machine-stable interchange format.

## Topology repair scope

The current forensic writer is intentionally narrow. It may change only:

```text
prev
next
checksum
```

All other bytes of a repaired header come from the original raw header observed during scan.

This avoids turning a graph hypothesis into permission to normalize unrelated metadata.

## Checksum corroboration

APA uses a 32-bit additive checksum. It is not cryptographic and can have collisions.

For a graph-derived topology correction:

1. the graph determines the exact expected `prev`/`next` value;
2. that value is substituted in memory;
3. if the original stored checksum was stale and becomes valid after exactly that correction, the patch is marked **checksum-corroborated**.

If the source header already contains a checksum recomputed over the wrong link, the graph may still identify a strong candidate, but checksum no longer independently supports the correction. Such a patch is heuristic/manual, not automatic-safe.

## Bit-distance evidence

For each topology patch, the engine counts changed bits across the old and proposed link values.

One/two-bit corrections are explicitly tracked because they strongly resemble ordinary physical corruption when independent topology evidence agrees.

The regression suite includes a link with exactly two flipped bits and requires:

- correct expected neighbor from graph evidence;
- bit distance of exactly 2;
- stale checksum restored by the correction;
- automatic-safe repair classification.

The engine is not limited to two-bit differences. It does not brute-force bit combinations; the graph derives the candidate value first.

## Repair-plan classes

### Automatic-safe

Requires a sufficiently strong map and no speculative patches. Every changed topology field must be checksum-corroborated.

### Manual expert

A map may be structurally high-confidence with no overlaps/conflicts while containing a patch not independently corroborated by stale checksum. The UI labels this explicitly as heuristic and requires a stronger confirmation chord.

### Blocked

Any of the following keep the current plan read-only:

- low map confidence;
- unresolved conflicts;
- overlaps;
- missing reliable master for the current write model;
- impossible/unsupported patch scope;
- source changed after scan;
- snapshot failure;
- final candidate fails safety validation.

## `HDDMETA` snapshot format

Before a forensic topology write, Michishirube saves one of:

```text
HDDMETA.BIN
HDDMETA2.BIN
```

The file is an application recovery artifact, **not** an APA disk format.

Current version magic:

```text
APAMETA1
```

The image contains:

### Fixed header

- format magic;
- format version;
- total disk sector count;
- selected candidate-map index;
- map confidence;
- patch count;
- checksum-corroborated count;
- speculative count;
- one/two-bit correction count;
- reserved bytes for future compatible metadata.

### Per-header entries

For every header the plan intends to modify:

- physical LBA;
- SHA-256 of the original 1024-byte header;
- exact original 1024 bytes.

### Trailer

- SHA-256 of the complete snapshot payload preceding the trailer.

The snapshot is written to external storage and read back byte-for-byte before the HDD write path can proceed.

Existing snapshot slots are not silently overwritten. A pre-existing file is reusable only if its size and exact bytes match the newly generated snapshot image.

## Write transaction

`hdd_forensic_repair_ps2` does not decide which topology is correct. It executes an already built plan.

### Source-stability gate

Immediately before each write, the current 1024-byte raw header is read again and compared with the exact source bytes retained by the scan.

If they differ, the operation stops. This prevents a stale UI/plan from writing over media that changed after analysis.

### Commit ordering

```text
HDDMETA snapshot verified
        |
        v
non-master header 1 -> flush -> readback compare
non-master header 2 -> flush -> readback compare
...
        |
        v
master LBA 0 LAST -> flush -> readback compare
        |
        v
raw reread of every touched header
        |
        v
mandatory restart
```

The master-last rule mirrors the normal bootstrap payload-first/pointer-last invariant: subordinate state should be committed and verified before the root metadata advertises the repaired topology.

## Partial failure

A multi-header transaction cannot promise atomicity across physical ATA/cache/power boundaries.

If a later write fails, earlier verified headers may already have changed. Therefore:

- the complete original touched set exists in `HDDMETA` before the first write;
- the application reports a partial/failed transaction explicitly;
- no further HDD operation is allowed in the same logical workflow;
- restart is mandatory;
- follow-up forensic scan should determine actual on-disk post-failure state before any recovery decision.

## UI workflow

Typical workflow:

```text
Recovery
  -> APA forensic / degraded read-only
       -> Raw scan
       -> Candidate map A/B/C
       -> Browse nodes / evidence
       -> Save FORENSIC.TXT
       -> Inspect repair plan
       -> Save/verify HDDMETA
       -> Explicit confirmation
       -> Apply verified topology plan
       -> Restart
```

Normal read-only map inspection requires no destructive confirmation.

Automatic-safe and expert plans use different confirmation chords so a heuristic write cannot be mistaken for an evidence-complete repair.

## Controller activity indication

Long forensic reads/writes call the nested pad activity API. On supported DualShock-style controllers this requests analog mode, which normally illuminates the red ANALOG lamp.

The lamp is not independently controllable, so it is **not blinked**. The screen remains authoritative for operation status.

Hardware validation must include controller-mode behavior because third-party pads can implement Sony commands incompletely or differently.

## Hardware validation checklist

Before forensic writes are release-ready, test at least:

### Raw scanning

- several disk capacities;
- original IDE HDD and representative SATA adapters where available;
- readable bad APA master;
- broken internal link;
- off-grid referenced subpartition;
- unreadable raw-read error handling;
- scan duration and UI responsiveness.

### External snapshot storage

- `mc0:`;
- `mc1:`;
- `mass:`;
- existing `HDDMETA` slot;
- nearly full target;
- slow USB/media;
- disconnect/failure before HDD mutation.

### Topology write

- one corroborated non-master link repair;
- exact two-bit corroborated link repair;
- several-header plan;
- master-only topology patch where allowed;
- non-master-first ordering;
- master-last ordering;
- source-changed abort;
- read-back mismatch path;
- restart and fresh raw rescan.

### Controlled interruption

Where safely reproducible on disposable/snapshotted media, interrupt at:

- before snapshot completion;
- after snapshot but before HDD write;
- after first interior header;
- after flush;
- before master write;
- after master write before final full verification.

Every observed result should become a deterministic host regression if it reveals a policy or classification edge case.

### Controller activity

Test:

- original DualShock 2;
- controller starting in digital mode;
- controller starting in analog mode;
- representative third-party controller;
- unsupported/main-mode-failure fallback;
- restart/power restoration of the initial mode.

## Future 0.5 direction

The portable forensic core can support broader tooling without granting additional PS2 write authority:

- host-side `HDDMETA`/`HDDRAW` inspector;
- machine-readable forensic report;
- raw-image graph reconstruction using the same portable engine;
- read-only bulk export from shadow APA extents;
- PFS/HDL-aware read-only inspection where intact filesystem metadata permits it;
- comparison of current raw scan against an earlier `HDDMETA`/forensic record.

Those features should consume the same trust ladder rather than inventing another independent recovery model.
