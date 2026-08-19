# Changelog

All notable changes to FHDB Bootstrap Manager are documented here.

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
