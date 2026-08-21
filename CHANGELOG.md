# Changelog

All notable changes to PS2 HDD Bootstrap Manager are documented here.

## [0.4.0-dev] - unreleased

**Codename: Michishirube (道標)**

### Changed

- Completed the regression-gated split of the former monolithic EE application into portable policy/format modules, narrow PS2 device adapters, application controllers, shared presentation/navigation helpers, and a small `main.c` composition root.
- `main.c` now owns startup, the normal APA admission gate, initial diagnostics, and hand-off to the manager dashboard rather than backup/rescue/signing/write/recovery workflows.
- Replaced the former one-button-per-feature main screen with a hierarchical dashboard: **Bootstrap**, **Diagnostics**, **Recovery**, **Backup & Storage**, and **System**.
- Standardized ordinary navigation on `UP/DOWN`, `X`, and `TRIANGLE`; unavailable operations remain visible with a reason instead of disappearing.
- Kept destructive confirmation chords separate from menu navigation so new features no longer consume additional global controller shortcuts.
- Added `manager_menu_ps2` for top-level navigation and state-dependent feature availability.
- Added `forensic_controller_ps2` for raw scan, candidate-map browsing, report export, repair-plan preview, forensic snapshot gating, and topology-repair authorization.
- Added best-effort controller activity indication through `padSetMainMode()`: ANALOG lamp is requested ON during nested manager activity and OFF while idle instead of being strobed. Unsupported pads fall back to screen-only progress.
- Updated README, architecture, roadmap, comments, and recovery documentation to distinguish normal writes, deterministic single-master recovery, and broader forensic topology recovery.

### Added

- Guarded startup APA master recovery before normal `HDIOC_STATUS` rejection through `hdd_recovery_wrap`.
- Portable fail-closed `apa_repair` planning for one independently known canonical master field: `APA\0`, `__mbr`, Sony MBR marker, master `start`, MBR type, or MBR version.
- Mandatory non-overwriting exact `HDDRAW*.BIN` snapshot/read-back before exceptional raw master writes.
- Narrow `hdd_repair_ps2` sectors 0-1 writer that accepts only a completed standard canonical non-hybrid APA master, flushes, reads both sectors back, and requires exact comparison.
- Portable `apa_forensic` raw-evidence graph engine.
- Coarse standard APA grid scanning plus direct chasing of surviving `next`, `prev`, `main`, and `subs[]` references, including off-grid subpartitions.
- Up to three explicit candidate maps: forward links, reverse links, and geometry order.
- Per-node and per-map confidence/evidence including reciprocal links, inferred links, conflicts, overlaps, checksum state, geometry, type, flags, and subpartition references.
- Read-only **shadow APA** browsing that does not spoof `ps2hdd` into treating uncertain metadata as healthy.
- Human-readable `FORENSIC.TXT` export of discovered headers, candidate maps, confidence, and proposed topology changes.
- Portable topology repair planning limited to reconstructable `prev`, `next`, and checksum fields.
- Per-patch bit-distance tracking and explicit one/two-bit correction statistics.
- An exact two-bit physical-style link corruption regression in which neighboring graph evidence reconstructs the correct value, bit distance must equal 2, and the stale checksum must corroborate the exact repair.
- Versioned `HDDMETA.BIN` / `HDDMETA2.BIN` forensic snapshots containing every exact pre-repair 1024-byte header touched by a topology plan, per-header SHA-256, plan metadata, and a complete snapshot digest.
- `hdd_forensic_repair_ps2` multi-header writer with source-stability checks, non-master-first/master-last ordering, flush/read-back after each header, and final full touched-header verification.
- Stronger expert confirmation for high-confidence topology plans containing at least one heuristic-only change.
- Mandatory restart after successful or partially failed forensic topology writes before any further HDD operation.

### Tests

