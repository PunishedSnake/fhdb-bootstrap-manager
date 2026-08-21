# Changelog

All notable changes to PS2 HDD Bootstrap Manager are documented here.

## [0.4.0] - 2026-08-21

**Codename: Michishirube (道標)**

Michishirube is the largest release so far. It turns the former bootstrap utility into a modular PS2-side recovery/forensic toolkit while preserving the established normal write contract.

### Architecture

- Split portable policy/format code, PS2 device adapters, application controllers, presentation/navigation and the `main.c` composition root into explicit layers.
- Moved backup, rescue, signing, diagnostics, deterministic recovery and forensic recovery out of the former monolithic application flow.
- Kept portable APA/recovery logic PS2SDK-free where practical so it can run under host CI.
- Added explicit controller ownership for normal bootstrap operations, diagnostics, deterministic health/recovery and degraded forensic recovery.
- Kept normal `hdd_write`, exceptional `hdd_repair_ps2`, and forensic `hdd_forensic_repair_ps2` as distinct write authorities rather than one generic raw writer.

### UI / usability

- Replaced the one-button-per-feature screen with a hierarchical dashboard: **Bootstrap**, **Diagnostics**, **Recovery**, **Backup & Storage**, and **System**.
- Standardized ordinary navigation on `UP/DOWN`, `X`, and `TRIANGLE`.
- Kept unavailable actions visible with an explanation and added an explicit full-row `LOCKED` visual state.
- Added an application-wide Graphics Synthesizer frontend using libdraw/GIF DMA instead of mixing the debug terminal with a separate status overlay.
- Added native 640x224 FIELD rendering, native 8x8 glyphs and a one-time PS2SDK MSX font atlas upload.
- Added four UI themes: `aqua`, `amber`, `sakura`, and `mono`.
- Added stable `HDDMAN.CFG` theme configuration beside the ELF when launch-path information is available; 0.4.x can still read the development-only legacy `MICHISHIRUBE.CFG` name.
- Added best-effort DualShock ANALOG-mode activity indication without LED strobing.
- Added contextual error presentation with symbolic ID, failing stage, summary, reason, recommendation and preserved raw numeric code.
- Added live operation/action/location/I/O/LBA/progress telemetry across startup, diagnostics, backup, install/restore/disable and recovery workflows.
- Added VBlank-synchronized status presentation and coalesced high-rate read updates. Physical testing confirmed that forensic-scan tearing disappeared without forcing one VBlank wait per raw disk read.

### Startup

- Removed the full boot-chain/PFS/MC diagnostics pass from the pre-dashboard startup path.
- Startup now performs only the safety-critical IOP/module/service/controller/HDD admission/master-validation sequence before entering the dashboard.
- Added per-phase startup timing to `HDDMAN.LOG`.

### Diagnostics / rescue

- Kept full `HDDRESCUE.BIN` / `HDDRESCUE2.BIN` capsule support with payload/header SHA-256 validation.
- Kept `BOOTCHAIN.TXT` and `HDDMAN.LOG` diagnostics and storage selection.
- Added lazy boot-chain evidence collection through the Diagnostics workflow rather than forcing it at every launch.
- Continued to prefer `MBR.XIN` with `MBR.XLF` compatibility fallback.

### Deterministic master recovery

- Added a guarded startup recovery entry before normal `HDIOC_STATUS` rejection.
- Added portable `apa_repair` policy for one narrowly reconstructable canonical master field.
- Require stale-checksum corroboration for deterministic automatic master correction.
- Added exact non-overwriting `HDDRAW.BIN` / `HDDRAW2.BIN` preservation before exceptional sectors 0-1 writes.
- Added PS2-only verified sectors 0-1 writer with canonical-master validation, GPT/protective rejection, flush, exact read-back and mandatory restart.
- Kept checksum-valid ambiguity, multiple unexplained changes and low-identity states fail-closed.

### Forensic APA reconstruction

- Added portable `apa_forensic` raw-evidence graph reconstruction independent of normal `ps2hdd` admission.
- Added coarse APA grid scanning and direct chasing of surviving `next`, `prev`, `main`, and `subs[]` references.
- Added forward-link, reverse-link and geometry candidate maps with explicit confidence, reciprocal/inferred links, conflict and overlap counts.
- Added read-only shadow-map browsing that never spoofs normal writable APA health.
- Added `FORENSIC.TXT` export of discovered headers, map state, confidence/evidence and proposed topology changes.
- Added topology repair planning limited to `prev`, `next`, and checksum.
- Added per-patch bit-distance classification and stale-checksum corroboration for one-bit and exact two-bit link damage.
- Added `HDDMETA.BIN` / `HDDMETA2.BIN` versioned pre-repair snapshots containing every touched original 1024-byte header, per-header SHA-256 and a whole-image digest.
- Added source-stability reread immediately before every metadata write.
- Added non-master-first / master-LBA-0-last commit ordering, flush/read-back after every header and final full touched-set verification.
- Added mandatory restart after successful or partially failed exceptional topology recovery.

### Large-disk hardware findings

Physical testing on a healthy large HDL-heavy disk directly changed the forensic policy before release.

- Increased forensic candidate capacity from 512 to 2048 nodes.
- Made **every truncated scan strictly read-only** in map policy, repair-plan construction, UI authorization and the raw PS2 writer.
- Added regressions for a healthy 768-header chain and a 2049-header capacity/truncation case.
- Tightened direct-grid admission so random game-data sectors cannot become confidence-30 APA candidates merely from a few accidentally plausible fields.
- Increased `FORENSIC.TXT` report capacity from 64 KiB to 512 KiB.
- Added `DORMANT_FREE` classification for checksum-valid historical `__empty` headers wholly covered by a later canonical active free extent.
- Retained dormant free-space records as evidence while excluding them from active-map confidence/geometry competition.
- Added a dedicated dormant-free coalescing regression.

