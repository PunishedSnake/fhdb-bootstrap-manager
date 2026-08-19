# Changelog

All notable changes to PS2 HDD Bootstrap Manager are documented here.

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
