# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. Semantic versions remain the authoritative machine-readable identifiers.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, guarded installation. |
| `0.3.x` | **Torii** (鳥居) | gateway | Stable rescue capsules, payload restoration, boot-chain diagnostics, CI, compatible MBR.XIN/XLF handling. |
| `0.4.x` | **Michishirube** (道標) | signpost | Modular recovery architecture, regression laboratory, forensic APA reconstruction, guarded metadata recovery, scalable/observable GS UI. |
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
- later general-purpose host-side PFS/APA/HDL management.

The projects may share **formats, evidence and repair-plan contracts**. They should not grow duplicate host browsers, PFS extractors, mount providers or disk-management UIs merely because both understand APA.

## Scope guardrail

Before assigning a future Bootstrap Manager feature, ask:

1. Does it need to run on a PS2 to recover or safely authorize repair of a PS2 HDD?
2. Is its primary purpose recovery/bootstrap integrity rather than normal file management?
3. Would implementing it here duplicate a capability that naturally belongs in DriveForge?
4. Can it preserve the rule that host-generated evidence/plans never bypass PS2-side reread/revalidation before physical writes?

If the answers point toward a general host tool, the feature belongs in DriveForge or a shared interchange specification.

# 0.4.x — Michishirube

## Status: released

**0.4.0 released 2026-08-21. 0.4.1 maintenance release published 2026-08-22.**

The line is now **feature-frozen** except for defects and narrowly scoped validation hardening. Exceptional raw metadata repair remains explicitly experimental until broader independent hardware reports exist.

### Delivered architecture

- [x] portable APA parsing, checksum, repair, forensic graph and health policy;
- [x] bounded bootstrap geometry/KELF/transaction/rescue/report formats;
- [x] separate normal, deterministic-master and forensic PS2 write adapters;
- [x] `HDDRAW*.BIN` and `HDDMETA*.BIN` evidence preservation;
- [x] controllers for bootstrap, diagnostics, deterministic repair and forensic recovery;
- [x] hierarchical manager dashboard;
- [x] application-wide GS frontend;
- [x] contextual errors;
- [x] live operation/LBA telemetry;
- [x] stable `HDDMAN.CFG` theme and guarded video-mode configuration;
- [x] guarded host physical-HDD fault injector;
- [x] lean startup with heavy diagnostics deferred until requested.

### Delivered recovery policy

- [x] first-`HDIOC_STATUS` damaged-master recovery entry;
- [x] one canonical master-field correction only when stale checksum corroborates the exact deterministic repair;
- [x] exact two-sector master write + flush + read-back + mandatory restart;
- [x] raw forensic scanning independent of normal `ps2hdd` admission;
- [x] forward, reverse and geometry candidate maps;
- [x] read-only shadow-map browsing;
- [x] `FORENSIC.TXT` export;
- [x] topology planning limited to `prev` / `next` / checksum;
- [x] one/two-bit link-distance classification;
- [x] source-stability check before every forensic write;
- [x] non-master-first / master-last commit ordering;
- [x] flush/read-back and final touched-set verification;
- [x] truncated scan => hard read-only at map, planner, UI and writer layers;
- [x] `DORMANT_FREE` classification for historical coalesced-free-space headers;
- [x] direct-grid garbage rejection learned from real large-HDD scans.

### Delivered UI / observability

- [x] Bootstrap / Diagnostics / Recovery / Backup & Storage / System hierarchy;
- [x] standard `UP/DOWN`, `X`, `TRIANGLE` navigation;
- [x] explicit full-row `LOCKED` states;
- [x] native 640x224 GS rendering and native 8x8 font raster;
- [x] `aqua`, `amber`, `sakura`, `mono` themes;
- [x] hardware-tested native/480p switching plus guarded experimental NTSC,
      PAL, 576p, 720p and 1080i outputs;
- [x] VBlank-synchronized status rendering;
- [x] coalesced high-rate raw READ presentation without throttling disk I/O to one operation per frame;
- [x] symbolic/stage-aware error explanations;
- [x] startup-phase timing in `HDDMAN.LOG`.

### Regression coverage

Portable CI includes:

- SHA-256/capsule/rescue integrity;
- KELF structural cases;
- boot-chain/report/payload fingerprint tests;
- bootstrap transaction fault injection;
- **30** deterministic mounted raw-HDD fixtures with matrix `4 no-repair / 6 header-repair / 8 pointer-clear / 12 blocked`;
- **9** sparse forensic raw-HDD E2E fixtures;
- exact one/two-bit stale-checksum topology recovery;
- overlap/conflict/missing-master write gates;
- healthy chains beyond the old 512-node limit;
- hard read-only truncation beyond current capacity;
- canonical empty-ID HDL subpartitions;
- direct-grid garbage rejection;
- dormant historical `__empty` coalescing;
- contextual error mapping;
- guarded hardware fault-injector self-test.

### Physical evidence at release

A healthy large-HDL physical disk produced the final release-validation result:

