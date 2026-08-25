# Architecture and safety invariants

PS2 HDD Bootstrap Manager deliberately separates **evidence**, **policy**, **device mechanics**, and **authorization**. The `0.4.x` **Michishirube** line is designed so that uncertain metadata can be inspected aggressively while write authority remains conservative.

The central rule is simple:

> Read-only reconstruction may form hypotheses. Write paths may only commit explicitly authorized, bounded changes whose source bytes and postconditions are verified.

## Layer model

### 1. Portable policy and format core

These modules compile without PS2SDK and perform no device I/O:

- `apa.c` — little-endian APA parsing, additive checksum, standard master recognition, conservative hybrid guard, and same-disk identity checks.
- `apa_repair.c` — fail-closed planning for narrowly reconstructable canonical master fields.
- `apa_forensic.c` — raw-evidence graph reconstruction, candidate-map scoring, topology repair planning, checksum corroboration, and bit-distance classification.
- `repair_health.c` — mounted-disk pointer/payload health policy and pointer-clear recommendation.
- `hdd_bounds.c` — deterministic OSD pointer/geometry rules and stable error ordering.
- `kelf.c` — structural KELF parsing and recovery of unpadded size from sector images.
- `bootstrap_transaction.c` — payload-first/pointer-last sequencing and failure-stage reporting.
- `rescue_image.c`, `capsule_format.c` — rescue validation and endian-stable serialization.
- `boot_chain.c`, `boot_payload.c`, `boot_report.c` — evidence classification, fingerprinting, and report rendering.
- `sha256.c` — portable hashing used by rescue, diagnostics, and forensic snapshots.

Portable code may decide **what the evidence supports**, **what repair plan is safe**, or **what operation should follow**. It cannot touch HDD, memory-card, USB, PFS, MagicGate, or controller APIs.

### 2. PS2 device/service adapters

These modules bind narrow operations to PS2SDK:

- `hdd_read.c` — read-only raw sector/payload acquisition and live `__mbr` geometry.
- `hdd_write.c` — normal bootstrap payload write/flush/read-back plus `HDIOC_SETOSDMBR` pointer updates.
- `hdd_repair_ps2.c` — exceptional two-sector APA master writer for deterministic startup recovery.
- `hdd_forensic_repair_ps2.c` — multi-header topology writer; validates source stability, writes non-master headers before LBA 0, flushes, reads back, and re-verifies every touched header.
- `header_backup.c` — mandatory normal-operation master backup mechanics.
- `repair_snapshot.c` — exact `HDDRAW*.BIN` snapshot before single-master raw recovery.
- `forensic_snapshot.c` — versioned `HDDMETA*.BIN` preservation of every pre-repair header touched by a forensic topology plan, including per-header SHA-256 and complete snapshot digest.
- `rescue_storage.c` — PS2-side rescue file lifecycle.
- `bootstrap_source.c` — MBR.XIN/XLF-compatible source preparation.
- `bootstrap_signing.c` — MagicGate signing and post-sign structural validation.
- `bootstrap_transaction_ps2.c` — PS2 binding for the portable normal transaction sequencer.
- `boot_chain_ps2.c`, `boot_payload_ps2.c`, `boot_diagnostics_ps2.c`, `boot_report_ps2.c`, `boot_report_session.c` — read-only evidence acquisition and report persistence.
- `storage.c`, `platform.c`, `session_log.c` — storage state, IOP/pad lifecycle, and ordered logging.
- `hdd_recovery_wrap.c` — intercepts exactly the first `hdd0:` `HDIOC_STATUS` so a raw-readable invalid master can enter guarded startup recovery before normal admission fails.

A device adapter performs mechanics. It must not invent higher-level recovery policy.

### 3. Controllers and presentation

High-level flows are split from device and format logic:

- `bootstrap_controller_ps2.c` — normal backup, disable, restore, and install authorization/error flow.
- `diagnostics_controller_ps2.c` — boot-chain refresh, report save, and presentation.
- `repair_controller_ps2.c` — deterministic startup/master repair and mounted structure-health presentation.
- `forensic_controller_ps2.c` — raw forensic scan, candidate-map browsing, report export, repair-plan preview, snapshot gate, and forensic topology write authorization.
- `manager_menu_ps2.c` — hierarchical dashboard/navigation and state-dependent feature availability.
- `app_ui_ps2.c` — shared fatal/info/power/storage/signing-card presentation.
- `platform.c` — pad initialization/input and best-effort activity-mode control.

### 4. Composition root

`main.c` owns only:

1. launch storage selection;
2. screen/IOP/fileXio/power/signing/pad initialization;
3. first HDD status / startup recovery opportunity;
4. normal APA admission;
5. initial session diagnostics;
6. hand-off to `manager_menu_ps2`.

New recovery algorithms, snapshot formats, write loops, UI sections, or device protocols do not belong in `main.c`.

## Hierarchical UI contract

Michishirube no longer uses one global controller button per feature. The
root dashboard exposes six cards:

```text
Bootstrap
Diagnostics
Recovery
HDL Tools
Backup & Storage
System
```

