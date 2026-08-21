# Michishirube application-wide GS frontend

The physical PS2 test of the first GS HDD HUD exposed a mixed-renderer bug: normal screens were still owned by libdebug while the live status panel was rendered separately through libdraw. The HUD also compensated for the debug renderer's coordinate origin even though PS2SDK libdraw already adds the GS +2048 primitive bias internally. The result was a deterministic +320 px horizontal / +112 px vertical displacement into the lower-right portion of the screen.

The current development frontend removes that architecture instead of tuning the overlay offset.

## Video ownership

`gs_ui_ps2` now owns normal application video setup. It allocates a 640x448 PSMCT32 framebuffer, initializes the display with `graph_initialize()`, configures the drawing environment with `draw_setup_environment()`, and uses the standard libdraw `XYOFFSET = 2048,2048`. `main.c` does not call `init_scr()` during normal execution.

The built-in PS2SDK MSX glyph data is converted once into a 128x64 RGBA atlas and uploaded once to VRAM. Text thereafter consists of textured GS sprites. Panels, menu cards, selection bars, outlines, status areas and progress bars are ordinary GS primitives. Frames are submitted through GIF DMA with two alternating EE packet buffers.

## Application UI

Shared menu navigation now calls `gs_ui_render_menu()` directly, so the manager dashboard and its Bootstrap, Diagnostics, Recovery, Backup & Storage, System, storage-picker and signing-card menus all use the same GS frontend.

Existing controller screens that still build text incrementally with `scr_clear()` / `scr_printf()` are routed through `gs_debug_compat_ps2`. That compatibility object provides those symbols and sends their accumulated text to the GS renderer, so source migration can proceed incrementally without allowing libdebug to draw a second UI into the framebuffer. The `scr_*` names in those sources are therefore compatibility calls, not evidence that libdebug still owns the screen.

`-ldebug` remains linked for the PS2SDK MSX font asset/toolchain compatibility; it is no longer the normal application renderer.

## Live HDD telemetry

Live HDD status remains event-driven and unthrottled. The same full-screen frontend renders operation, phase, I/O kind, physical LBA/range, disk progress, and write-sensitive warnings. There is no separate overlay coordinate space.

## Physical validation gate

Before treating this UI path as hardware-proven, validate on real consoles/displays:

- full-screen placement with no lower-right displacement;
- PAL and NTSC field geometry;
- readable 8x12 rendered glyph scaling and no clipping;
- dashboard/menu selection and disabled-state styling;
- compatibility text screens with long diagnostics;
- forensic raw-scan live telemetry;
- write/verify/flush status transitions;
- visible tearing or field artifacts under rapid event updates.

If tearing is visible, presentation synchronization/double-buffering should be addressed without reintroducing HDD-event throttling.

This UI validation is additive to the existing Michishirube hardware gates in `HARDWARE_VALIDATION_0.4.md` and `HARDWARE_FAULT_INJECTION.md`; it does not replace the destructive recovery validation matrix.