```text
Nodes        : 1621 / 2048
Dormant free : 8
Truncated    : no

forward map
confidence   : 100
nodes        : 1613
reciprocal   : 1612
inferred     : 0
conflicts    : 0
overlaps     : 0
patches      : 0
```

Physical testing also validated the final GS UI, fixed the initial long startup behavior, and confirmed VSync eliminated visible forensic-scan tearing.

### Maintenance / still experimental

The following are maintenance-validation work for 0.4.x, not reasons to delay 0.5 feature development:

- independent sacrificial-disk tests of direct master repair;
- independent one-bit/two-bit topology repair reports;
- multi-header physical recovery tests;
- power-loss/interruption characterization;
- wider adapter/HDD/SSD/controller/storage matrix.

Any bug found there should become a 0.4.x regression/fix if it does not require a new architectural feature.

# 0.5.x — Kakehashi

**One purpose: make recovery evidence portable between tools without moving write authority away from the PS2.**

Planned scope:

- version/freeze a machine-readable forensic evidence manifest;
- define stable disk/session identity fields;
- machine-readable candidate maps, confidence/evidence and proposed patches;
- documented v1 schemas for `HDDRAW`, `HDDMETA`, rescue capsules and forensic evidence;
- host reference parser/validator, preferably shared with or consumed by DriveForge;
- compatibility/migration rules for future artifact versions;
- golden/reference artifact corpus in CI;
- explicit capability/version negotiation for imported evidence;
- deterministic comparison of evidence bundles without requiring host write authority.

Not Kakehashi scope:

- another Windows GUI;
- generic PFS browsing/export;
- Dokany/FUSE mounting;
- HDL game management;
- host physical-drive write support;
- generic APA partition management.

Those belong to DriveForge or existing host tooling.

### Exit criteria

- every recovery artifact has a documented versioned schema;
- every artifact includes sufficient source identity to reject foreign/stale use;
- malformed/truncated/foreign-session artifacts fail closed;
- at least one independent host implementation parses the reference corpus;
- compatibility is exercised in CI;
- round trips are byte/digest reproducible where promised by the format.

# 0.6.x — Watari

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
PS2 independently rebuilds/revalidates safety preconditions
        ↓
user previews exact diff
        ↓
PS2 performs final write through existing recovery adapters
```

Requirements:

- imported plans are **suggestions**, never trusted write commands;
- disk identity and source-header digests must match;
- PS2 rebuilds expected resulting headers itself;
- PS2 recomputes local safety predicates rather than trusting host confidence claims;
- no arbitrary LBA/data write primitive is exposed by the plan format;
- stale/source-modified plans fail closed;
- existing `HDDRAW`/`HDDMETA` gates and master-last ordering remain authoritative.

### Exit criteria

- evidence -> host analysis -> plan -> PS2 preview works with zero writes;
- stale/foreign/source-modified plans are rejected;
- an imported deterministic repair produces the same post-state as the local equivalent;
- imported plans cannot escape the whitelisted recovery vocabulary.

# 0.7.x — Sekisho

**One purpose: make interrupted recovery a first-class recoverable state.**

Potential scope:

- versioned external transaction journal;
- transaction/evidence IDs and exact intended patches;
- durable phase boundaries before destructive stages;
- startup detection of incomplete manager-owned transactions;
- deterministic classification into safe resume / verify-only / exact rollback / manual investigation;
- rollback constrained to exact manager-captured source/post/partial states;
- fault injection at journal/write/flush/master/final-verify boundaries;
- freeze write-operation vocabulary intended for 1.0.

No rollback guesswork is allowed against an unrecognized on-disk state.

# 0.8.x — intentionally unassigned

Do not manufacture a feature train solely because `0.8` is numerically available. Assign it only if hardware/interoperability work exposes a coherent milestone not naturally belonging to Kakehashi, Watari, Sekisho or Tōge.

# 0.9.x — Tōge

Tōge is the **feature-frozen pre-1.0 stabilization line**.

Required work:

- broad console/ROMVER matrix;
- official and third-party network/SATA/IDE adapter matrix where applicable;
- multiple HDD/SSD/bridge capacities and vendors;
- memory-card/USB/controller fallback matrix;
- long-running forensic scans and repeated recovery cycles;
- fuzz/mutation corpus expansion from all prior hardware bugs;
- freeze or explicitly version-bump rescue/evidence/repair-plan formats;
- reproducible release artifacts and published hashes/provenance;
- external tester checklist that does not require reading source code;
- classify every write path as stable, experimental or removed before 1.0.

No large new subsystem should enter Tōge.

# 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires:

- stable rescue/recovery/interoperability contracts or documented migration;
- reproducible tagged builds with provenance and checksums;
- no known safety-critical write defects;
- documented interrupted-operation/rollback behavior;
- representative real-hardware matrix;
- host repair plans remain proposals and never bypass PS2-side safety policy;
- enough independent recovery testing that project QA is not one console and one developer.
