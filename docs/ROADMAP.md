# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. Semantic versions remain the authoritative machine-readable identifiers.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, guarded installation. |
| `0.3.x` | **Torii** (鳥居) | gateway | Stable full rescue capsules, payload restoration, boot-chain diagnostics, CI, compatible MBR.XIN/XLF handling. |
| `0.4.x` | **Michishirube** (道標) | signpost | Modular architecture, regression laboratory, guarded metadata recovery, and stronger diagnostics. |
| `0.5.x` | **Kakehashi** (架け橋) | bridge | Broader interoperability and portable/forensic tooling around rescue and APA inspection data. |
| `1.0.0` | **Kaidō** (街道) | main road | Frozen recovery/rescue contracts, reproducible releases, and broad real-hardware validation. |

## 0.4.x — Michishirube

Michishirube is developed as regression-gated extractions. A checked item means the responsibility has physically left the monolithic application path and has an explicit interface; it does not mean every public API is frozen.

### Modularization progress

- [x] `platform` — IOP/module startup, pad DMA lifetime, button-edge input, confirmation chords.
- [x] `storage` — selected-target state, launch-device selection, ROMVER/file helpers.
- [x] `header_backup` — mandatory non-overwriting normal-operation master backup and exact read-back verification.
- [x] `rescue_image` / `rescue_storage` — portable rescue validation plus PS2 file/payload lifecycle.
- [x] `bootstrap_source` — MBR.XIN/XLF-compatible source preparation and live capacity validation.
- [x] `bootstrap_signing` — MagicGate mutation and post-sign KELF validation with no HDD transaction authority.
- [x] `apa` — portable APA parsing/checksum/identity/hybrid guard.
- [x] `apa_repair` — portable fail-closed planner for narrowly reconstructable APA master defects.
- [x] `repair_health` — portable mounted-disk policy for pointer/payload health and pointer-clear recommendation.
- [x] `hdd_bounds` — portable payload-pointer/geometry rules and stable error ordering.
- [x] `hdd_read` — PS2-only read transport with no write/flush/pointer API.
- [x] `hdd_write` — normal payload write/flush/read-back and `HDIOC_SETOSDMBR` mechanics.
- [x] `hdd_repair_ps2` — separately gated exceptional raw sectors 0-1 writer for approved master recovery only.
- [x] `repair_snapshot` — exact `HDDRAW*.BIN` preservation before exceptional raw master repair.
- [x] `bootstrap_transaction` + PS2 adapter — portable payload-first/pointer-last commit sequence and failure injection.
- [x] `boot_chain` / `boot_chain_ps2` — portable classification plus read-only PS2 evidence acquisition.
- [x] `boot_payload` / `boot_payload_ps2` — portable KELF/image fingerprinting plus read-only acquisition adapter.
- [x] `boot_diagnostics_ps2` — read-only evidence orchestration outside the application root.
- [x] `boot_report` / `boot_report_ps2` / `boot_report_session` — rendering, persistence, and current report state split.
- [x] `session_log` — bounded ordered `HDDMAN.LOG` persistence.
- [x] `kelf`, `sha256`, `capsule_format`, `mbr_compat` — format/security-support responsibilities isolated.
- [x] `bootstrap_controller_ps2` — high-level backup/disable/restore/install authorization and user-facing error flow moved out of `main.c`.
- [x] `diagnostics_controller_ps2` — scan timing/report refresh and diagnostics screen moved out of `main.c`.
- [x] `repair_controller_ps2` — startup/health recovery presentation separated from portable `apa_repair` / `repair_health` policy.
- [x] `app_ui_ps2` — shared fatal/info/power/storage/signing-card presentation and lifecycle helpers.
- [x] `main.c` composition root — now limited to initialization, normal APA admission, top-level menu state, and dispatch.
- [ ] UI polish / controller API cleanup — reduce remaining presentation duplication inside individual controllers without moving format or transport policy back into UI.

### Guarded recovery progress

