# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. Semantic versions remain the authoritative machine-readable identifiers.

Michishirube grew far beyond its original modularization milestone. This roadmap therefore treats `0.4.x` as a deliberate architectural pivot: the project is now a **PS2-side HDD bootstrap and APA recovery authority**, not a general-purpose host filesystem manager.

## Project boundary after Michishirube

The project should not grow by duplicating capabilities that belong on the host.

**PS2 HDD Bootstrap Manager owns:**

- on-console HDD bootstrap inspection, rescue, disable/restore/install;
- APA metadata health, forensic reconstruction and guarded recovery;
- recovery evidence capture and transaction authorization on the physical console;
- explicit PS2-side verification immediately before and after every HDD metadata write;
- controller/UI flows for recovery when a PC is unavailable.

**PS2 DriveForge owns:**

- Windows/host-side physical-drive and disk-image access;
- general APA/PFS browsing and recursive extraction;
- host filesystem integration, Explorer/Dokany presentation and performance work;
- later host-side APA/PFS/HDL mutation and richer disk-management workflows.

The two projects may share **formats, evidence and repair-plan contracts**, but they should not independently reimplement each other's frontends or filesystem-management scope.

Existing host projects such as `pfsshell`/`pfsfuse` and `hdl-dump` remain compatibility references, not features to duplicate inside the PS2 ELF.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, guarded installation. |
| `0.3.x` | **Torii** (鳥居) | gateway | Stable rescue capsules, payload restoration, boot-chain diagnostics, CI, compatible MBR.XIN/XLF handling. |
| `0.4.x` | **Michishirube** (道標) | signpost | Modular recovery architecture, regression laboratory, guarded APA metadata recovery, forensic reconstruction, scalable UI. |
| `0.5.x` | **Kakehashi** (架け橋) | bridge | Stable cross-tool recovery artifacts and machine-readable interoperability with host tooling. |
| `0.6.x` | **Watari** (渡り) | crossing / passage | Host-assisted recovery-plan round trip with PS2-side revalidation and execution authority. |
| `0.7.x` | **Sekisho** (関所) | checkpoint | Transaction journaling, interruption recovery, rollback discipline and write-contract hardening. |
| `0.9.x` | **Tōge** (峠) | mountain pass | Pre-1.0 stabilization: compatibility/hardware matrix, format freeze and release-candidate hardening. |
| `1.0.0` | **Kaidō** (街道) | main road | Frozen recovery/rescue contracts, reproducible releases and broad real-hardware validation. |

There is intentionally no feature assigned to `0.8.x` yet. A minor version will be created only if hardware validation or the Kakehashi/Watari/Sekisho work exposes a coherent milestone that deserves one; the roadmap will not invent a feature merely to fill a number.

## 0.4.x — Michishirube

Michishirube is feature-frozen except for defects exposed by validation. Its remaining work is **hardware validation, recovery hardening and regression expansion**, not another broad capability layer.

### Modularization

