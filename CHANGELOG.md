# Changelog

All notable changes to PS2 HDD Bootstrap Manager are documented here.

## [Unreleased]

### Disk layouts

- Admit APA-Jail type A2 disks (PSBBN Definitive Project / HOSDMenu: APA plus
  a PC MBR table exposing exFAT) instead of refusing to start. The classifier
  recognises exactly what the PSBBN installers write ("APAJ-A2" magic at
  0xE0, type-0x17 APA container, recomputed checksum); real GPT, protective
  0xEE, unrecognised and overlapping hybrids stay blocked.
- Treat the first non-container PC partition as an APA "jail fence": HDL free
  space is clipped at it, any new allocation that ps2hdd places across it is
  removed before the journal records it, and the forensic grid sweep stops
  there. Exceptional master repair stays blocked on any PC-signature disk.

### Bootstrap payloads

- Accept OSDMenu MBR's distributed `OSDMBR.XLF` when no `MBR.XIN`/`MBR.XLF`
  exists, and install it byte-for-byte without a MagicGate step, as the PSBBN
  installer does.
- Accept sector-aligned KELFs whose declared content ends in the final sector
  (kelftool documents that field as non-authoritative); OSDMenu MBR 1.3.0 was
  previously rejected with `KELF_ERR_FILE_SIZE`.
- Name byte-identical published payloads in `BOOTCHAIN.TXT` from an exact
  SHA-256 catalogue (`tools/payload_catalog_entry.py` adds entries).

### HDL Tools

