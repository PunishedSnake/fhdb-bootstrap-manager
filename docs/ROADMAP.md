# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. The theme fits a tool whose entire job is deciding what the PS2 crosses into after ROM boot. Semantic versions remain the authoritative machine-readable release identifiers.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and the first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, and guarded installation. |
| `0.3.x` | **Torii** (鳥居) | gateway | Stable full rescue capsules, payload restoration, boot-chain inspection, persistent diagnostics, CI, targeted R5900 hashing optimizations, and compatible MBR.XIN/MBR.XLF source handling. |
| `0.4.x` | **Michishirube** (道標) | signpost | Internal modularization and stronger diagnostic/test infrastructure. |
| `0.5.x` | **Kakehashi** (架け橋) | bridge | Broader interoperability and portable tooling around rescue/inspection data. |
| `1.0.0` | **Kaidō** (街道) | main road | Frozen rescue-format contract, reproducible releases, and broad real-hardware validation. |

## 0.4.x — Michishirube

Michishirube is developed as a sequence of regression-gated extractions. A checked item means the responsibility has physically left `main.c`; it does not imply that its public API is already final.

### Modularization progress

- [x] `platform` — IOP reset, embedded IRX startup, pad DMA lifetime, button-edge input, and confirmation chords.
- [x] `storage` first pass — storage targets, launch-device selection, ROMVER/file helpers, exact/bounded reads, and generic writes.
- [ ] Encapsulate storage selection state after the mechanical split has hardware coverage.
- [x] `apa` portable core — endian parsing, checksum validation, normal `__mbr` recognition, hybrid-GPT detection, and same-disk header matching; covered by synthetic host tests.
- [ ] `apa` transport/write half — raw sector transfer boundary, payload bounds, driver-mediated `osdStart`/`osdSize` updates, flush/read-back verification, and DMA buffers. This remains in `main.c` until the read-only core has a wider regression baseline.
- [x] `boot_chain` portable core — shared evidence model, CNF parsing, `Skip_HDD`, ROMVER region mapping, OSDMenu/FHDB target parsing, and deterministic family classification.
- [x] `boot_chain` PS2 read-only scanner — memory-card HDD modules, FMCB settings, `__sysconf`, `__system`, OSDMenu, PSBBN, HOSDMenu, and HDD-OSD evidence collection.
- [ ] `boot_chain` orchestration/report split — active payload fingerprinting currently crosses the raw-HDD transport boundary, while report rendering/logging still belongs to `main.c`.
- [ ] `rescue` — capsule creation/lookup/validation plus restore orchestration while preserving payload-first/pointer-last semantics.
- [ ] `ui` — menus, fatal/info screens, logging presentation, and confirmation text after core policy has explicit interfaces.

### Test coverage now in place

Portable CI now exercises:

- SHA-256 streaming and complete-block paths;
- rescue capsule round-trip and malformed metadata rejection;
- synthetic APA header/checksum/hybrid-GPT/same-disk cases;
- CNF parsing with comments, CRLF, whitespace, exact-key matching, and bounded output;
- current and legacy `Skip_HDD` spellings plus conflicting-key precedence;
- OSDMenu `$PSBBN`, `$HOSDSYS`, custom and absent `boot_auto` values;
- FHDB `LK_Auto_E1`/`E2`/`E3` precedence;
- ROMVER region-to-system-folder mapping;
- boot-chain classification for invalid/disabled/unreadable/invalid-KELF states;
- explicit OSDMenu targets overriding deliberately conflicting stale filesystem evidence;
- FHDB, HOSDMenu, PSBBN, HDD-OSD, and unknown-KELF fallback classifications.

### Remaining engineering work

- replace project-specific magic negative result numbers with documented enums/domains where PS2SDK errors are not being forwarded directly;
- move KELF structural parsing into a portable module and add malformed/truncated KELF fixtures;
- add build-size/performance reporting so optimization work is measurable rather than flag-driven;
- establish a hardware validation matrix across FAT console revisions, storage adapters/HDDs, memory-card layouts, and launch devices;
- make classification evidence more data-driven so supporting another known environment does not require threading another special case through the UI.

## 0.5.x — Kakehashi

Candidate work after the internal split is proven:

- a small host-side rescue-capsule inspector/extractor that verifies `HDDRESCUE*.BIN` without a PS2;
- import/export conveniences for reports and rescue metadata without weakening same-disk restore checks;
- richer compatibility evidence for FHDB, OSDMenu, PSBBN, HDD-OSD/HOSDMenu, and custom chains;
- optional machine-readable diagnostic output alongside the human-readable report.

## 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires a stable rescue-format contract, reproducible tagged builds, no known safety-critical write-path defects, documented interrupted-operation recovery behavior, and enough independent hardware validation that the project is no longer relying on one console as its entire quality-assurance department.
