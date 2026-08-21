# Michishirube application-wide GS frontend

Physical PS2 validation has now exposed three separate generations of the 0.4 GS frontend. Keeping those results explicit is important because the failures came from different layers.

1. **Mixed libdebug + GS overlay** — signal worked, but normal screens were owned by libdebug while the HDD status panel was drawn separately with libdraw. The overlay also compensated for the debug renderer's origin even though PS2SDK libdraw already adds its own GS coordinate bias. On real hardware this produced the observed lower-right displacement of approximately +320 px horizontally and +112 px vertically.
2. **Standalone 640x448 graph-owned display** — software build/link passed, but the physical console produced a black screen. That path is no longer used.
3. **Proven CRT bootstrap + application-wide GS rendering** — `init_scr()` establishes the physically proven display/read-circuit state, while linker wrappers prevent normal application `scr_printf`/`scr_clear` calls from using the debug renderer. This produced a correctly placed full-screen manager on real hardware. The remaining defect visible in the physical photographs was the glyph raster: the UI was authored at 640x448 and divided vertically by 2 into the 640x224 field drawing space, which compressed an 8-pixel bitmap font into fractional/merged rows.

The current code removes that last transform. The complete UI is authored directly in the hardware-proven **640x224 field coordinate space**.

## Video ownership

`init_scr()` remains only as the CRT/read-circuit bootstrap known to work on the tested console. The visible framebuffer stays at VRAM address 0. `gs_ui_ps2` reserves that framebuffer footprint in the VRAM allocator, allocates its font atlas after it, sets the standard libdraw `XYOFFSET = 2048,2048`, clips to `0..639 x 0..223`, and then owns all ordinary application pixels.

The built-in PS2SDK MSX glyph data is converted once into a 128x64 RGBA atlas and uploaded once to VRAM. Every glyph is now rendered **8x8 source -> 8x8 destination** with nearest-neighbour sampling and integer native field coordinates. There is no 448->224 Y scaling stage. Panels, cards, selection/disabled bars, outlines, status areas and progress bars are ordinary GS primitives. Frames are submitted through GIF DMA with two alternating EE packet buffers.

Linker wrappers for `scr_printf`, `scr_vprintf`, and `scr_clear` route legacy incremental text screens through `gs_debug_compat_ps2` and therefore through the same GS renderer. The real libdebug renderer remains reachable only as a last-resort GS-initialization failure display.

## Menu states

Physical feedback confirmed that the overall five-section dashboard layout is useful and readable, but unavailable rows were not distinct enough. The current renderer therefore gives disabled actions a complete semantic state rather than merely changing the selection stripe:

- muted disabled background;
- warning-colored border/accent;
- dimmed label/hint text;
- explicit `LOCKED` marker on the right;
- the existing hint continues to explain why the operation is unavailable.

Disabled rows remain navigable so their reason can be read, but `X` still cannot execute them.

## Themes and launch-local config

0.4 now contains four predefined palettes that share the same layout and safety semantics:

- `aqua` — default cyan/blue palette;
- `amber` — warm service-console palette;
- `sakura` — pink/violet accent palette;
- `mono` — neutral grayscale/high-compatibility palette.

The theme can be changed live in **System -> UI theme** with no restart. `MICHISHIRUBE.CFG` uses the small format:

```text
theme=aqua
```

When `argv[0]` exposes a usable launch path, the manager reads/writes the config beside the ELF. If the launcher does not expose such a directory, the selected backup/report storage root is used as a deterministic fallback. Missing or unwritable config is not fatal: the selected theme remains active for the current session. CI artifacts include a default `MICHISHIRUBE.CFG` beside the ELF.

## Live operation telemetry

The GS status view is no longer forensic-specific. The shared publisher now exposes:

- operation;
- current action/phase;
- semantic location (`APA master`, reserved `__mbr`, `__sysconf`, `__system`, memory card, backup storage, etc.);
- I/O kind;
- physical LBA/range whenever an HDD command exists;
- operation progress or disk-relative position;
- explicit write-sensitive warning state.

Instrumentation currently covers startup HDD admission, boot-chain diagnostics, rescue/header backup preparation, disable/restore/install preflight, MagicGate signing, payload reads/writes/read-back verification, pointer update/read-back, deterministic health analysis, HDDRAW snapshots, exceptional master repair, HDDMETA snapshots, and multi-header forensic repair. Low-level raw transports publish exact LBA information; higher layers publish the human-readable reason that those sectors are being accessed.

Presentation remains unthrottled. If field tearing is observed under rapid events, presentation synchronization should be fixed rather than hiding HDD events.

## Physical validation gate

The following are already physically observed:

- video signal via `init_scr()` bootstrap — **PASS**;
- full-screen GS manager placement after removal of the offset compensation — **PASS**;
- hierarchical dashboard/menu geometry — **PASS**;
- old 448->224 bitmap-font scaling — **FAIL**, visible as stretched/compressed/missing glyph rows and now removed.

The next build must validate:

- crisp complete 8x8 glyph rows in native 640x224 coordinates;
- `LOCKED` disabled-state visibility;
- all four theme palettes and live switching;
- `MICHISHIRUBE.CFG` load/save from the launch directory used by the real launcher;
- compatibility text screens with long diagnostics;
- startup/diagnostics/bootstrap/recovery live status transitions;
- correct semantic location and exact LBA where applicable;
- visible tearing or field artifacts during rapid raw reads.

This UI validation is additive to the existing Michishirube hardware gates in `HARDWARE_VALIDATION_0.4.md` and `HARDWARE_FAULT_INJECTION.md`; it does not replace the destructive recovery validation matrix.
