# Michishirube application-wide GS frontend

0.4.0 is the first release in which the complete normal application UI is rendered through one Graphics Synthesizer frontend. Physical PS2 validation exposed several distinct failures during development, and all of them are kept here because they document why the final display contract is intentionally conservative.

## Hardware iteration history

1. **Mixed libdebug + GS overlay** — signal worked, but normal screens were owned by libdebug while the HDD status panel was drawn separately with libdraw. The overlay also compensated for the debug renderer origin even though PS2SDK libdraw already applies the GS coordinate bias. Real hardware showed the status panel displaced into the lower-right by roughly +320 px / +112 px.
2. **Standalone 640x448 graph-owned display** — software build/link passed, but the physical console produced a black screen. This path is not used.
3. **Proven CRT bootstrap + application-wide GS rendering** — `init_scr()` establishes the known-good display/read-circuit state, while linker wrappers prevent normal application `scr_printf`/`scr_clear` calls from using the debug renderer. This produced a correctly placed full-screen manager.
4. **Virtual 448-line UI scaled into 224 field lines** — geometry was correct but bitmap glyphs lost/merged rows. The complete UI was therefore moved to native 640x224 coordinates.
5. **Native 640x224 UI with high-rate raw telemetry** — UI/font/locked states were correct, but rapid full-frame submission during forensic scanning visibly tore. 0.4.0 therefore synchronizes status-frame presentation with VBlank and coalesces high-rate raw READ telemetry.

## Final 0.4.0 video ownership

`init_scr()` remains only as the CRT/read-circuit bootstrap known to work on the tested console. The visible framebuffer stays at VRAM address 0. `gs_ui_ps2` reserves that framebuffer footprint in the VRAM allocator, allocates its font atlas after it, sets the standard libdraw `XYOFFSET = 2048,2048`, clips to `0..639 x 0..223`, and owns all ordinary application pixels from that point onward.

The built-in PS2SDK MSX glyph data is converted once into a 128x64 RGBA atlas and uploaded once to VRAM. Every glyph is rendered **8x8 source -> 8x8 destination** with nearest-neighbour sampling and integer native field coordinates. There is no 448->224 scaling stage.

Panels, cards, selection/disabled rows, outlines, status areas and progress bars are ordinary GS primitives. Frames are submitted through GIF DMA with alternating EE packet buffers.

Linker wrappers for `scr_printf`, `scr_vprintf`, and `scr_clear` route older incremental text screens through `gs_debug_compat_ps2` and therefore through the same GS renderer. The real libdebug renderer remains reachable only as a last-resort GS-initialization failure display.

## Menu states

Unavailable rows use a complete semantic state rather than merely changing the selection stripe:

- muted disabled background;
- warning-colored border/accent;
- dimmed label/hint text;
- explicit `LOCKED` marker on the right;
- explanatory hint describing why the operation is unavailable.

Disabled rows remain navigable so the reason can be read, but `X` cannot execute them.

## Themes and stable config

0.4.0 contains four predefined palettes sharing the same layout/safety semantics:

- `aqua` — default cyan/blue palette;
- `amber` — warm service-console palette;
- `sakura` — pink/violet accent palette;
- `mono` — neutral grayscale palette.

The theme can be changed live through **System -> UI theme**.

The stable application config is:

```text
HDDMAN.CFG
```

with the small format:

```text
theme=aqua
```

When `argv[0]` exposes a usable launch path, the manager reads/writes the config beside the ELF. Otherwise the selected backup/report storage root is used as a deterministic fallback. Missing/unwritable config is non-fatal. 0.4.x retains read-only compatibility with the development-only legacy `MICHISHIRUBE.CFG`, but official assets and new saves use `HDDMAN.CFG` only.

## Live operation telemetry

The GS status view is shared infrastructure rather than a forensic-specific overlay. It exposes:

- high-level operation;
- current action/phase;
- semantic location (`APA master`, reserved `__mbr`, `__sysconf`, `__system`, memory card, backup storage, etc.);
- I/O kind;
- physical LBA/range when a raw HDD command exists;
- operation progress or disk-relative position;
- explicit write-sensitive warning state.

Instrumentation covers startup HDD admission, boot-chain diagnostics, rescue/header backup preparation, disable/restore/install preflight, MagicGate signing, payload reads/writes/read-back verification, pointer update/read-back, deterministic health analysis, `HDDRAW`, exceptional master repair, `HDDMETA`, and forensic multi-header repair.

Low-level transports publish exact LBA information; higher layers publish the human-readable reason those sectors are being accessed.

## VBlank synchronization

A large healthy-disk forensic scan performs thousands of raw reads. Waiting for one VBlank per read would serialize disk I/O to 50/60 operations per second, so 0.4.0 separates telemetry production from frame presentation.

- complete frames are rendered into an off-screen native buffer and swapped on
  VBlank using PS2SDK `graph_wait_vsync()` and
  `graph_set_framebuffer_filtered()`;
- high-rate ordinary READ events are coalesced and the newest state is presented every 32 reads;
- WRITE / VERIFY / FLUSH / POINTER and semantic phase changes remain immediate;
- disk I/O itself is not limited to the display frame rate.

Physical retesting confirmed that visible forensic-scan screen tearing disappeared. The screen updates less frequently during rapid reads, which is intentional.

The two 640x224 buffers reuse the 640x448 VRAM reservation already budgeted by
0.4.0, so true double buffering adds no framebuffer-memory overhead.

## Physical validation result

Observed on real PS2 hardware before 0.4.0 release:

- video signal via `init_scr()` bootstrap — **PASS**;
- full-screen GS placement — **PASS**;
- hierarchical dashboard geometry — **PASS**;
- native complete 8x8 glyph raster — **PASS**;
- explicit `LOCKED` state — **PASS**;
- theme/config behavior — **PASS**;
- live status outside forensic-only workflows — **PASS**;
- VBlank-synchronized forensic status with no observed screen tearing — **PASS**.

The remaining release disclaimer applies to exceptional destructive HDD recovery, not to the normal GS frontend.