- Expanded the deterministic sparse raw-HDD laboratory to **30** 16 MiB logical images.
- Added physical-style stale-checksum bit flips for APA identity/master anchor fields, checksum-valid but semantically noncanonical variants, checksum-only corruption, torn-header states, interrupted payload states, and an explicit additive-checksum collision regression.
- Added byte-level normal MBR payload overwrite and `osdStart`/`osdSize` enable/disable mutation tests, including proof that the unrelated PC `BootIndicator` remains untouched.
- All 30 raw images run through parser/bounds/KELF expectations and through mounted repair policy with postconditions.
- Current mounted repair-matrix contract remains **4 no-repair / 6 guarded header-repair / 8 pointer-clear / 12 blocked**.
- Added portable forensic graph tests for a healthy chain, stale-checksum broken link, checksummed wrong-link manual-only classification, off-grid referenced subpartition discovery, and the missing-master write gate.
- Added exact two-bit link-corruption coverage requiring correct graph reconstruction, `bit_distance == 2`, checksum corroboration, and automatic-safe classification.
- Existing rescue, KELF, boot-chain, report, payload fingerprint, SHA-256/capsule, bootstrap-transaction, HDD fixture, and mutation suites remain in the normal `make test-host` gate.
- Full R5900 build remains warning-clean under the pinned PS2DEV v2.0.0 toolchain after the forensic/UI/activity additions.

### Safety

- APA's additive checksum is treated as supporting evidence rather than collision-resistant integrity. A checksum-valid suspicious structure is never considered healthy merely because the sum matches.
- Deterministic single-master automatic repair still requires a stale checksum whose old value is restored by exactly one planner-approved canonical correction.
- Forensic read-only reconstruction is deliberately more permissive than forensic write authorization.
- A candidate map exists in RAM only and is not silently injected into normal writable `ps2hdd`/PFS paths.
- A forensic topology plan may currently change only `prev`, `next`, and checksum; IDs, lengths, timestamps, passwords, filesystem metadata, and lost contents are not invented.
- Before any topology write, every touched original header must be preserved in a verified `HDDMETA` snapshot.
- Immediately before each header write, the raw source must still match the exact bytes seen by the scan; changed media/state aborts the operation.
- Non-master headers are committed before the LBA-0 master, mirroring the project's payload-first/pointer-last principle.
- Every metadata write is flushed and read back; the complete touched set is verified again at the end.
- Normal install/restore/disable workflows retain their original verified backup, confirmation, payload-first/pointer-last, `HDIOC_SETOSDMBR`, and read-back contract.
- `hdd_read` remains read-only; normal `hdd_write`, deterministic `hdd_repair_ps2`, and forensic `hdd_forensic_repair_ps2` remain separate write authorities.

### Remaining validation

- Physical-HDD validation is still required for the exceptional sectors 0-1 master-repair path.
- Full-disk forensic scan speed and raw-read behavior must be measured on real HDDs/adapters of several capacities.
- Multi-header topology repair, source-stability detection, master-last ordering, and partial-failure restart behavior still require controlled physical tests.
- `HDDMETA`, `HDDRAW`, rescue, report, and log behavior must be checked across `mc0`, `mc1`, and USB under slow/full/pre-existing-slot conditions.
- ANALOG-lamp activity mode must be tested on original DualShock 2 and representative third-party pads.
- Host CI cannot prove DEV9/ATA timing, DMA/fileXio behavior, cache durability, APA journaling, adapter quirks, or exact physical power-loss effects.

## [0.3.1] - 2026-08-21

**Codename: Torii (鳥居)**

### Changed

- `MBR.XIN` is now the preferred source filename for manual HDD bootstrap installation, matching the Sony naming pointed out by Berion on PSX-Place.
- `MBR.XLF` remains supported as a compatibility fallback for existing community installer layouts; no payload conversion or on-disk format change is performed.
- When both names are available, the compatibility shim opens `MBR.XIN`. If that preferred file exists but cannot be opened or fails the existing KELF validation, the operation fails instead of silently hiding it behind `MBR.XLF`.
- The selected path buffer is updated to `MBR.XIN` after selection so subsequent source diagnostics identify the payload actually being installed.

