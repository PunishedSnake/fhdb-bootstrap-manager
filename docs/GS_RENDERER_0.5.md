# Scalable GS renderer and bitmap-font pipeline

This document describes the 0.5 development renderer built on top of the 0.4.2
display-safety hotfix. Native and 480p are physically proven. Development build
4 keeps guarded 576p, 720p and 1080i available while correcting the first-mode
transaction and native rollback regression observed in dev3.

## Geometry contract

Application screens remain authored in a stable logical 640x224 coordinate
system. A `ui_layout_t` independently records:

- complete active output width and height;
- framebuffer stride, height and pixel format;
- viewport origin and dimensions;
- logical-to-viewport horizontal and vertical scale.

The renderer clears the complete active output before drawing the viewport.
This makes centered and letterboxed layouts deterministic and prevents the UI
band from being mistaken for the complete output surface.

Primitive edges are transformed and pixel-snapped independently. Text cell
edges are also mapped independently rather than advancing by a rounded scaled
width. In 480p, logical x=8 therefore maps to output x=9 exactly, while the
right edge of every later cell is recalculated from its logical coordinate.
There is no cumulative 9/8 rounding drift.

## Font pipeline

`HDDMAN.CFG` accepts:

```text
font=msx
font=spleen
```

The original PS2SDK MSX raster remains available. Spleen 2.2.0 is pinned to
upstream commit `57f9219328c9f5873085320fe8bc8f7dd34b8791` under BSD-2-Clause.
Byte-identical BDF sources are stored with deterministic gzip compression and
the unmodified license in `third_party/spleen`.

`tools/generate_spleen_font.py` extracts printable ASCII into deterministic C
tables. `make test-host` regenerates the tables and compares them byte-for-byte
with the committed result.

The two resident atlases use 8x16 slots in 128x128 RGBA textures:

| Output | MSX raster | Spleen raster | Output cell |
|---|---:|---:|---:|
| native | 8x8 | 5x8 centered | 8x8 |
| 480p | 8x8 sampled to 8x16 | 8x16 | 9x16 |
| 576p / 720p | 8x8 sampled to 8x16 | 8x16 | 8x16 |
| 1080i | 8x8 sampled to 8x16 | 8x16 | 8x16 |

The logical text advance always remains eight pixels. Font changes therefore
do not alter wrapping, menu density or diagnostic layout.

## VBlank failure policy

Every presentation and mode transition clears the VSYNC event and polls it
against the EE system timer. GIF idle and GS FINISH waits are independently
bounded to 250 ms as well. Failure aborts only GIF PATH3 and restores the
captured native CRT/read-circuit state, layout and draw environment. Runtime
rollback never repeats `init_scr()` and therefore never resets unrelated SIF
or IOP DMA channels. The ten-second confirmation can consequently fail closed
even if an experimental frame never reaches FINISH.

## VRAM budget

The renderer retains a native pair and a fixed alternate reservation:

- two 640x224x32-bit native buffers;
- two 640x1080x16-bit reserved regions, reinterpreted by each alternate mode;
- two 128x128x32-bit font atlases, one native and one scaled.

The combined alternate reservation holds either the proven pair of
768x448x32-bit 480p buffers, one 768x576x32 576p buffer, one 640x720x32 720p
buffer, or two 640x540x32 1080i FRAME buffers. Page-aligned allocation plus both
atlases uses approximately 3.86 MiB and leaves 144 KiB of GS VRAM. Buffer and
atlas addresses never move during a mode switch.

## Extended mode contracts

| Mode | Render surface | Logical viewport | Read-circuit result |
|---|---:|---:|---|
| 576p | 768x576 stride, 720x576 visible, 32-bit, one buffer | 640x448 at (40,64) | complete progressive 720x576 output |
| 720p | 640x720, 32-bit, one buffer | 640x448 at (0,136) | horizontal 2x magnification to 1280x720 |
| 1080i | two 640x540x32 FRAME buffers | 640x448 at (0,46) | two fields form the 1920x1080 signal |

1080i presents each completed framebuffer for two VBlanks. Its odd and even
fields therefore come from the same UI frame rather than alternating between
two independently rendered buffers. Dev2 incorrectly treated FRAME storage as
1080 framebuffer lines; the GS FRAME contract stores 540 lines for 1080i.

576p and 720p deliberately use a single 32-bit buffer. Drawing begins at
VBlank and follows the scanout beam. A mostly static recovery UI benefits more
from a proven color/read-circuit format than from preserving double buffering
with the dev2 16-bit path that produced only a correctly timed black signal.

The 576p path detects the console ROM. ROM 2.20 and newer use the normal PS2SDK
request. On older retail ROMs, PS2SDK would silently substitute PAL. The
manager therefore asks the kernel for 480p first—576p uses the same DVE
parameters—and then changes only the GS timing to the established 576p values.
It never reconfigures the DVE through the active DEV9/HDD bus.

## Development build 2 physical result

Testing on the target SCPH-50000 and display produced a useful separation of
signal timing from framebuffer output:

- 480p displayed correctly;
- 576p, 720p and 1080i were recognized by the display but produced black
  frames;
- some rollbacks succeeded, proving the native timing path remained reachable;
- a later switch could hang before the ten-second restore, especially after a
  complete alternate/native cycle;
- leaving and re-entering the menu allowed more cycles, indicating persistent
  renderer/DMA state rather than accumulating VRAM allocation.

Code review confirmed that dev2 used the same unvalidated 16-bit scanout path
for every black mode, modeled 1080i FRAME as 1080 stored lines instead of 540,
called libdebug's global DMAC reset on every rollback, and still waited without
a bound for GIF idle and GS FINISH before reaching its bounded VBlank check.
Development build 3 corrects all four conditions while keeping every candidate
available from the menu.

## Development build 3 physical result and dev4 transaction

On the target SCPH-50000, dev3 changed the requested signal timing but every
alternate mode remained black. Returning to native changed the detected timing
again without restoring a visible framebuffer. The fault therefore preceded
the planned twenty-cycle stress test and was common to the mode transaction,
not to any one HDTV geometry.

Development build 4 restores the known-good dev2 ordering with three bounded
changes. A healthy alternate-to-native return calls `SetGsCrt()` without
resetting the GS or GIF, then atomically restores the complete captured native
read circuit including `DISPFB1/2`, `DISPLAY1/2`, `SMODE2`, `BGCOLOR` and
`PMODE`. A hard GIF/GS reset is reserved for an actual timeout. Finally, native
and scaled font atlases remain at fixed VRAM addresses and are uploaded only at
startup or when the user changes font, so a video transaction performs no heap
allocation and no texture transfer.

## Validation boundary

Portable tests prove layout geometry, font identifiers, ASCII source coverage
and deterministic generation. The R5900 build proves PS2SDK API and linker
compatibility. Neither replaces physical validation.

Before release, both fonts must be checked in every exposed mode for:

- complete glyph rows and columns;
- stable menu/text alignment;
- correct framebuffer swap;
- mode switch, confirmation, timeout and native restoration;
- repeated font changes before and after a video-mode change.

The dev4 transition stress test additionally requires at least twenty complete
`native -> candidate -> native` cycles in one uninterrupted menu session.

576p, 720p and 1080i are exposed only by the development build until their full
signal, framebuffer, viewport, field/read-circuit and fallback contracts pass
the hardware matrix. The stable release must not inherit them merely because
the compiler accepts the register writes.
