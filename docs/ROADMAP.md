# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. Semantic versions remain the authoritative machine-readable identifiers.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, guarded installation. |
| `0.3.x` | **Torii** (鳥居) | gateway | Stable rescue capsules, payload restoration, boot-chain diagnostics, CI, compatible MBR.XIN/XLF handling. |
| `0.4.x` | **Michishirube** (道標) | signpost | Modular architecture, regression laboratory, guarded metadata recovery, forensic APA reconstruction, scalable and observable UI. |
| `0.5.x` | **Kakehashi** (架け橋) | bridge | Versioned recovery interchange and cross-tool interoperability contracts. |
| `0.6.x` | **Watari** (渡り) | crossing | Host-assisted repair-plan round trip with PS2-side revalidation and final write authority. |
| `0.7.x` | **Sekisho** (関所) | checkpoint | Transaction journal, interruption recovery, rollback discipline and write-contract hardening. |
| `0.8.x` | **unassigned** | — | Reserved for a coherent milestone exposed by hardware/interoperability work; no feature is invented merely to fill the number. |
| `0.9.x` | **Tōge** (峠) | mountain pass | Feature-frozen pre-1.0 hardware, compatibility and artifact-format stabilization. |
| `1.0.0` | **Kaidō** (街道) | main road | Frozen recovery/rescue contracts, reproducible releases, and broad real-hardware validation. |

## Product boundary after Michishirube

Michishirube grew far beyond its original modularization milestone. The future roadmap therefore separates **PS2-side recovery authority** from **general host-side disk/filesystem tooling**.

PS2 HDD Bootstrap Manager owns:

- bootstrap-pointer and signed-MBR management on the console;
- APA recovery evidence collection on the console;
- PS2-side safety policy and final authorization for HDD metadata writes;
- recovery/rescue artifacts generated before a console-side write;
- degraded read-only recovery when normal PS2 APA admission fails.

PS2 DriveForge owns or should own:

- general host-side APA/PFS browsing and extraction;
- Explorer/Dokany integration;
- host-side performance/cache/read-ahead work;
- generic physical-drive/image inspection;
- later general-purpose host-side PFS/APA management.

The two projects may share **formats, evidence and repair-plan contracts**. They should not independently grow duplicate host browsers, PFS extractors, mount providers or physical-drive management UIs merely because both have portable knowledge of APA.

## Scope guardrail for every future Bootstrap Manager feature

Before assigning a feature to this project, ask:

1. Does it need to execute on a PS2 to recover or safely repair a PS2 HDD?
2. Is its primary purpose recovery/bootstrap integrity rather than normal file management?
3. Would implementing it here duplicate a capability that belongs naturally in DriveForge?
4. Can it preserve the existing rule that host-generated evidence/plans never bypass PS2-side revalidation before a physical write?

If the answers point toward a general host tool, the feature belongs in DriveForge or in a shared interchange specification, not in the manager ELF.

## 0.4.x — Michishirube

Michishirube is now under **feature freeze** except for fixes or narrowly scoped instrumentation required by hardware validation. New broad subsystems move to later milestones.

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
- [x] `app_ui_ps2` — shared presentation/lifecycle helpers and contextual error presentation.
- [x] `manager_menu_ps2` — hierarchical dashboard replacing global one-button-per-feature shortcuts.
- [x] `disk_status_ps2` — throttled live HDD/LBA/action status fed by real transport/write stages.
- [x] `app_error` — domain/stage-aware symbolic error catalog while preserving original numeric return codes.
- [x] `main.c` composition root — startup/admission then hand-off to the dashboard; heavy boot-chain evidence collection is lazy.

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
- [x] guarded host fault injector for reproducible one-bit master and one/two-bit topology corruption on images/physical test disks;
- [ ] physical-HDD validation of exceptional raw metadata write paths.

### UI / startup / observability