The root uses the directional pad to move through a two-by-three card grid and
keeps the selected card's short description in a separate panel. Ordinary
submenus retain `UP/DOWN`, `X`, and `TRIANGLE` list navigation. Destructive
operations still use explicit confirmation chords after a preview/snapshot
stage.

An unavailable operation remains visible with a reason. This prevents feature growth from consuming additional controller buttons and makes application state explicit.

## Controller activity indication

The DualShock/DS2 red **ANALOG** lamp follows the controller's main digital/analog mode; it is not exposed as an independent LED.

Therefore Michishirube does not blink it. `platform` implements a best-effort nested activity state:

- idle requests digital mode / lamp off;
- activity requests analog mode / lamp on;
- nested activity remains on until the outer operation finishes;
- restart/power-off restores the initial controller mode;
- unsupported pads fall back to screen-only progress without blocking the operation.

The screen is the authoritative activity indicator. Lamp behavior remains a hardware-validation item.

## Normal Torii-compatible write invariants

The stable normal paths preserve these rules:

1. `ps2hdd` accepts the disk and the current master passes standard APA/checksum/non-hybrid validation.
2. A current 1024-byte master backup is saved and read back before HDD mutation.
3. The user explicitly authorizes the operation.
4. Raw writes stay inside the reserved `__mbr` program area beginning at sector `0x2000`; normal workflows do **not** raw-write sectors 0-1.
5. New/restored payload is written, flushed, and compared before pointer exposure.
6. `osdStart`/`osdSize` changes use `HDIOC_SETOSDMBR` and are read back.
7. A damaged or wrong-disk rescue capsule blocks fallback to a weaker restore.

These invariants remain authoritative even as the UI and module layout evolve.

## Exceptional deterministic master recovery

This path exists only because a damaged LBA-0 master can prevent rule 1 above from starting.

The path is:

```text
first HDIOC_STATUS
    -> raw sectors 0-1
    -> apa_repair policy
    -> HDDRAW snapshot
    -> explicit confirmation
    -> hdd_repair_ps2
    -> flush + exact readback
    -> mandatory restart
```

Automatic repair remains narrow:

- one known canonical master identity/anchor field;
- sufficient independent identity evidence;
- stale checksum mismatch;
- correcting only that field restores the old stored checksum;
- no GPT/protective-MBR signal;
- completed candidate master passes full standard validation.

Checksum-valid noncanonical state, checksum-only mismatch, low identity, or unexplained multiple corruption are not automatically legalized by recalculating a checksum.

## Why additive checksum is only supporting evidence

APA protects a header with a 32-bit additive checksum. It is useful for accidental-change evidence but is not collision-resistant.

Two independent changes can cancel mathematically. Consequently:

- a matching checksum does not prove a suspicious structure is healthy;
- a stale checksum can corroborate an exact externally derived correction;
- checksum-only mismatch cannot identify the damaged word;
- checksummed semantic corruption remains ambiguous unless other graph evidence resolves it.

This distinction applies to both single-master and multi-header recovery.

## Implemented forensic / degraded read-only recovery

Forensic recovery is no longer a future concept. `apa_forensic` implements a raw read-only scanner and graph builder.

### Discovery

The scanner:

1. raw-reads LBA 0;
2. samples the standard APA allocation grid at `0x40000`-sector intervals (128 MiB);
3. rejects blank/no-anchor sectors rather than scoring technically valid zeros;
4. follows surviving `next`, `prev`, `main`, and `subs[]` references directly, including off-grid subpartitions;
5. records source evidence, checksum state, identity, geometry, type, flags, subpartition metadata, and confidence for every discovered node.

The scanner is intentionally read-only and can operate even when normal `ps2hdd` admission is not trustworthy.

### Candidate maps

Up to three independent maps are generated:

- **forward links** — follows `next` plus unique reciprocal evidence;
- **reverse links** — reconstructs from `prev`/master-tail evidence;
- **geometry order** — sorts credible extents by LBA.

Each map records:

- node count;
- reciprocal links;
- inferred links;
- conflicts;
- overlaps;
- confidence;
- whether it is structurally eligible to become a repair candidate.

Multiple plausible maps may coexist. The UI exposes them rather than silently choosing one.

### Shadow APA / degraded read-only state

A candidate map exists only in RAM. It does not patch `ps2hdd` status and does not make the disk writable.

The forensic controller can browse map nodes and export `FORENSIC.TXT`, including:

- discovered LBA and ID;
- start/length;
- `prev`/`next`;
- type/flags;
- `main`/`nsub`;
- checksum state;
- confidence/evidence;
- map statistics;
- proposed topology changes.

This is the project's **shadow APA**: a read-only interpretation of the raw disk, not a claim that the physical metadata is healthy.

## Forensic topology repair

Broader repair is intentionally limited to topology that the candidate graph can reconstruct.

Current writeable fields are only:

```text
prev
next
checksum
```

The engine does not synthesize arbitrary IDs, lengths, timestamps, passwords, PFS metadata, or missing content.

### Repair-plan classes

For each changed header, the plan records:

