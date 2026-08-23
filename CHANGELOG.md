# Changelog

All notable changes to PS2 HDD Bootstrap Manager are documented here.

## [Unreleased]

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