- [x] hierarchical sections: Bootstrap, Diagnostics, Recovery, Backup & Storage, System;
- [x] standard `UP/DOWN`, `X`, `TRIANGLE` navigation;
- [x] unavailable actions remain visible with a reason;
- [x] destructive confirmation chords remain separate from navigation;
- [x] forensic candidate-map and patch inspection;
- [x] best-effort steady ANALOG-lamp activity indication;
- [x] defer full PFS/MC boot-chain diagnostics until requested rather than blocking dashboard startup;
- [x] log startup timing for IOP reset, modules, services, pad, HDD status, header read and total pre-dashboard time;
- [x] live HDD monitor with current operation, phase, raw LBA/range, physical-disk position and progress bar;
- [x] immediate live redraw for destructive WRITE/FLUSH/VERIFY/pointer stages and throttled ordinary read/scan redraw;
- [x] forensic repair display of source-stability checks, interior-header writes, master-last LBA-0 commit and final touched-set verification;
- [x] contextual errors showing symbolic ID, stage, summary, reason, recommended next step and preserved raw code;
- [x] raw IOP failures are described from operation context instead of guessing the meaning of a small negative integer alone;
- [ ] validate fast-start timing, live-monitor readability/performance, contextual error screens and ANALOG-lamp behavior on hardware.

## Current regression coverage

Portable CI covers:

- SHA-256/capsule/rescue integrity and stale/foreign identity;
- KELF structural edge cases and sector-padding recovery;
- boot-chain parsing/classification, payload fingerprinting, and report rendering;
- bootstrap transaction success and injected failures;
- **30 deterministic sparse raw-HDD fixtures** covering mounted/deterministic policy;
- all 30 fixture states with postconditions: `4 no-repair / 6 header-repair / 8 pointer-clear / 12 blocked`;
- **9 sparse 512 MiB forensic raw-HDD E2E fixtures** through production `apa_forensic_scan()`;
- one-bit and exact two-bit stale-checksum topology recovery;
- overlap/conflict/missing-master write gates;
- guarded hardware fault-injector image self-test for probe/mutation/read-back/restoration;
- contextual error-catalog mapping and record/consume lifecycle tests.

## Hardware evidence already collected for 0.4

A first healthy-disk real-console pass has been recorded in [`HARDWARE_VALIDATION_0.4.md`](HARDWARE_VALIDATION_0.4.md).

Observed/supplied evidence includes:

- valid 1024-byte `HDDMBR.BIN` with matching APA checksum;
- version-1 `HDDRESCUE.BIN` whose embedded header is byte-identical to `HDDMBR.BIN`;
- successful `BOOTCHAIN.TXT` generation;
- successful standalone backup/reuse behavior;
- storage selection behavior;
- fail-closed missing MBR-source path;
- tester-observed hierarchical UI and unavailable-action gating working as intended.

The same pass exposed an approximately **1–2 minute pre-dashboard initialization delay**. The current branch defers the previously automatic full boot-chain scan and adds phase timing; that fast-start change now requires a second hardware measurement.

## Remaining 0.4 engineering work

1. **Fast-start measurement** — collect the new `Startup timing ms:` line and identify any remaining module/DEV9/pad bottleneck.
2. **Live-monitor baseline** — validate current operation/action/LBA/range/progress readability and measure whether redraw throttling materially changes forensic scan time.
3. **Contextual error UX** — deliberately trigger missing MBR source and representative snapshot/bounds/recovery failures; confirm symbolic explanation and recommendation match the actual failing stage.
4. **Physical forensic baseline** — compare read-only Michishirube topology with DriveForge's independently known view of the sacrificial test HDD.
5. **One-bit deterministic master fault** — use the guarded fault injector and validate `HDDRAW` + sectors 0-1 repair/restart.
6. **One-bit topology fault** — validate shadow-map reconstruction, `HDDMETA`, exact patch and restart.
7. **Exact two-bit topology fault** — require bit distance 2 plus stale-checksum corroboration on hardware.
8. **Multi-header topology repair** — only after the single-header tests pass.
9. **Storage/controller/power-loss matrix** — `mc0`, `mc1`, USB, slow/full media, original/third-party pads, then controlled interruption boundaries.
10. **Regression capture** — every hardware discrepancy becomes a host fixture/test before its fix is accepted.

The HDD previously used for DriveForge testing and the HDD currently installed in the physical PS2 are distinct devices. The DriveForge test HDD can therefore be treated as the sacrificial fault-injection target while the console HDD remains a known-good baseline.

Controlled physical corruption procedure: [`HARDWARE_FAULT_INJECTION.md`](HARDWARE_FAULT_INJECTION.md).
Live-status/error behavior: [`STATUS_AND_ERRORS.md`](STATUS_AND_ERRORS.md).