The final healthy physical report used for release validation produced:

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

### Regression laboratory

- Expanded the deterministic mounted raw-HDD laboratory to **30** sparse fixtures.
- Current mounted repair matrix remains **4 no-repair / 6 guarded header-repair / 8 pointer-clear / 12 blocked**.
- Added **9** sparse forensic raw-HDD E2E fixtures at realistic APA LBAs.
- Added one-bit and exact two-bit stale-checksum topology recovery tests.
- Added checksum-valid wrong-link manual-only, overlap, conflict, missing-master and multi-header damage cases.
- Added large-chain, hard-truncation, empty-ID HDL subpartition, grid-garbage and dormant-free regressions from real hardware findings.
- Added normal payload-first/pointer-last mutation testing.
- Added contextual error-catalog regression tests.
- Added guarded hardware fault-injector self-test for mutation/read-back/restoration.
- Full R5900 builds remain `-Wall -Wextra -Werror` clean under pinned PS2DEV v2.0.0.

### Safety / release status

- Normal bootstrap workflows retain verified backup, explicit confirmation, reserved-`__mbr` payload bounds, payload-first/pointer-last ordering, `HDIOC_SETOSDMBR`, flush and read-back.
- Normal install/restore/disable paths do **not** raw-write sectors 0-1.
- Exceptional raw metadata recovery is deliberately isolated from normal writes.
- Truncated forensic evidence never authorizes a write.
- Every forensic metadata write requires preserved original bytes and a source-stability check immediately before mutation.
- A healthy map with zero patches performs no write even if the candidate otherwise satisfies structural eligibility.

**Important:** the read-only diagnostics/forensics, UI, backups and normal workflows have substantial real-console validation. Direct sectors 0-1 repair and forensic multi-header metadata repair remain **experimental** in 0.4.0 and need independent testing on sacrificial or fully imaged media.

## [0.3.1] - 2026-08-21

**Codename: Torii (鳥居)**

### Changed

- Made `MBR.XIN` the preferred manual MBR source name.
- Retained `MBR.XLF` as a compatibility fallback.
- If both are present, `MBR.XIN` wins; a broken preferred source is not silently hidden behind the fallback.
- Kept all existing APA/rescue/signing/write safety behavior unchanged.

### Credit

- Thanks to **Berion** on PSX-Place for the `MBR.XIN` naming correction and historical `MBR.XLF` context.

## [0.3.0] - 2026-08-21

**Codename: Torii (鳥居)**

### Added

- Versioned `HDDRESCUE.BIN` / `HDDRESCUE2.BIN` capsules containing master header and exact active bootstrap sectors.
- SHA-256 for embedded header, sector-aligned payload and unpadded KELF fingerprint.
- Payload-first full rescue restoration.
- Read-only boot-chain diagnostics, ROMVER/region detection, FMCB HDD-skip/module inspection, `__sysconf`/`__system` evidence and characteristic FHDB/PSBBN/HDD-OSD classification.
- `BOOTCHAIN.TXT` and append-only `HDDMAN.LOG` on selectable `mc0:`, `mc1:` or `mass:`.
- Portable SHA-256/capsule tests and pinned reproducible CI/release builds.

### Changed

- Reorganized maintained sources under `src/`, headers under `include/`, and technical documentation under `docs/`.
- Centralized version/codename in `include/version.h`.
- Stopped tracking a prebuilt ELF in source control.

## [0.2.0] - 2026-08-19

### Changed

- Promoted the hardware-tested 0.2 release candidate to the first full PS2 HDD Bootstrap Manager release.

### Hardware validation

- Verified standalone backup/read-back and the complete normal manager workflow on real PS2 hardware.

## [0.2.0-rc2] - 2026-08-19

### Added

- Standalone current-master backup action available regardless of bootstrap pointer state.
- Four non-destructive backup slots across both memory-card ports.

## [0.2.0-rc1] - 2026-08-19

### Changed

- Renamed the application from FHDB Bootstrap Manager to PS2 HDD Bootstrap Manager.
- Replaced automatic backup target selection with explicit `mc0`, `mc1`, or `mass` storage selection.
- Changed new backup names to `HDDMBR.BIN` / `HDDMBR2.BIN` while retaining legacy restore compatibility.

### Added

- Embedded BDM/FatFs USB support.
- Stock MBR KELF validation and 4 MiB safety limit.
- Console-side MagicGate signing.
- Manual MBR bootstrap installation to reserved `__mbr` space beginning at sector `0x2000`.
- Payload write/flush/read-back verification followed by pointer-last activation.
- Power/restart/return system menu.

## [0.1.1] - 2026-08-19

### Fixed

- Replaced POSIX `O_*` flags passed to fileXio with the required IOP `FIO_O_*` flags.
- Fixed real-hardware memory-card backup verification.
- Removed invalid use of `O_EXCL` against the ROM memory-card driver.

### Confirmed

- Real hardware successfully cleared `osdStart`/`osdSize`, recalculated a valid APA checksum through `ps2hdd`, and eliminated the stale FHDB boot loop without disconnecting or formatting the HDD.

## [0.1.0] - 2026-08-19

### Added

- Initial APA master validation.
- Full 1024-byte memory-card backup/restore workflow.
- Same-disk backup matching.
- Hybrid APA/GPT rejection.
- Confirmation chords and post-write read-back verification.

### Known issue

- Used POSIX file flags with direct fileXio calls, causing backup read-back failure on hardware. The safety gate worked correctly and blocked HDD mutation when verification failed.