- [x] `platform` — IOP/module startup, pad DMA lifetime, edge input, confirmation chords, nested activity-mode control.
- [x] `storage` — selected-target state, launch-device selection, ROMVER/file helpers.
- [x] `header_backup` — verified non-overwriting normal-operation master backup.
- [x] `rescue_image` / `rescue_storage` — portable rescue validation plus PS2 lifecycle.
- [x] `bootstrap_source` / `bootstrap_signing` — source preparation and MagicGate signing.
- [x] `apa` — portable APA parsing/checksum/identity/hybrid guard.
- [x] `apa_repair` — conservative deterministic single-master repair planner.
- [x] `apa_forensic` — portable raw-evidence graph reconstruction and topology repair planning.
- [x] `repair_health` — mounted-disk pointer/payload health policy.
- [x] `hdd_bounds` — portable pointer/geometry policy.
- [x] `hdd_read` — read-only PS2 raw transport.
- [x] `hdd_write` — normal payload/pointer mechanics.
- [x] `hdd_repair_ps2` — exceptional verified two-sector master writer.
- [x] `hdd_forensic_repair_ps2` — verified multi-header topology writer with master-last ordering.
- [x] `repair_snapshot` — `HDDRAW*.BIN` single-master evidence preservation.
- [x] `forensic_snapshot` — versioned `HDDMETA*.BIN` complete pre-repair metadata preservation.
- [x] `bootstrap_transaction` + PS2 adapter — payload-first/pointer-last commit sequencing.
- [x] boot-chain / boot-payload / report modules — portable policy plus PS2 acquisition/persistence.
- [x] `bootstrap_controller_ps2` — backup/disable/restore/install authorization outside `main.c`.
- [x] `diagnostics_controller_ps2` — diagnostics workflow outside `main.c`.
- [x] `repair_controller_ps2` — deterministic startup/health repair UI outside portable policy.
- [x] `forensic_controller_ps2` — raw scan, shadow-map browsing, report/snapshot/write authorization.
- [x] `app_ui_ps2` — shared presentation/lifecycle helpers.
- [x] `manager_menu_ps2` — hierarchical dashboard replacing global one-button-per-feature shortcuts.
- [x] `main.c` composition root — startup/admission/initial refresh then hand-off to the dashboard.

### Recovery implementation

- [x] first-`HDIOC_STATUS` startup recovery entry for a raw-readable invalid master;
- [x] exact `HDDRAW*.BIN` preservation before sectors 0-1 recovery;
- [x] one canonical master-field repair only when stale checksum corroborates the exact correction;
- [x] additive-checksum collision regression and fail-closed ambiguous-state policy;
- [x] exact two-sector write + flush + read-back + mandatory restart;
- [x] mounted structure-health routing of pointer/payload failures through normal backup + pointer clear;
- [x] coarse raw APA grid scan plus direct chasing of surviving `next`, `prev`, `main`, and `subs[]` references;
- [x] forward, reverse, and geometry candidate maps with explicit confidence, conflict and overlap reporting;
- [x] read-only shadow APA browsing without spoofing `ps2hdd` health;
- [x] `FORENSIC.TXT` evidence export;
- [x] topology repair planning limited to `prev` / `next` / checksum;
- [x] one/two-bit distance classification and explicit exact two-bit stale-checksum regression;
- [x] `HDDMETA*.BIN` snapshot containing every original header touched by a forensic plan with SHA-256 protection;
- [x] source-stability check immediately before each topology write;
- [x] non-master-first / master-last multi-header commit with per-header flush/read-back and final full verification;
- [x] stronger expert confirmation for high-confidence but non-checksum-corroborated topology plans;
- [ ] physical-HDD validation of all exceptional raw metadata write paths.

### UI overhaul

- [x] replace global feature shortcuts with hierarchical sections: Bootstrap, Diagnostics, Recovery, Backup & Storage, System;
- [x] standardize normal navigation on `UP/DOWN`, `X`, `TRIANGLE`;
- [x] keep unavailable actions visible with a reason;
- [x] keep destructive confirmation chords separate from ordinary navigation;
- [x] add forensic candidate-map and patch-by-patch inspection screens;
- [x] add on-screen progress for raw scan/write operations;
- [x] implement best-effort ANALOG-lamp activity indication through controller main-mode switching without strobing;
- [ ] validate ANALOG-lamp behavior on original DualShock 2 and representative third-party pads.

## Current regression coverage

Portable CI now covers two complementary raw-disk laboratories.

### Mounted/deterministic laboratory

- **30 deterministic sparse 16 MiB raw-HDD fixtures** covering enabled/disabled states, interruption, invalid payloads, pointer anomalies, checksum-only corruption, checksummed semantic corruption, stale-checksum physical-style bit flips, additive-checksum collision, torn header, PC signature, hybrid APA/GPT, GPT-only and deterministic garbage;
- all 30 through mounted repair policy with postconditions: `4 no-repair / 6 header-repair / 8 pointer-clear / 12 blocked`;
- normal byte-level MBR payload overwrite / pointer-last semantics.

