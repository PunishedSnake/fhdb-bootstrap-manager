# Video modes, UI scaling and font architecture

## Current development contract

Native and 480p are the hardware-validated baseline. Development build 3 keeps
three guarded candidates available while correcting the dev2 framebuffer
format, 1080i FRAME geometry and repeated-switch rollback path:

| Config value | Backing surface | UI viewport | Color | Status |
|---|---:|---:|---:|---|
| `native` | 640x224 | 640x224 | 32-bit | hardware-proven default and fallback |
| `480p` | 768x448 stride | 720x448 | 32-bit | hardware-tested |
| `576p` | 768x576 stride | 640x448 at (40,64) | 32-bit, single buffer | guarded hardware candidate |
| `720p` | 640x720 | 640x448 at (0,136) | 32-bit, single buffer | guarded hardware candidate |
| `1080i` | 640x540 FRAME | 640x448 at (0,46) | 32-bit, double buffer | guarded hardware candidate |

The signal timing, framebuffer, visible viewport and logical UI are different
things. A PS2SDK constant proving that the GS can request a timing does not
prove that a particular framebuffer/read-circuit configuration is correct, nor
that a television, cable and console combination can display it.

The development menu may persist a candidate only after ten-second
confirmation, and it requires confirmation again at every startup. Stable
releases must contain only modes which completed physical validation.

## What PS2SDK exposes

PS2SDK's graph API defines NTSC, PAL, 480p, 576p, 720p and 1080i timings, plus
several VGA timings. Its table describes the following nominal active modes:

| PS2SDK mode | Nominal timing | Important constraint |
|---|---:|---|
| NTSC | up to 640x448 interlaced | FIELD/FRAME and two read circuits require deliberate handling |
| PAL | up to 640x512 interlaced | not a drop-in higher-resolution native mode |
| 480p | 720x480 progressive | validated manager viewport is 720x448 |
| 576p | 656x576 in libgraph; 720x576 in the mature DTV setup | PS2SDK silently substitutes PAL on ROM versions older than 2.20 |
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

## Implemented backend model

The 0.5 development renderer requires every mode descriptor to contain the
following independently:

1. signal timing and FIELD/FRAME policy;
2. read circuit and filtering policy;
3. framebuffer stride, height and pixel format;
4. complete active output dimensions;
5. centered UI viewport inside that output;
6. logical-to-viewport transform;
7. font raster and cell metrics;
8. bounded synchronization and native restoration policy;
9. exact VRAM budget for both buffers and texture assets.

The renderer keeps the existing logical 640x224 layout. Panels and other
vector-like GS primitives are scaled into a viewport. Bitmap glyphs are
selected and placed in output-pixel space to avoid fractional or filtered
raster damage. Rectangle and text-cell edges are snapped independently, so the
9/8 horizontal scale used by 480p cannot accumulate drift across a row.

### Implemented guarded layouts

The development renderer now implements complete surfaces rather than storing
only the UI band:

| Timing | Framebuffer | UI viewport | Rationale |
|---|---:|---:|---|
| 576p | 768x576 stride, 32-bit, single buffered | 640x448 centered at (40,64) | complete 720x576 visible surface; integer 2x vertical UI |
| 720p | 640x720, 32-bit, single buffered, read-circuit MAGH 2x | 640x448 centered at y=136 | full-height surface without storing 1280 pixels per row |
| 1080i | 640x540, 32-bit, double buffered, FRAME, read-circuit MAGH 3x | 640x448 centered at y=46 | correct 540-line FRAME storage; two fields form 1080i |

The mode descriptors include signal, surface, stride, viewport, color depth,
interlace policy and exact DISPLAY magnification. Portable tests prove the
geometry and VRAM bounds. Physical output remains the release gate.

### Legacy-ROM 576p

PS2SDK v2.0.0 silently substitutes PAL when `GRAPH_MODE_HDTV_576P` is requested
on ROM versions older than 2.20. This includes the target SCPH-50000 generation
and explains why the former confirmation screen described 576p while the GS was
actually running another timing.