- Install DVD9 images once the layer break is proven from a valid layer-1
  primary volume descriptor (hdl_dump convention, confirmed against
  mkps2iso's dual-layer writer); unproven images are still refused.

### Diagnostics

- New `HARDWARE AND LAYOUT` report section: disk layout and fence, DEV9
  model, read-only SPEED identity and an official/third-party adapter
  classification, with an explanation when PSBBN sits on a third-party
  adapter.
- `CONSOLE` section adds MechaCon revision, DEX/MechaPwn detection and the
  effective region policy (read-only CDVD queries, ported from PS2 Memory
  Card Inspector).

### Performance and cleanup

- SHA-256 kernel ~31% fewer R5900 instructions per block (~3,990 vs ~5,820).
- Library `printf` output is formatted integer-only into `HDDMAN.LOG`,
  keeping Newlib's floating `vfprintf` out of the ELF (~40 KiB).
- Removed unreferenced helpers; ignore generated host-test artefacts.

## [0.5.0] - 2026-09-20

**Codename: Kakehashi (架け橋)**

Kakehashi bridges the established PS2-side recovery toolkit with the first
guarded HDL-management workflow. The release intentionally ships a single
conservative internal-HDD implementation for the normal DEV9/ATA + APA/HDL
stack. Profiling builds and experimental alternate HDD-write optimization
variants are not included.

### HDL Tools

- Replaced the fixed 128-game installed list with a dynamically allocated raw
  APA catalogue that follows and validates the live linked list directly.
- Read HDLoader metadata lazily for only the eight games on the visible page,
  avoiding hundreds of partition open/seek/read/close RPC sequences on large
  HDDs.
- Added `LEFT/RIGHT` page navigation for installed games and kept `UP/DOWN` for
  selection; page controls no longer consume list rows or collapse text below
  a renderable height.
- Removed the fixed 64-ISO source limit and applied the same eight-row
  `LEFT/RIGHT` browser to `mass:/` ISO selection.
- Reworked large-disk admission to treat the driver's complete 32-bit sector
  count as an unsigned geometry after errno-range failures are rejected and to
  calculate free space from validated APA free nodes.
- Added lazy raw metadata snapshots keyed by main-partition LBA, main/sub count
  comparison and repeated metadata SHA-256 checks around guarded deletion.
- Re-probe the PS2 ISO identity before an initial or resumed write in addition
  to the journal's quick source fingerprint; complete source/target byte and
  SHA-256 verification remains the final authority.
- Split the PS2-only HDL controller into responsibility-specific include
  fragments while retaining one translation unit and the existing public API.

### UI and navigation

- Replaced the root list with a two-by-three card dashboard while retaining a
  dedicated short description for the selected section.
- Added directional-pad card navigation without changing the established
  vertical navigation of ordinary submenus.
- Discarded acknowledged compatibility-console screens so repeated visits no
  longer append another `Press X to return.` footer.

### Installer and transaction safety

- Added ISO9660 / `SYSTEM.CNF` identity probing before target allocation.
- Added bounded ISO-to-HDL transfer with source fingerprinting, transaction
  journal state, SHA-256 accumulation and mandatory target read-back.
- Added target-layout and metadata revalidation around destructive stages.
- Added a dedicated IOP streamer with direct USB/BDM source reads when
  supported, normal fallback otherwise, internal-HDD writes through ps2hdd,
  one IOP-to-EE copy for hashing, and double-buffered USB prefetch.
- Added safe single-buffer fallback when the second IOP staging allocation or
  prefetch worker cannot be created.
- Kept DVD9, split FAT32 image sets and recursive source discovery unsupported
  rather than guessing at partially defined formats.
- Marked ISO-to-HDL installation as **experimental/trial** for 0.5.0 pending a
  wider physical install + OPL boot matrix.

### Code and runtime cleanup

- Retained the accepted Corpus-v2 code-removal and locality work.
- Removed unused generic pathname/libc work from the application contract.
- Kept integer-only bounded formatting for the UI/status paths.
- Split large human-paced control flows into coarse stages and compile selected
  cold controller code with `-Os` while normal runtime code remains
  `-O2 -flto`.
- Reduced the Phase-2 static EE footprint versus the earlier Phase-1 baseline
  by roughly 4.5 KiB of named text and about 1100 instructions.
- Release builds do not enable the later PROFILE ON/OFF research variants.

### Release storage policy

- 0.5.0 ships one release path centered on the PS2 internal HDD connected
  through the expansion-bay / Network Adapter DEV9/ATA interface.
- Later experimental storage-write, checkpoint and materialized forensic
  benchmark variants are deliberately excluded from the release.
- Third-party adapters, SATA conversions and bridge devices remain outside the
  authoritative write-behavior matrix for this release.

## [0.4.3] - 2026-08-22

**Codename: Michishirube (道標)**

0.4.3 is the corrective display release. It keeps the 0.4.0 recovery contract
and publishes the renderer work that was temporarily numbered as 0.5.0-dev
while the actual milestone boundary was still being decided.

### Video and GS

- Added guarded `native`, `480p`, `576p`, `720p` and `1080i` output with
  ten-second confirmation and native fallback on every alternate startup.
- Replaced partial or incorrect HDTV frame descriptions with complete 32-bit
  surfaces and explicit signal/framebuffer/viewport contracts.
- Corrected 1080i FRAME storage to two 640x540 buffers and retained each frame
  for both interlaced fields.
- Fixed black HDTV output by writing the assembled DISPLAY value directly to
  both GS read circuits instead of reading a privileged write-only register
  back as temporary storage.
- Added the pre-ROM-2.20 576p setup without raw DVE access through the active
  DEV9/HDD bus.
- Added bounded GIF-idle, FINISH and VBlank waits plus a complete native GS
  rebootstrap that avoids libdebug's unrelated global DMAC reset.
- Fixed the repeated-switch EE heap corruption caused by a 64-qword clear
  packet receiving the 100 qwords required by the 480p buffer pair.
- Calibrated the final 576p, 720p and 1080i UI viewports against matching PCSX2
  and physical-console evidence.

### UI and fonts

- Added resolution-independent mapping from the stable 640x224 logical UI.
- Added selectable PS2SDK MSX and BSD-2-Clause Spleen bitmap fonts.
- Kept native/scaled font atlases resident so mode switching performs no font
  upload or heap allocation.

### Validation and packaging

- Completed 20 uninterrupted native/480p/native cycles on both the target
  SCPH-50000 and PCSX2.
- Promoted PCSX2 to the primary GS iteration gate after it reproduced the
  console's delayed black-screen failure and mode behavior.
- Retained the complete portable suite, 30 mounted-HDD fixtures, 9 forensic
  fixtures and the pinned PS2DEV v2.0.0 R5900 build.
- Expanded the release ZIP with project, PS2SDK, Spleen and Open PS2 Loader
  notices required by the shipped code and font data.
