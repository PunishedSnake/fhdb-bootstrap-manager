# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. Semantic versions remain the authoritative machine-readable identifiers.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, guarded installation. |
| `0.3.x` | **Torii** (鳥居) | gateway | Stable rescue capsules, payload restoration, boot-chain diagnostics, CI, compatible MBR.XIN/XLF handling. |
| `0.4.x` | **Michishirube** (道標) | signpost | Modular architecture, regression laboratory, guarded metadata recovery, forensic APA reconstruction, scalable UI. |
| `0.5.x` | **Kakehashi** (架け橋) | bridge | Broader interoperability, extraction, host-side tools, and filesystem-aware read-only recovery. |
| `1.0.0` | **Kaidō** (街道) | main road | Frozen recovery/rescue contracts, reproducible releases, and broad real-hardware validation. |

## 0.4.x — Michishirube

Michishirube is now structurally feature-complete enough that the remaining work is dominated by **hardware validation, recovery hardening, and regression expansion** rather than monolith extraction.

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

Portable CI now covers:

- SHA-256/capsule/rescue integrity and stale/foreign identity;
- KELF structural edge cases and sector-padding recovery;
- boot-chain parsing/classification, payload fingerprinting, and report rendering;
- bootstrap transaction success and injected failures;
- **30 deterministic sparse raw-HDD fixtures** covering enabled/disabled states, interruption, invalid payloads, pointer anomalies, checksum-only corruption, checksummed semantic corruption, stale-checksum physical-style bit flips, additive-checksum collision, torn header, PC signature, hybrid APA/GPT, GPT-only, and deterministic garbage;
- normal byte-level MBR payload overwrite / pointer-last semantics;
- all 30 fixture states through mounted repair policy with postconditions: `4 no-repair / 6 header-repair / 8 pointer-clear / 12 blocked`;
- portable forensic healthy graph reconstruction;
- stale-checksum broken-link repair;
- checksummed wrong-link manual-only classification;
- off-grid subpartition discovery through surviving references;
- missing-master write gate;
- exact two-bit link corruption with graph-derived repair, `bit_distance == 2`, checksum corroboration, and automatic-safe classification.

## Remaining 0.4 engineering work

The next work should concentrate on real hardware and failure behavior rather than adding another broad feature layer:

1. **Physical forensic scan validation** — measure scan time and raw-read behavior on real HDDs/adapters of several capacities.
2. **Single-master raw repair validation** — verify sectors 0-1 write/flush/read-back and restart behavior on disposable/snapshotted media.
3. **Multi-header topology repair validation** — test source-stability gate, non-master-first/master-last ordering, and restart after success/partial failure.
4. **Storage validation** — confirm `HDDRAW`, `HDDMETA`, `FORENSIC.TXT`, rescue, and logs on `mc0`, `mc1`, and USB, including full/slow media and pre-existing slots.
5. **Controller matrix** — original DualShock 2 plus representative third-party pads for mode switching/activity fallback.
6. **Power-loss experiments** — controlled interruption at snapshot, interior-header write, flush, master write, and post-write verification boundaries.
7. **Synthetic expansion from every hardware finding** — each discovered edge case becomes a deterministic host fixture/regression before a fix is accepted.
8. **Performance/build telemetry** — record ELF size, forensic scan rate, raw-read failure counts, and memory ceilings before considering larger transfers or more aggressive scanning.

## 0.5.x — Kakehashi

With core APA forensic reconstruction now living in 0.4, Kakehashi can focus on interoperability instead of reimplementing the scanner:

- host-side reader for `HDDRAW`, `HDDMETA`, rescue capsules, and `FORENSIC.TXT`;
- machine-readable forensic/reconstruction report format in addition to human-readable text;
- read-only bulk extraction from shadow APA extents;
- filesystem-aware inspection/export where PFS/HDL metadata is intact;
- optional host-side raw-image reconstruction/verification using the same portable graph engine;
- richer compatibility evidence for FHDB, OSDMenu, PSBBN, HDD-OSD/HOSDMenu, and custom chains;
- tools for comparing a physical-disk scan with a previous forensic snapshot without immediately writing anything.

## 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires:

- stable rescue/recovery artifact formats or explicit migration tooling;
- reproducible tagged builds;
- no known safety-critical write-path defects;
- documented interrupted-operation behavior;
- a representative real-hardware matrix across consoles, adapters, HDDs, storage targets, and controllers;
- enough independent recovery testing that the project is no longer relying on one console as its entire quality-assurance department.
