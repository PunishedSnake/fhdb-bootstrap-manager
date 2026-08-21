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
- [x] `header_backup` safety storage — mandatory non-overwriting header backup/reuse, exact read-back verification, legacy same-disk lookup, and per-slot diagnostics are outside `main.c`; the application still fails closed when no safe slot exists.
- [x] `rescue_image` portable validation — complete rescue-image metadata/hash/APA/KELF consistency and protected-slot state matching are host-testable without PS2SDK.
- [x] `rescue_storage` lifecycle — protected rescue slots, active-payload acquisition, USB/file I/O, save/read-back verification, same-disk lookup, and damaged-capsule fallback policy are outside `main.c`.
- [x] `bootstrap_source` installation preparation — MBR.XIN/XLF-compatible loading, size bound, unsigned-KELF validation, sector count, and live `__mbr` capacity validation are outside `main.c` and cannot sign or write the HDD.
- [x] `bootstrap_signing` security adapter — `SecrInit`, console-side `SecrDownloadFile`, and post-sign KELF validation are isolated from card-selection UI and HDD transaction code.
- [x] `apa` portable core — endian parsing, checksum validation, normal `__mbr` recognition, hybrid-GPT detection, and same-disk header matching; covered by synthetic host tests.
- [x] `hdd_read` read-only transport — `HDIOC_READSECTOR`, the conservative aligned two-sector read buffer, live `hdd0:__mbr` payload bounds checks, and sector-aligned active-payload acquisition are isolated behind a PS2-only interface that exposes no write, flush, or pointer-update operation.
- [x] `hdd_write` write-capable transport — `HDIOC_WRITESECTOR`, `HDIOC_SETOSDMBR`, write-side DMA/read-back buffers, flushes, payload byte comparison, and final pointer verification are isolated behind a PS2-only interface.
- [x] `bootstrap_transaction` commit policy — portable failure-injected tests cover payload/release/pointer/verify ordering, and the PS2 adapter binds that policy to `hdd_write` without owning pre-write authorization.
- [ ] higher-level write workflow/UI split — `main.c` still owns operation selection, the fail-closed decision around subsystem results, confirmation screens/chords, signing-card selection, and operation-specific error presentation; the underlying rescue/source/signing/transport/transaction mechanics are now modular.
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
- [ ] `ui` — menus, power/fatal/info screens, storage/signing-card choice, logging presentation, and confirmation text after core policy has explicit interfaces.

### Test coverage now in place

Portable CI now exercises:

- SHA-256 streaming and complete-block paths;
- rescue capsule metadata round-trip and malformed metadata rejection;
- complete rescue-image validation including APA/payload hash corruption, header-only images, and protected-slot state identity;
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
- bootstrap transaction success plus injected payload, pointer-set, and pointer-verify failures, including proof that payload allocation release precedes pointer exposure and that payload failure stops the transaction;
- bounded report truncation with guaranteed NUL termination and the external-HDD-module/`Skip_HDD` advisory note.

### Remaining engineering work

- continue replacing project-specific magic negative result numbers with documented enums/domains where PS2SDK errors are not being forwarded directly; KELF, read-only payload-bound, rescue-image, rescue-storage, and source-preparation result domains are now named where useful without changing historical diagnostics;
- reduce the remaining UI/controller code in `main.c` now that storage formats, rescue lifecycle, source preparation, signing mechanics, raw write transport, and commit ordering have explicit interfaces;
- hardware-validate the combined `header_backup` + `rescue_storage` + `bootstrap_source` + `bootstrap_signing` + `hdd_write` + `bootstrap_transaction` boundaries on a real HDD before treating the refactor as behavior-equivalent to Torii;
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
