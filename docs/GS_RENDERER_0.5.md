# Scalable GS renderer and bitmap-font pipeline

This document describes the 0.5 development renderer built on top of the 0.4.2
display-safety hotfix. It is intentionally tested first in the two outputs
already proven on physical hardware: native and 480p.

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

The logical text advance always remains eight pixels. Font changes therefore
do not alter wrapping, menu density or diagnostic layout.

## VBlank failure policy

Every presentation and mode transition clears the VSYNC event and polls it
against the EE system timer. The wait is bounded to 250 ms. Failure restores
the proven native CRT/read-circuit state, GIF DMA setup, layout, atlas and draw
environment. The ten-second user confirmation can therefore continue even
when an experimental signal never produces the expected VBlank state.

## VRAM budget

The renderer retains separate native and 480p framebuffer pairs:

- two 640x224x32-bit native buffers;
- two 768x448x32-bit 480p backing buffers for a 720x448 active output;
- one 128x128x32-bit adaptive font atlas.

The fixed allocation uses approximately 3.78 MiB and leaves 224 KiB of GS VRAM.
Future modes must provide an explicit budget and may require 16-bit buffers;
they cannot borrow memory by describing only a partial UI band as a complete
framebuffer.

## Validation boundary

Portable tests prove layout geometry, font identifiers, ASCII source coverage
and deterministic generation. The R5900 build proves PS2SDK API and linker
compatibility. Neither replaces physical validation.

Before release, both fonts must be checked in native and 480p for:

- complete glyph rows and columns;
- stable menu/text alignment;
- correct framebuffer swap;
- mode switch, confirmation, timeout and native restoration;
- repeated font changes before and after a video-mode change.

576p, 720p and 1080i remain absent from the public mode list until their full
signal, framebuffer, viewport, field/read-circuit and VRAM contracts pass their
own hardware matrix.