- original and proposed `prev`;
- original and proposed `next`;
- bit distance between old/new link values;
- whether restoring the graph-derived value also restores the old stored checksum.

A plan can be:

- **automatic-safe** — high-confidence map, no conflicts/overlaps, all changes checksum-corroborated;
- **manual expert** — high-confidence structurally coherent map but at least one topology correction lacks independent checksum corroboration;
- **not writable** — map confidence/evidence is insufficient or structural conflicts remain.

User selection of a candidate map is authorization to consider a hypothesis, not evidence that makes it true.

## Exact two-bit recovery contract

The engine does not brute-force all two-bit combinations across the disk.

Instead:

1. neighboring/reciprocal graph evidence derives the expected exact link value;
2. the current and expected values are compared;
3. bit distance is recorded;
4. if the source checksum is stale, applying exactly that graph-derived change must restore the old stored checksum.

The host suite contains an explicit two-bit link corruption case and requires:

- correct candidate reconstruction;
- `bit_distance == 2`;
- checksum corroboration;
- automatic-safe classification.

This generalizes naturally to exact graph-derived changes with more than two altered bits, although the UI/report specifically highlights one/two-bit cases because they are strong physical-corruption evidence.

## `HDDMETA` forensic snapshot

Before any forensic topology write, `forensic_snapshot` saves every original header the plan intends to touch.

The versioned snapshot contains:

- format magic/version;
- disk sector count;
- selected map index/confidence;
- patch count and corroboration statistics;
- one/two-bit correction count;
- for every patch: LBA, SHA-256, exact original 1024-byte header;
- SHA-256 over the complete snapshot payload.

Files are non-overwriting slots:

```text
HDDMETA.BIN
HDDMETA2.BIN
```

Snapshot creation itself is write/read-back verified before any HDD metadata change is authorized.

## Multi-header commit invariants

`hdd_forensic_repair_ps2` follows these rules:

1. Plan and scan source must be internally consistent.
2. Immediately before each write, the current raw header must still equal the exact bytes seen by the scan. If not, the transaction stops with a source-changed failure.
3. Each repaired header is built by the portable core; the writer does not invent fields.
4. Non-master headers are written first.
5. Each header write is followed by flush and exact raw read-back comparison.
6. LBA 0 is written **last**.
7. The master candidate must remain a complete standard non-hybrid APA master.
8. After all writes, every touched header is raw-read again and compared with the planned final bytes.
9. Any success or partial failure requires restart before further HDD work.

Master-last is the topology equivalent of the normal payload-first/pointer-last rule: do not advertise repaired root metadata until the subordinate structure has already been committed and verified.

## Trust ladder

Recovery state is explicit:

1. **Raw readable** — sectors can be read; no structure claimed.
2. **Forensic candidate map** — one or more plausible graphs exist.
3. **Read-only shadow map** — candidate is coherent enough for browsing/reporting, still no HDD writes.
4. **Repair candidate** — exact field-level plan and complete pre-repair snapshot exist.
5. **Repaired + raw revalidated** — every touched header matches the planned final bytes.
6. **Normal ps2hdd admitted after restart** — standard manager write paths may be considered again.

Calling levels 2-3 “healthy” is intentionally avoided.

## Filesystem boundary

APA topology recovery can recover **where partitions are and how they are linked** when sufficient headers survive. It cannot recreate sectors that are genuinely missing.

PFS/HDL/game/file recovery is a separate problem. A future read-only filesystem adapter may consume a shadow APA extent for inspection or export, but speculative geometry must never be silently injected into normal writable PFS/APA services.

## Regression gates

`make test-host` covers:

- APA/capsule/SHA-256 formats;
- deterministic master repair policy;
- forensic graph reconstruction;
- healthy graph, stale-checksum broken link, checksummed heuristic-only link, off-grid subpartition discovery, missing-master write gate, and exact two-bit corruption;
- boot-chain/payload/report/KELF suites;
- transaction failure injection;
- rescue validation;
- generated raw-HDD parser/bounds/KELF states;
- normal payload/pointer mutation semantics;
- mounted `repair_health` policy and repair postconditions.

The R5900 release build is warning-clean under the pinned PS2DEV toolchain.

## Hardware gate

Host/CI cannot prove:

- DEV9/ATA timing;
- fileXio RPC/DMA behavior;
- write-cache durability;
- APA journal interaction;
- adapter/controller quirks;
- physical power-loss boundaries.

Therefore the following remain hardware-unproven until explicitly tested on real PS2 HDD setups:

- raw sectors 0-1 deterministic master recovery;
- full-disk forensic scan behavior/performance;
- multi-header topology repair and master-last commit ordering;
- `HDDMETA` storage behavior;
- ANALOG-lamp activity mode across original and third-party pads.

## Commenting rule

Comments should explain ownership, evidence, safety gates, hardware constraints, and failure ordering. They should not narrate C syntax.

In particular:

- “normal paths never write sector zero” must be qualified as a **normal Torii-compatible invariant**;
- deterministic raw master recovery and forensic topology recovery are explicit exceptions with their own gates;
- a forensic candidate must never be described in code comments as a healthy or mounted disk unless normal admission has actually occurred.