### Forensic raw-image laboratory

- **9 deterministic sparse 512 MiB raw-HDD fixtures** running production `apa_forensic_scan()` against file-backed images at realistic APA-grid LBAs;
- healthy chain reconstruction;
- stale-checksum broken link;
- exact two-bit stale-checksum link corruption;
- checksum-valid wrong link retained as manual/expert only;
- off-grid referenced subpartition discovery;
- missing-master read-only/no-write gate;
- overlapping geometry write block;
- two-header checksum-corroborated topology repair plan;
- conflicting live target write block.

The wider host suite also covers SHA-256/capsule/rescue integrity, KELF parsing, boot-chain/payload/report rendering and bootstrap transaction success/failure injection.

## Remaining 0.4 engineering work

The next work should concentrate on real hardware and failure behavior rather than adding another broad feature layer:

1. **Physical forensic scan validation** — measure scan time and raw-read behavior on real HDDs/adapters of several capacities.
2. **Single-master raw repair validation** — verify sectors 0-1 write/flush/read-back and restart behavior on disposable/snapshotted media.
3. **Multi-header topology repair validation** — test source-stability gate, non-master-first/master-last ordering, and restart after success/partial failure.
4. **Storage validation** — confirm `HDDRAW`, `HDDMETA`, `FORENSIC.TXT`, rescue, and logs on `mc0`, `mc1`, and USB, including full/slow media and pre-existing slots.
5. **Controller matrix** — original DualShock 2 plus representative third-party pads for mode switching/activity fallback.
6. **Power-loss experiments** — controlled interruption at snapshot, interior-header write, flush, master write, and post-write verification boundaries.
7. **Synthetic expansion from every hardware finding** — each discovered edge case becomes a deterministic host fixture/regression before a fix is accepted.
8. **Performance/build telemetry** — record ELF size, forensic scan rate, raw-read failure counts and memory ceilings before considering larger transfers or more aggressive scanning.

### 0.4 exit criteria

Michishirube may leave development status only when:

- read-only forensic scan matches known-good physical APA topology on the validation disks;
- deterministic master recovery has passed on expendable media;
- at least one controlled multi-header repair has passed end-to-end with byte-for-byte pre/post evidence;
- `HDDRAW`, `HDDMETA`, reports and rescue artifacts have been validated on every supported storage target;
- no known hardware finding requires weakening a current safety invariant;
- all hardware discoveries have corresponding host regressions where they can be modeled.

## 0.5.x — Kakehashi

Kakehashi is an **interoperability release**, not a second host disk manager.

### Primary goals

- define a versioned, machine-readable recovery evidence manifest alongside the existing human-readable `FORENSIC.TXT`;
- freeze/document v1 schemas for `HDDRAW`, `HDDMETA`, rescue capsules and forensic evidence, with explicit backward-compatibility rules;
- attach disk geometry, stable evidence/session identifiers, hashes and source-build identity so artifacts from different sessions cannot be silently mixed;
- provide deterministic reference vectors and conformance tests for every artifact format;
- allow DriveForge and other host tools to consume the formats without copying PS2 UI/device code;
- support comparison of two evidence bundles as a read-only operation;
- document migration policy before any on-disk/external artifact format changes.

### Explicit non-goals

Kakehashi will **not** add:

- a second general-purpose host APA/PFS browser;
- recursive PFS extraction already covered by DriveForge/pfsshell;
- Dokany/FUSE/Explorer integration;
- HDL game management;
- arbitrary host-side HDD writes.

Those belong in DriveForge or existing host tools.

### Exit criteria

- every recovery artifact has a documented version and reference vector;
- at least one independent host consumer successfully parses Kakehashi evidence;
- malformed/truncated/foreign-session artifacts fail closed;
- artifact compatibility is covered in CI.

## 0.6.x — Watari

Watari adds a **host-assisted recovery round trip** while keeping write authority on the PS2.

### Primary goals

