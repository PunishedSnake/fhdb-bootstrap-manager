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
- [x] `storage` — storage targets, encapsulated selected-target state, launch-device selection, ROMVER/file helpers, exact/bounded reads, and generic writes.
- [x] `apa` portable core — endian parsing, checksum validation, normal `__mbr` recognition, hybrid-GPT detection, and same-disk header matching; covered by synthetic host tests.
- [x] `hdd_read` read-only transport — `HDIOC_READSECTOR`, the conservative aligned two-sector read buffer, live `hdd0:__mbr` payload bounds checks, and sector-aligned active-payload acquisition are isolated behind a PS2-only interface that exposes no write, flush, or pointer-update operation.
- [ ] `apa` write-capable transport half — `HDIOC_WRITESECTOR`, `HDIOC_SETOSDMBR`, flush/read-back verification, write-side DMA buffers, and transaction ordering remain in `main.c` until the read-only boundary has wider regression and hardware coverage.
- [x] `boot_chain` portable core — shared evidence model, CNF parsing, `Skip_HDD`, ROMVER region mapping, OSDMenu/FHDB target parsing, and deterministic family classification.
- [x] `boot_chain` PS2 read-only scanner — memory-card HDD modules, FMCB settings, `__sysconf`, `__system`, OSDMenu, PSBBN, HOSDMenu, and HDD-OSD evidence collection.
- [x] `kelf` portable core — endian-safe KELF structural validation and recovery of the unpadded file size from a sector-aligned HDD image, with named stable result codes.
- [x] `boot_payload` portable fingerprinting — sector-image SHA-256, KELF structure/size conversion, and unpadded-KELF SHA-256 without PS2SDK, allocation, or I/O.
- [x] `boot_payload_ps2` active-payload acquisition — combines the read-only HDD transport with portable payload fingerprinting and fills only payload-derived boot-chain evidence.
- [x] `boot_report` portable renderer — bounded `BOOTCHAIN.TXT` formatting, assessment text, hashes, and evidence presentation with no device or persistence dependency.
- [x] `boot_report_ps2` persistence — storage-path construction, complete `BOOTCHAIN.TXT` replacement, and the existing USB write grace period are outside `main.c` while the portable renderer stays storage-free.
- [x] `session_log` persistence — bounded ordered session buffering, append-only `HDDMAN.LOG`, 128 KiB rotation, per-storage unsaved offsets, and USB retry behavior are outside `main.c`.
- [x] `boot_diagnostics_ps2` orchestration — ROMVER initialization, active-payload evidence, FMCB/PFS evidence collection, and final family classification are combined outside `main.c` behind a read-only PS2-specific entry point.
- [x] `boot_report_session` state — latest rendered report bytes, length, and last persistence result are outside `main.c`, while rendering and device I/O remain delegated to their existing narrow modules.
- [ ] diagnostics presentation/UI — `main.c` still decides when to scan/render/persist and owns the short diagnostics screen itself.
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
- FHDB, HOSDMenu, PSBBN, HDD-OSD, and unknown-KELF fallback classifications;
- valid low/high KELF header layouts, the optional length-prefixed section, and the 63-entry BIT-table boundary;
- plain ELF, truncated/impossible header, BIT overflow, missing variable/key area, and malformed sector-image rejection;
- recovery of the exact unpadded KELF size from a sector-aligned payload image;
- portable payload fingerprinting for a valid sector-padded KELF, an invalid KELF whose full sector-image hash must still be retained, and empty-input field reset;
- a complete byte-for-byte golden `BOOTCHAIN.TXT` fixture for a disabled bootstrap;
- active payload report formatting including sector-image/KELF SHA-256 fingerprints, OSDMenu evidence, and memory-card HDD modules;
- report assessment precedence for inconsistent pointers, unreadable payloads, invalid KELFs, and unknown downstream environments;
- bounded report truncation with guaranteed NUL termination and the external-HDD-module/`Skip_HDD` advisory note.

### Remaining engineering work

- continue replacing project-specific magic negative result numbers with documented enums/domains where PS2SDK errors are not being forwarded directly; KELF and read-only payload-bound results are now named without changing their historical numeric values;
- reduce the remaining diagnostics presentation/UI state without moving classification policy, evidence acquisition, or storage operations back into `main.c`;
- only after the read-only boundary and diagnostics split have enough regression/hardware confidence, extract the write-capable APA transport without changing payload-first/pointer-last semantics;
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
