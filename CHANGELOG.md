# Changelog

All notable changes to PS2 HDD Bootstrap Manager are documented here.

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
