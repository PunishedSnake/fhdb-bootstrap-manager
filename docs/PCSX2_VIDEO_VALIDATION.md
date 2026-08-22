# PCSX2-first video validation

PCSX2 is the primary fast feedback loop for development video modes. It does
not replace the final console gate, but it can reject a broken GS transaction
before another ELF is copied to physical media.

## Correlation established by dev5 and dev6

The same repeated-switch failure was reproduced on the target SCPH-50000 and
in PCSX2: dev5 could complete several native/480p/native transactions before a
later 480p entry went black. The defect was an undersized GIF clear packet and
therefore belonged to the emulated EE/GS contract rather than to the display.

After the packet overflow was fixed, dev6 completed twenty uninterrupted
native/480p/native cycles on both the physical console and PCSX2. Neither path
lost the candidate image, native rollback, input response or the ability to
start the next transaction.

PCSX2 also recognized the requested 576p, 720p and 1080i timings while showing
the same black candidate output previously observed during console testing.
Its console log independently recorded each timing transition and the return
to the native PAL/NTSC mode. A recognized timing is therefore evidence for the
CRT request only; it is not evidence that `DISPLAY`, `DISPFB`, `PMODE` and the
draw framebuffer form a visible read circuit.

## GS-dump diagnosis for dev7

Native-to-candidate-to-native GS dumps isolated one identical read-circuit
failure in all three candidates. Their CRT timing registers, `DISPFB2` base,
stride and pixel format matched the requested modes. `DISPLAY1` also contained
the intended window:

| Mode | `DISPLAY1` geometry | Active `DISPFB2` |
|---|---|---|
| 576p | 255,44; 1440x576; MAGH 1 | FBP 140, FBW 12, PSMCT32 |
| 720p | 302,24; 1280x720; MAGH 1 | FBP 140, FBW 10, PSMCT32 |
| 1080i | 232,36; 1920x1080; MAGH 2 | FBP 140/309, FBW 10, PSMCT32 |

However, `PMODE` enabled read circuit 2 while `DISPLAY2` was
`0x00000000551B6004` in every candidate. Decoded, that undefined value has
zero display width and height. The application had written `DISPLAY1` and then
read that privileged register back to populate `DISPLAY2`; GS display-control
registers cannot be used as EE-side storage. Dev7 now assembles the value once
in a local `u64` and writes it independently to both registers.

## Current evidence matrix

| Mode | PCSX2 timing | PCSX2 pixels | Rollback | Physical evidence | Status |
|---|---|---|---|---|---|
| native | recognized | visible | n/a | visible baseline | proven fallback |
| 480p | recognized | visible | 20/20 | 20/20 on SCPH-50000 | validated |
| 576p | recognized | visible in dev7 | returns native | matches emulator | dev8 geometry calibration |
| 720p | recognized | visible in dev7 | returns native | matches emulator | dev8 geometry calibration |
| 1080i | recognized | visible in dev7 | returns native | matches emulator | dev8 geometry calibration |

The dumps replace the earlier read-circuit hypothesis with a concrete defect
and register-level regression target. Candidate promotion still depends on
visible pixels, rollback and repeat testing rather than on the diagnosis alone.

## Dev8 viewport calibration

Dev7 screenshots from PCSX2 were confirmed to match physical-console output.
Native, 576p, 720p and 1080i already occupied the same measured horizontal
span, while their UI heights were approximately 100%, 86%, 63% and 84% of the
native reference. Dev8 therefore leaves horizontal presentation and the
validated CRTC timings unchanged, expanding only the logical vertical
viewports to 512, 711 and 518 lines. Each viewport stays inside its complete
backing surface; 1080i uses lines 22 through 539 of its 540-line FRAME buffer.
Glyph quads follow each snapped output cell even when its height is not an
integer multiple of the source bitmap, keeping text and panels at the same
apparent scale while leaving the established native and 480p dimensions
unchanged.

Dev9 performs the final per-mode alignment pass from the second screenshot
set. The 576p viewport is reduced to 472 lines so the complete footer remains
visible. 720p grows by one draw line and moves two DISPLAY lines down. 1080i
uses a 527-line viewport starting at frame line 13 while DISPLAY moves nine
lines down; those changes cancel at the top edge and extend the lower UI using
the remaining safe portion of the 1125-line raster.

Dev10 uses the third screenshot set for a sub-pixel-scale finishing pass. The
576p viewport grows from 472 to 480 lines. The 720p viewport uses its complete
720-line surface while DISPLAY returns from line 26 to line 24, expanding the
UI around its observed center instead of moving it down. The 1080i geometry is
unchanged: its 1080-line DISPLAY window already ends on raster line 1124, so a
further downward adjustment would enter vertical blanking.

The maintainer accepted the resulting 576p and 720p captures as ideal. Together
with the earlier matching console/emulator mode behavior and successful 1080i
output/rollback, this completes the visual promotion gate for stable 0.4.3.

## Required PCSX2 record

Every emulator result must record:

- exact PCSX2 version or commit;
- operating system and graphics backend;
- software or hardware GS renderer;
- ELF version and SHA-256;
- BIOS region/version used for the run;
- console log covering every mode transition;
- screenshot of the candidate and restored native output;
- a GS dump when the failure is visible in the dump;
- whether X confirmation, TRIANGLE rollback and timeout rollback were tested.

PCSX2's own graphics-report workflow treats GS dumps as replayable graphics
evidence and stores captures in its `snaps` directory. Do not hard-code a
capture shortcut here; use the current PCSX2 UI because frontend bindings can
change independently of this project.

## Fast regression gate

For a change limited to an already validated mode:

1. Cold-boot the ELF in native output.
2. Enter the video menu and select the candidate.
3. Confirm that the PCSX2 log reports the requested timing.
4. Require a visible, correctly scaled confirmation screen. A timing log plus
   a black image is a failure.
5. Test explicit TRIANGLE rollback and automatic timeout rollback separately.
6. Complete twenty uninterrupted native/candidate/native cycles without
   leaving the menu.
7. Leave and re-enter the menu, switch once more, and verify normal input and
   UI rendering.
8. Repeat with the saved candidate in `HDDMAN.CFG` and verify its guarded
   startup confirmation and fallback.

A known-good PCSX2 configuration may be used for routine iterations. Before a
candidate is proposed for physical promotion, repeat the visible-output test
with the software renderer and at least one hardware renderer to separate an
application defect from a backend-specific emulator defect.

## Candidate-mode development gate

For 576p, 720p or 1080i, record four results independently:

1. **TIMING** — PCSX2 recognizes the intended mode.
2. **PIXELS** — the candidate framebuffer is visible and correctly placed.
3. **ROLLBACK** — both TRIANGLE and timeout restore visible native output.
4. **REPEAT** — a later candidate transaction still works.

Only a candidate passing all four categories in PCSX2 should be copied to a
console. Failed emulator runs should retain the log and, where useful, a GS
dump so `PMODE`, `DISPLAY`, `DISPFB`, `FRAME`, field state and packet ordering
can be compared with the validated 480p path.

## What still requires a console

Physical validation remains mandatory for:

- analog DVE behavior and cable/display compatibility;
- ROM-version and console-region differences, especially legacy 576p setup;
- real GIF/DMAC/cache timing and recovery after stalls;
- interlaced field order, flicker and stability;
- guarded startup from real USB or memory-card storage;
- final promotion of any new mode into a stable release.

The intended workflow is consequently `portable tests -> R5900 build ->
PCSX2 -> physical PS2`, with most broken candidates dying at the inexpensive
third step instead of receiving a ceremonial trip through USB storage.
