# PS2 HDD Bootstrap Manager 0.4.3 — Michishirube

0.4.3 is the corrective display release for the Michishirube line. The work
was temporarily labeled `0.5.0-dev` while its final scope was being discovered,
but shipping it as 0.5.0 would imply a new product milestone. What actually
happened was that the PS2's video modes were taught to display pixels, survive
being selected more than three times, and return to native without requiring a
small religious ceremony.

The APA recovery policy and every HDD write invariant remain unchanged.

## Corrected video modes

**System -> Video mode** now provides:

| `HDDMAN.CFG` value | Output | Backing format | Status |
|---|---:|---:|---|
| `native` | automatic region-correct interlaced | 640x224x32 FIELD pair | proven default and fallback |
| `480p` | 720x448 progressive | 768x448x32 pair | 20-cycle PS2 + PCSX2 pass |
| `576p` | 720x576 progressive | 768x576x32 single surface | visible and calibrated |
| `720p` | 1280x720 progressive | 640x720x32 single surface, 2x read-circuit width | visible and calibrated |
| `1080i` | 1920x1080 interlaced | two 640x540x32 FRAME buffers | visible, stable fields and rollback |

Every non-native mode must be confirmed with X within ten seconds, including
when loaded from a saved configuration. TRIANGLE, timeout or an internal
failure restores native output. A failed startup also rewrites the preference
to `native`, because a permanent black-screen preference is technically
persistent configuration but not especially useful persistence.

Explicit `ntsc-480i` and `pal-576i` selectors remain removed. Old values are
sanitized to `native` before the GS is touched. Native already supplies the
console's region-correct interlaced output without reviving the PAL 576i VBlank
failure.

## GS transaction repairs

- Replaced incomplete HDTV descriptions with explicit signal, complete
  surface, framebuffer stride, viewport, pixel format and field contracts.
- Corrected 1080i FRAME storage from 1080 stored lines to two 640x540 buffers;
  each completed frame remains active for both fields.
- Fixed black 576p/720p/1080i output by writing the locally assembled DISPLAY
  value directly to both GS read circuits. Privileged DISPLAY registers are
  write-only from the EE and cannot safely double as temporary variables.
- Added a guarded legacy-ROM 576p setup using the kernel's 480p DVE path and
  established GS timing, without raw DVE access through the active DEV9 bus.
- Added bounded GIF-idle, GS FINISH and VBlank waits.
- Rebuilt native rollback around the proven CRT/read-circuit bootstrap without
  repeating libdebug's global DMAC reset.
- Fixed the repeated-switch heap corruption: clearing both 480p buffers emits
  100 qwords, while the old transition packet reserved only 64. The replacement
  persistent 256-qword packet is budgeted and checked before submission.
- Kept alternate VRAM reservations and both font atlases at fixed addresses so
  mode switching performs no allocation or texture upload.

## Resolution-independent UI and fonts

The application still authors every screen in a logical 640x224 space. The GS
renderer now maps it through a per-mode viewport with independently snapped
edges, preventing fractional scaling drift between text, cards and outlines.

Two fonts are selectable through **System -> UI font** or `HDDMAN.CFG`:

- `msx` — the original PS2SDK bitmap;
- `spleen` — Spleen 5x8 in native and 8x16 in scaled modes.

The project remains MIT-licensed. PS2SDK and the adapted Open PS2 Loader timing
reference retain AFL-2.0 notices; Spleen remains BSD-2-Clause. The ZIP contains
the applicable licenses and notices rather than relying on telepathy.

## Validation

- Full portable host suite: **PASS**.
- 30 generated mounted-HDD fixtures: **PASS**.
- 9 sparse forensic raw-HDD fixtures: **PASS**.
- Guarded physical-HDD fault-injector self-test: **PASS**.
- Stripped R5900 build with pinned PS2DEV v2.0.0 and LTO: **PASS**.
- Twenty uninterrupted native/480p/native cycles on SCPH-50000: **PASS**.
- The same twenty-cycle transaction in PCSX2: **PASS**.
- PCSX2 reproduced the earlier console failure and is now the primary fast GS
  gate; physical hardware remains the final DVE/cable/display authority.
- 576p, 720p and 1080i visible output and rollback: **PASS**.
- Final dev10 576p/720p geometry accepted by the maintainer: **PASS**.

## Recovery safety contract

0.4.3 does not change APA evidence weights, confidence thresholds, repair
authorization, snapshot policy, source-stability checks, non-master-first /
master-last ordering, flush/read-back verification, or the payload-first /
pointer-last normal bootstrap transaction.

Exceptional direct APA master and topology repair remains experimental. Use
sacrificial or fully imaged media for destructive recovery testing.

## Release assets

The recommended download is:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.4.3.zip
```

It contains the versioned ELF, `HDDMAN.CFG`, `SHA256SUMS.txt`, project license,
PS2SDK license and third-party notices. The ELF, configuration and checksum
list also remain available separately for anyone who prefers assembling a
release one collectible at a time.