- [x] first-`HDIOC_STATUS` recovery entry point for a raw-readable master rejected by normal APA admission;
- [x] exact raw 1024-byte `HDDRAW*.BIN` snapshot before sectors 0-1 recovery write;
- [x] single canonical-field master repair only when stale checksum corroborates the exact correction;
- [x] additive-checksum collision regression; checksum-valid ambiguous corruption remains blocked;
- [x] GPT/protective-MBR, low-identity, checksum-only, and multi/unknown corruption fail closed;
- [x] exact two-sector raw write + flush + read-back + compare + mandatory restart;
- [x] mounted `L2` structure-health UI routing pointer/payload anomalies through the safer normal backup + pointer-clear workflow;
- [ ] physical-HDD validation of the exceptional raw master-repair path.

### Test coverage now in place

Portable CI covers, among other format/diagnostic suites:

- SHA-256/capsule/rescue integrity and stale/foreign rescue identity;
- KELF structural edge cases and sector-padding recovery;
- boot-chain parsing/classification, payload fingerprinting, and bounded report rendering;
- bootstrap transaction success and injected payload/pointer failures;
- **30 deterministic sparse raw-HDD fixtures** covering valid enabled/disabled states, interrupted writes, invalid payloads, inconsistent/out-of-range pointers, checksum-only corruption, checksum-valid noncanonical fields, physical-style stale-checksum bit flips, additive-checksum collision, torn header, PC signature, hybrid APA/GPT, GPT-only, and deterministic garbage;
- byte-level normal MBR payload overwrite and `HDIOC_SETOSDMBR` pointer semantics;
- all **30** fixture states through repair policy with postconditions: `4 no-repair / 6 header-repair / 8 pointer-clear / 12 blocked`;
- portable `repair_health` as the actual mounted-disk recommendation layer used by the PS2 repair screen.

Generated images remain deterministic build products under `tests/generated_hdds/`; opaque binary fixtures are not committed.

### Remaining 0.4 engineering work

- hardware-validate normal modular boundaries and the exceptional sectors 0-1 recovery path on real HDDs/adapters;
- keep extending the synthetic suite whenever a parser/policy/recovery bug is found;
- continue replacing project-specific magic negative codes with named result domains where this does not obscure forwarded PS2SDK errors;
- add build-size/performance reporting before adopting any larger raw transfer, LTO, or speculative compiler optimization;
- establish a wider hardware validation matrix across FAT revisions, adapters/HDDs, launch devices, and memory-card layouts;
- decide how much **forensic/degraded read-only APA reconstruction** belongs in 0.4 versus the interoperability-focused 0.5 line.

## Forensic / degraded recovery direction

A disk rejected by normal `ps2hdd` may still be raw-readable. Future recovery should exploit that fact without pretending uncertain metadata is healthy.

Planned stages:

1. raw-scan candidate APA headers across plausible partition boundaries;
2. build forward/backward candidate graphs from `next` / `prev`, `start`, `length`, type/flags, main/sub references, alignment, bounds, and overlap constraints;
3. score multiple plausible maps using independent evidence rather than checksum alone;
4. expose sufficiently consistent candidates **read-only** for listing, reporting, dumping, and—where feasible—read-only filesystem inspection;
5. present ambiguous map variants explicitly in the UI instead of silently choosing one;
6. only after exact preview + snapshot of every touched header allow a separately authorized reconstruction write;
7. raw-rescan and require the repaired graph to become internally consistent before returning to normal writable `ps2hdd` workflows.

The trust ladder is therefore:

`raw readable -> forensic candidate map -> read-only validated map -> repair candidate -> repaired/raw-revalidated -> normal ps2hdd admitted`.

This model can potentially recover from multiple bit flips or broken link fields when neighboring headers provide enough redundant constraints. It cannot recreate lost partition/file contents whose sectors themselves are gone.

## 0.5.x — Kakehashi

Candidate work after the 0.4 internal boundaries and recovery contracts are hardware-proven:

- host-side rescue-capsule inspector/extractor;
- portable APA/raw-image inspection and forensic graph reconstruction;
- import/export of machine-readable diagnostic/reconstruction reports;
- safer bulk extraction from read-only reconstructed partition maps;
- richer compatibility evidence for FHDB, OSDMenu, PSBBN, HDD-OSD/HOSDMenu, and custom chains.

## 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires stable rescue/recovery contracts, reproducible tagged builds, no known safety-critical write-path defect, documented interrupted-operation behavior, and enough independent physical-HDD validation that the project is no longer relying on one console as its entire quality-assurance department.