- export a complete evidence bundle suitable for offline analysis;
- define a versioned repair-plan document containing only whitelisted recovery operations;
- allow a host tool such as DriveForge to propose a plan without granting it direct authority over the physical PS2 HDD;
- import the plan on PS2 and show the full patch-by-patch diff before authorization;
- re-read the physical disk and require exact source identity/current-byte matches before any imported operation can execute;
- recompute local safety predicates rather than trusting host-provided confidence or checksums;
- preserve the normal `HDDRAW`/`HDDMETA` snapshot requirements and master-last ordering;
- reject arbitrary-LBA writes or opaque payloads in imported plans.

### Trust rule

A host may provide **analysis and a proposal**. The PS2 manager remains the final authority that decides whether the current disk still satisfies the operation's local safety contract.

### Exit criteria

- evidence -> host analysis -> repair plan -> PS2 preview can complete with zero HDD writes;
- stale/foreign/modified-source plans are reliably rejected;
- at least one deterministic topology repair can complete through the imported-plan path and produce the same post-state as an equivalent locally generated plan;
- imported plans cannot escape the whitelisted metadata operations.

## 0.7.x — Sekisho

Sekisho is about **transaction durability and rollback discipline**, not broader repair heuristics.

### Primary goals

- introduce a versioned external recovery transaction journal containing transaction ID, source evidence identity, intended patches and progress state;
- record durable transaction boundaries before each destructive phase;
- detect an interrupted manager-owned transaction at startup;
- distinguish safe resume, verified rollback and manual-investigation states;
- support guarded restoration of touched metadata from `HDDRAW`/`HDDMETA` only when current bytes match an expected known partial/post state;
- never use rollback as permission to overwrite an unrecognized disk state;
- expand controlled power-loss testing at every journal/write/flush/master/verification boundary;
- freeze the write-operation vocabulary intended to survive into 1.0.

### Exit criteria

- every modeled interruption point produces a deterministic startup classification;
- resume/rollback never depends on guessing which write reached the platter;
- physical power-loss experiments agree with or extend the host state machine;
- every new hardware-observed partial state becomes a regression fixture.

## 0.9.x — Tōge

Tōge is the **pre-1.0 stabilization line**. No large new subsystem belongs here.

Primary work:

- freeze stable recovery/rescue/interoperability formats or provide explicit migration tooling;
- complete representative console/network-adapter/HDD/storage/controller validation matrices;
- validate recovery behavior across multiple HDD capacities and both original and third-party SATA/network adapters where possible;
- run extended fuzz/malformed-artifact campaigns against every portable parser;
- complete reproducible release packaging and provenance/checksum documentation;
- resolve UI/accessibility issues discovered during hardware testing;
- obtain independent external recovery reports where practical;
- classify every remaining write path as stable, experimental or removed before 1.0.

`0.8.x` remains available if an unforeseen but coherent milestone is required between Sekisho and Tōge.

## 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires:

- stable rescue/recovery/interoperability artifact contracts or migration tooling;
- reproducible tagged builds with recorded provenance and checksums;
- no known safety-critical write-path defects;
- documented interrupted-operation and rollback behavior;
- a representative real-hardware matrix across consoles, adapters, HDDs, storage targets and controllers;
- host-assisted plans remaining proposals rather than bypasses around PS2-side safety gates;
- enough independent recovery testing that the project is no longer relying on one console as its entire quality-assurance department.

## Scope guardrail

When considering a future feature, ask in this order:

1. **Does it need to run on the PS2 to recover or authorize recovery of the console's HDD?** If yes, it may belong here.
2. **Is it primarily general disk/PFS/HDL browsing, extraction, mounting, performance or host UI?** If yes, it belongs in DriveForge or an existing host tool.
3. **Is it only useful because Michishirube now has a portable parser?** Portability alone is not sufficient reason to duplicate another project's product surface.
4. **Does it weaken a write invariant to make an edge case more convenient?** If yes, it does not enter the roadmap without new independent evidence and regression coverage.