The guarded implementation uses the normal PS2SDK request on ROM 2.20 and
newer. On older retail ROMs it first requests 480p so the kernel installs the
identical DVE parameters, then changes only the GS timing to the established
576p values adapted from Open PS2 Loader's GSM implementation. It never uses
the historical raw DVE transactions through DEV9 while the HDD stack is live.

## Synchronization safety

No mode-switch recovery path depends on an unbounded VBlank, GIF DMA or GS
FINISH wait in the mode being tested. The implemented backend:

- clears/starts the VSYNC event explicitly;
- polls `graph_check_vsync()` against `GetTimerSystemTime()`;
- abandons the candidate after a bounded 250 ms setup/presentation interval;
- restores the captured known-good native CRT/read-circuit state;
- resets only GIF PATH3 and rebuilds all GS draw state;
- never repeats libdebug's global DMAC reset after startup;
- require ten-second confirmation again at startup until the physical test
  matrix passes.

The ordinary render loop uses the same bounded wait. A lost VBlank restores
native output instead of spinning forever and preventing the input timeout.

## Font architecture

### Available fonts

The renderer provides two selectable fonts:

- PS2SDK's 8x8 `msx` font under `AFL-2.0`;
- Spleen under `BSD-2-Clause`, using 5x8 in native output and 8x16 in scaled modes.

It builds the selected native and scaled rasters into two 128x128 RGBA atlases, uses nearest-neighbor
sampling and supports the application's printable ASCII UI. See
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

### Spleen integration

Spleen is a native bitmap family released under BSD-2-Clause. Its 5x8 and 8x16
sizes fit the two validated manager outputs without introducing FreeType or
runtime vector rasterization.

Implemented adaptive mapping:

| Output | Spleen raster | Logical cell behavior |
|---|---:|---|
| native | 5x8 | centered inside the existing 8x8 glyph area; existing 8-pixel advance preserved |
| 480p | 8x16 | rendered pixel-perfect inside the scaled 9x16 output cell |
| 576p / 720p | 8x16 | rendered pixel-perfect inside the 8x16 output cell |
| 1080i | 8x16 | pixel-perfect inside the corrected 8x16 output cell |

Keeping the existing logical advance prevents menus, wrapping and status rows
from changing when a font or video mode changes. Only the raster inside the
cell changes.

### Implementation

1. `ui_font` owns stable identifiers and names.
2. `tools/generate_spleen_font.py` converts pinned BDF glyphs into compact C
   tables and records the exact upstream revision plus SPDX identifier.
3. Both font variants are uploaded at startup or when the font changes; a
   video-mode transition only selects an existing VRAM address.
4. Text keeps an 8x8 logical cell while the renderer selects a mode-appropriate
   output raster and centers it in the pixel-snapped cell.
5. `font=msx` and `font=spleen` are supported in `HDDMAN.CFG`; unknown values
   fall back to `msx` without blocking startup.
6. Host tests cover identifiers, source glyph coverage, layout transforms and
   deterministic generator output.
7. Native and 480p have completed real-hardware validation with both fonts;
   every newly exposed output repeats the same font and switching matrix.

The first implementation should remain ASCII-only because the application UI
is currently ASCII. Latin-1/UTF-8 can be added later through an explicit code
point map rather than by pretending bytes above 127 are already Unicode.

## Licensing decision

The project's existing **MIT License remains appropriate for original source
code**. PS2SDK and the adapted Open PS2 Loader GSM timing values retain their
Academic Free License notices, while embedded Spleen data remains under
BSD-2-Clause. These notices coexist; third-party work is not relicensed as MIT.

Release and source distributions must retain:

- the project [`LICENSE`](../LICENSE);
- [`PS2SDK_LICENSE.txt`](../PS2SDK_LICENSE.txt);
- [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md);
- the unmodified Spleen BSD-2-Clause license;
- the Open PS2 Loader GSM attribution notice;
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
- Open PS2 Loader GSM reference: <https://github.com/ps2homebrew/Open-PS2-Loader/blob/master/ee_core/src/gsm_engine_adv.S>
- Spleen source and BSD-2-Clause license: <https://github.com/fcambus/spleen>
