# Video modes, UI scaling and font architecture

## Current public contract

Only two manager-owned outputs are supported:

| Config value | Backing surface | UI viewport | Color | Status |
|---|---:|---:|---:|---|
| `native` | 640x224 | 640x224 | 32-bit | hardware-proven default and fallback |
| `480p` | 768x448 stride | 720x448 | 32-bit | hardware-tested |

The signal timing, framebuffer, visible viewport and logical UI are different
things. A PS2SDK constant proving that the GS can request a timing does not
prove that a particular framebuffer/read-circuit configuration is correct, nor
that a television, cable and console combination can display it.

The public menu and `HDDMAN.CFG` must contain only modes which have completed
physical validation. Research modes belong in a separate build and must never
be persisted as a startup preference.

## What PS2SDK exposes

PS2SDK's graph API defines NTSC, PAL, 480p, 576p, 720p and 1080i timings, plus
several VGA timings. Its table describes the following nominal active modes:

| PS2SDK mode | Nominal timing | Important constraint |
|---|---:|---|
| NTSC | up to 640x448 interlaced | FIELD/FRAME and two read circuits require deliberate handling |
| PAL | up to 640x512 interlaced | not a drop-in higher-resolution native mode |
| 480p | 720x480 progressive | validated manager viewport is 720x448 |
| 576p | 656x576 progressive | PS2SDK silently substitutes PAL on ROM versions older than 2.20 |
| 720p | 1280x720 progressive | full active-height surface/viewport design required |
| 1080i | 1920x1080 interlaced | 540-line fields, interlace policy and VRAM require separate design |
| VGA | 640x480 through 1280x1024 | research-only; cable/sync/display compatibility differs from consumer HDTV modes |

PS2SDK itself warns in `libgs.h` that its default display-environment helper
supports only NTSC, PAL and 480p, and that vertical magnification is not
automatically calculated for other modes. Higher timing constants are building
blocks, not a completed UI backend.

## Why the v0.4.1 experimental modes failed

`graph_set_screen()` derives `MAGH` and `MAGV` using integer division between
the timing table and the requested screen dimensions. It then programs the GS
DISPLAY registers using the requested height. The v0.4.1 implementation used
the height of the scaled UI or letterbox as the height of the output surface.
That was valid for the specifically tested 480p path but not a general rule.

Examples of the broken assumption:

- a 1280x448 720p buffer described the UI band, not a complete 1280x720 output;
- a 960x448 1080i view omitted the remaining field/output geometry;
- PAL 576i mixed a 512-line framebuffer assumption with a path whose VBlank
  behavior was not validated;
- all modes reused one logical scale without proving the corresponding
  DISPLAY/DISPFB/read-circuit relationship.

The confirmation screen also called the unbounded `graph_wait_vsync()` before
the wall-clock input timeout. When PAL 576i stopped producing the expected
VBlank state, the code could not reach its automatic restore logic.

## Required backend model

Every future mode descriptor must contain all of the following independently:

1. signal timing and FIELD/FRAME policy;
2. read circuit and filtering policy;
3. framebuffer stride, height and pixel format;
4. complete active output dimensions;
5. centered UI viewport inside that output;
6. logical-to-viewport transform;
7. font raster and cell metrics;
8. bounded synchronization and native restoration policy;
9. exact VRAM budget for both buffers and texture assets.

The renderer should keep the existing logical 640x224 layout. Panels and other
vector-like GS primitives may be scaled into a viewport, but bitmap glyphs
must be selected and placed in output-pixel space to avoid fractional or
filtered raster damage.

### Candidate progressive layouts

The following are design candidates, not supported modes:

| Timing | Candidate framebuffer | UI viewport | Rationale |
|---|---:|---:|---|
| 576p | 704x576 stride, 16-bit, double buffered | 640x448 centered at roughly (8,64) | complete 576-line surface; integer 2x vertical UI |
| 720p | 640x720, 16-bit, double buffered, read-circuit MAGH 2x | 640x448 centered at y=136 | full-height surface without storing 1280 pixels per row |
| 1080i | unresolved | unresolved | requires a field-aware prototype; must not be inferred from progressive layouts |