## 0.5.x — Kakehashi

**One purpose: make recovery evidence portable between tools without moving write authority away from the PS2.**

Planned scope:

- version and freeze a machine-readable forensic evidence manifest;
- define stable disk/session identity fields so artifacts can be matched to the correct physical HDD;
- machine-readable representation of candidate maps, confidence/evidence and proposed patches;
- host-reference parser/validator for `HDDRAW`, `HDDMETA`, rescue capsules and forensic manifests, preferably shared with or consumed by DriveForge rather than becoming a second end-user host application;
- compatibility/migration rules for future artifact versions;
- golden/reference artifact corpus in CI;
- explicit capability/version negotiation for imported evidence.

Not Kakehashi scope:

- another Windows GUI;
- generic PFS browsing/export;
- Dokany/FUSE mounting;
- host physical-drive write support;
- generic APA partition management.

Those belong to DriveForge or existing host tooling.

### Exit criteria

- every recovery artifact has a documented versioned schema;
- malformed/foreign/stale artifacts fail closed;
- at least one independent host implementation can parse the reference corpus;
- artifact round trips are byte/digest reproducible where the format promises reproducibility.

## 0.6.x — Watari

**One purpose: safely cross the PS2/host boundary with a repair plan.**

Proposed workflow:

```text
PS2 raw evidence / snapshot
        ↓
host analysis (for example DriveForge)
        ↓
versioned repair-plan artifact
        ↓
PS2 imports plan
        ↓
PS2 re-reads current disk
        ↓
PS2 independently rebuilds/revalidates every safety precondition
        ↓
user previews exact diff
        ↓
PS2 performs final write through existing recovery adapters
```

Requirements:

- imported plans are **suggestions**, never write commands trusted on signature/file presence alone;
- disk identity and source-header digests must match;
- PS2 rebuilds expected resulting headers itself;
- no arbitrary LBA/data write primitive is exposed by the plan format;
- stale plans fail closed;
- report which parts of a host proposal were accepted, rejected, or changed by console-side policy.

## 0.7.x — Sekisho

**One purpose: make interrupted recovery a first-class recoverable state.**

Potential scope after hardware evidence justifies it:

- versioned transaction journal stored externally before mutation;
- transaction ID bound to disk/session identity and exact source headers;
- explicit stages such as `SNAPSHOT`, `INTERIOR_WRITES`, `MASTER_PENDING`, `MASTER_COMMITTED`, `VERIFY_PENDING`, `COMPLETE`;
- startup detection of an incomplete manager-owned transaction;
- deterministic decision between resume, verify-only, rollback-from-external-evidence, or fail closed;
- rollback tooling constrained to exact headers/payload regions previously captured by the manager;
- fault-injection matrix at every transition;
- no assumption that flush equals power-loss durability until real hardware supports that conclusion.

## 0.8.x — intentionally unassigned

Do not manufacture a feature train solely because `0.8` is numerically available. Assign it only if hardware/interoperability work exposes a coherent milestone not naturally belonging to Kakehashi, Watari, Sekisho or Tōge.

## 0.9.x — Tōge

Tōge is the **feature-frozen pre-1.0 stabilization line**.

Required work:

- broad console/ROMVER matrix;
- official and third-party network/SATA/IDE adapter matrix where technically applicable;
- multiple HDD/SSD/bridge capacities and vendors;
- memory-card and USB storage matrix;
- controller/activity fallback matrix;
- long-running forensic scans and repeated recovery cycles;
- fuzz/mutation corpus expansion from all prior hardware bugs;
- freeze or explicitly version-bump rescue/evidence/repair-plan formats;
- reproducible release artifacts and published hashes;
- external tester checklist that does not require reading source code;
- classify every write path as stable, experimental, or removed before 1.0.

No large new subsystem should enter Tōge.

## 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires:

- stable rescue/recovery/evidence/repair-plan artifact contracts or explicit migration tooling;
- reproducible tagged builds;
- no known safety-critical write-path defects;
- documented interrupted-operation behavior;
- a representative real-hardware matrix across consoles, adapters, HDDs, storage targets, and controllers;
- cross-tool artifact validation where interoperability is promised;
- enough independent recovery testing that the project is no longer relying on one console as its entire quality-assurance department.