### Safety

- No APA write-path, rescue-capsule format, signing, payload verification, or pointer-last activation logic changed in this patch.
- The compatibility behavior is isolated in `src/mbr_compat.c` and linked through GNU ld `--wrap=fileXioOpen`, keeping the hardware-validated 0.3.0 installation state machine unchanged.

### Credit

- Thanks to **Berion** on PSX-Place for catching the `MBR.XIN` naming detail and explaining the historical `MBR.XLF` installer convention.

## [0.3.0] - 2026-08-21

**Codename: Torii (鳥居)**

### Added

- Added versioned `HDDRESCUE.BIN` / `HDDRESCUE2.BIN` capsules containing the complete APA master header and exact active bootstrap sectors.
- Added SHA-256 verification for both the embedded header and sector-aligned payload image, plus a separate fingerprint of the unpadded KELF.
- Added full rescue restoration that writes and compares the payload before enabling its saved pointer.
- Added a read-only boot-chain inspector available with `R1`.
- Added probable FHDB, PSBBN/OSDMenu, HOSDMenu/HDD-OSD, custom OSDMenu, invalid-KELF, and unknown-payload classification with explicit confidence labels.
- Added ROMVER and regional FMCB-folder detection.
- Added scans for `OSDSYS_Skip_HDD`/legacy `Skip_HDD` and exact `hddload.irx`, `dev9.irx`, and `atad.irx` locations across both memory cards and all known regional folders.
- Added read-only inspection of `__sysconf`, `__system`, OSDMenu `boot_auto`, downstream executables, and characteristic PSBBN partitions.
- Added a separate `BOOTCHAIN.TXT` report and append-only `HDDMAN.LOG` on the selected `mc0:`, `mc1:`, or `mass:` device.
- Added automatic storage selection from the ELF launch path and a USB-mount retry window for reports, logs, and rescue files.
- Added portable host tests for SHA-256 and the endian-stable capsule format.
- Added `docs/RESCUE_FORMAT.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, and contribution rules for storage-sensitive changes.
- Added reproducible CI and stable release automation around the pinned PS2DEV v2.0.0 toolchain.

### Changed

- Promoted the full-rescue and diagnostic feature set from release candidate to the stable `0.3.0` Torii line.
- Reorganized maintained C sources under `src/`, headers under `include/`, and technical documentation under `docs/`.
- Centralized the application version and release codename in `include/version.h`.
- Stopped tracking a prebuilt ELF in the source tree; release binaries and checksums are produced as build/release artifacts instead.
- Optimized SHA-256 for the R5900 by replacing the 64-word message schedule with a 16-word rolling schedule and hashing complete caller-owned blocks directly instead of copying each one first.
- Made `make test-host` independent of an installed PS2SDK environment.

### Safety

- Full capsules are accepted only after metadata, length, flags, APA structure, both SHA-256 digests, same-disk identity, KELF structure, and current `__mbr` bounds checks pass.
- A damaged or wrong-disk rescue capsule blocks silent fallback to a pointer-only restore.
- Legacy pointer-only restoration validates the target range and creates a fresh current-state safety backup before changing the pointer.
- An explicit OSDMenu `boot_auto` target takes precedence over stale partition evidence during classification.
- Rescue restoration and installation both retain pointer-last activation.
- The conservative two-sector raw fileXio transfer size is unchanged in Torii; larger transfers require explicit measurement and real-hardware validation.

### Validation status

- Release source is built with `-Wall -Wextra -Werror` using the pinned PS2DEV v2.0.0 toolchain.
- Portable SHA-256 and capsule-format tests cover streaming, complete-block, split-block, serialization, and rejection paths.
- The underlying `0.2.0` write workflows remain hardware-validated. Additional console, adapter, and HDD coverage for Torii remains welcome.

## [0.2.0] - 2026-08-19

### Changed

- Promoted the hardware-tested `0.2.0-rc2` code to the first full PS2 HDD Bootstrap Manager release.
- Removed the release-candidate label from the application and documentation.

### Hardware validation

- Confirmed the completed manager on real PlayStation 2 hardware after the standalone `mass:` backup was saved, read back, and verified without modifying HDD data.
- The complete `0.2.0` workflow was reported working as intended by Hifu Himejima.

## [0.2.0-rc2] - 2026-08-19

### Added

- Added a dedicated `START` action that saves and verifies the current 1024-byte APA master header without changing any HDD data.
- Made standalone backups available regardless of whether the HDD bootstrap pointer is enabled or disabled.

### Changed

- Updated the project credit from PunishedSnake to Hifu Himejima, the one gloriously unhinged developer who decided to correct this great injustice.

## [0.2.0-rc1] - 2026-08-19

### Changed

- Renamed the application from FHDB Bootstrap Manager to PS2 HDD Bootstrap Manager to reflect its broader scope.
- Replaced automatic cross-card backup selection with an explicit `mc0`, `mc1`, or `mass` storage menu.
- Changed new backup names to `HDDMBR.BIN` and `HDDMBR2.BIN` while retaining restoration compatibility with `FHDBMBR*.BIN` files from version 0.1.x.
- Replaced the one-way exit screen with a power menu offering shutdown, restart to the PS2 Browser, or return to the manager.

### Added

- Embedded BDM/FatFs USB mass-storage support for `mass:` backups and payload loading.
- Stock `MBR.XLF` structure validation and a 4 MiB safety limit.
- Console-side MagicGate KELF signing through PS2SDK `secrman`, `secrsif`, and the selected PS2 memory card.
- Manual MBR bootstrap installation to the reserved `__mbr` area beginning at sector `0x2000`.
- Reserved-area capacity checks before any payload write.
- Sector-by-sector post-flush verification of the complete signed payload.
- Pointer-last activation: `osdStart` and `osdSize` are updated only after the payload has been written and verified.
- English section comments throughout the expanded source.

### Safety

- Bootstrap installation is unavailable until the current pointer is disabled.
- Every disable, restore, and install path requires a verified header backup and a distinct three-button confirmation chord.
- Installation does not create partitions or copy the FHDB/HDD-OSD environment; it installs only the signed MBR program.

### Validation status

- The 0.1.1 disable path remains hardware-validated.
- The new storage, USB, restart, signing, and payload-installation paths are released as a candidate pending real-console testing.

## [0.1.1] - 2026-08-19

### Fixed

- Replaced POSIX `O_*` flags passed directly to `fileXioOpen()` with the required IOP `FIO_O_*` flags.
- Fixed memory-card backup verification on real hardware. In 0.1.0, `O_RDONLY` evaluated to `0` while IOP required `FIO_O_RDONLY` (`1`), so a backup could be created but its mandatory read-back failed.
- Removed `O_EXCL`, whose numeric value is a memory-card attribute rather than exclusive-create semantics when passed directly to the ROM memory-card driver.

### Added

- Per-path read, write, and verification diagnostics for memory-card backup failures.
- Four non-destructive backup slots across both memory-card ports.
- Hardware validation on an SCPH-50000 with MechaPWN, cross-model FMCB, and a standard APA HDD.

### Confirmed

- The tool successfully cleared `osdStart` and `osdSize`, recalculated a valid APA checksum through `ps2hdd`, and eliminated the post-uninstall FHDB boot loop without disconnecting or formatting the HDD.

## [0.1.0] - 2026-08-19

### Added

- Initial APA master-header validation.
- Full 1024-byte memory-card backup and restoration workflow.
- Same-disk backup matching.
- Hybrid APA/GPT rejection.
- Confirmation chords for disable and restore operations.
- Post-write HDD read-back verification.

### Known issue

- Used POSIX file flags with the direct fileXio API, causing backup read-back failure on hardware. The safety gate worked as designed: the HDD was not modified when verification failed.
