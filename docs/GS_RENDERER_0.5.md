# Scalable GS renderer and bitmap-font pipeline

This document describes the 0.5 development renderer built on top of the 0.4.2
display-safety hotfix. Native and 480p are physically proven. Development build
2 adds isolated, guarded 576p, 720p and 1080i contracts for hardware testing.

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

The active atlas uses 8x16 slots in a 128x128 RGBA texture:

| Output | MSX raster | Spleen raster | Output cell |
|---|---:|---:|---:|
| native | 8x8 | 5x8 centered | 8x8 |
| 480p | 8x8 sampled to 8x16 | 8x16 | 9x16 |
| 576p / 720p | 8x8 sampled to 8x16 | 8x16 | 8x16 |
| 1080i | 8x8 sampled to 8x32 | 8x16 sampled to 8x32 | 8x32 |

The logical text advance always remains eight pixels. Font changes therefore
do not alter wrapping, menu density or diagnostic layout.

## VBlank failure policy

Every presentation and mode transition clears the VSYNC event and polls it
against the EE system timer. The wait is bounded to 250 ms. Failure restores
the proven native CRT/read-circuit state, GIF DMA setup, layout, atlas and draw
environment. The ten-second user confirmation can therefore continue even
when an experimental signal never produces the expected VBlank state.

## VRAM budget

The renderer retains a native pair and a fixed alternate reservation:

- two 640x224x32-bit native buffers;
- two 640x1080x16-bit reserved regions, reinterpreted by each alternate mode;
- one 128x128x32-bit adaptive font atlas.

Each alternate region is large enough for the proven 768x448x32-bit 480p
layout, 768x576x16-bit 576p, 640x720x16-bit 720p and 640x1080x16-bit 1080i.
Page-aligned allocation plus the atlas uses approximately 3.80 MiB and leaves
more than 200 KiB of GS VRAM. Buffer addresses never move during a mode switch.

## Extended mode contracts

| Mode | Render surface | Logical viewport | Read-circuit result |
|---|---:|---:|---|
| 576p | 768x576 stride, 720x576 visible, 16-bit | 640x448 at (40,64) | complete progressive 720x576 output |
| 720p | 640x720, 16-bit | 640x448 at (0,136) | horizontal 2x magnification to 1280x720 |
| 1080i | 640x1080, 16-bit FRAME | 640x896 at (0,92) | horizontal 3x magnification to 1920x1080 |

1080i presents each completed framebuffer for two VBlanks. Its odd and even
fields therefore come from the same UI frame rather than alternating between
two independently rendered buffers.

The 576p path detects the console ROM. ROM 2.20 and newer use the normal PS2SDK
request. On older retail ROMs, PS2SDK would silently substitute PAL. The
manager therefore asks the kernel for 480p first—576p uses the same DVE
parameters—and then changes only the GS timing to the established 576p values.
It never reconfigures the DVE through the active DEV9/HDD bus.

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

576p, 720p and 1080i are exposed only by the development build until their full
signal, framebuffer, viewport, field/read-circuit and fallback contracts pass
the hardware matrix. The stable release must not inherit them merely because
the compiler accepts the register writes.