These candidates must be checked against the actual GS DISPLAY/DISPFB values
produced by the pinned PS2SDK, then tested through a non-persistent diagnostic
ELF. A good-looking emulator screenshot is useful evidence, but it is not the
release gate.

## Synchronization safety

No mode-switch recovery path may depend on an unbounded VBlank wait in the mode
being tested. The next experimental backend should:

- clear/start the VSYNC event explicitly;
- poll `graph_check_vsync()` against `GetTimerSystemTime()`;
- abandon the candidate after a short bounded setup interval;
- restore the known-good native CRT/read-circuit state;
- reinitialize GIF DMA and all GS draw state;
- keep research modes session-only until the physical test matrix passes.

The ordinary render loop should also detect a lost VBlank and fail closed
instead of spinning forever.

## Font architecture

### Current font

The renderer uses PS2SDK's 8x8 `msx` font. It builds a 128x64 RGBA atlas once,
uses nearest-neighbor sampling and supports ASCII bytes. The font data is part
of PS2SDK and is covered by `AFL-2.0`; see
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

### Preferred additional font: Spleen

Spleen is a native bitmap family released under BSD-2-Clause. Its 5x8 and 8x16
sizes fit the two validated manager outputs without introducing FreeType or
runtime vector rasterization.

Proposed adaptive mapping:

| Output | Spleen raster | Logical cell behavior |
|---|---:|---|
| native | 5x8 | centered inside the existing 8x8 glyph area; existing 8-pixel advance preserved |
| 480p | 8x16 | rendered pixel-perfect inside the scaled 9x16 output cell |

Keeping the existing logical advance prevents menus, wrapping and status rows
from changing when a font or video mode changes. Only the raster inside the
cell changes.

### Proposed implementation

1. Add a small `ui_font_t` descriptor containing identifier, source glyph
   dimensions, destination offsets, supported character map and bitmap data.
2. Add a deterministic host tool which converts pinned BDF glyphs into compact
   C tables; generated files record source revision and SPDX identifier.
3. Build/upload only the selected atlas, reusing the same VRAM allocation.
4. Replace compile-time glyph metrics in wrapping code with a stable logical
   cell plus mode-specific output raster metrics.
5. Add `font=msx` and `font=spleen` to `HDDMAN.CFG`; unknown values fall back
   to `msx` without blocking startup.
6. Add host tests for parser round trips, atlas bounds, ASCII coverage, cell
   placement and deterministic generator output.
7. Validate both fonts in native and 480p on real hardware before release.

The first implementation should remain ASCII-only because the application UI
is currently ASCII. Latin-1/UTF-8 can be added later through an explicit code
point map rather than by pretending bytes above 127 are already Unicode.

## Licensing decision

The project's existing **MIT License remains appropriate for original source
code**. PS2SDK remains under AFL-2.0 and any future Spleen data remains under
BSD-2-Clause. These notices coexist; third-party work is not relicensed as MIT.

Release and source distributions must retain:

- the project [`LICENSE`](../LICENSE);
- [`PS2SDK_LICENSE.txt`](../PS2SDK_LICENSE.txt);
- [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md);
- the unmodified Spleen BSD-2-Clause license if Spleen data is added;
- links to the corresponding source repositories.

No Sony SDK font, asset, header, library or proprietary documentation may be
copied into the repository or release artifacts.

This is a practical open-source compliance decision, not jurisdiction-specific
legal advice.

## Primary references

- PS2SDK graph modes: <https://github.com/ps2dev/ps2sdk/blob/master/ee/graph/include/graph.h>
- PS2SDK mode/read-circuit calculations: <https://github.com/ps2dev/ps2sdk/blob/master/ee/graph/src/graph_mode.c>
- PS2SDK VBlank implementation: <https://github.com/ps2dev/ps2sdk/blob/master/ee/graph/src/graph.c>
- PS2SDK default-environment limitation: <https://github.com/ps2dev/ps2sdk/blob/master/ee/libgs/include/libgs.h>
- PS2SDK license and source: <https://github.com/ps2dev/ps2sdk>
- Spleen source and BSD-2-Clause license: <https://github.com/fcambus/spleen>
