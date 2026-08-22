# PS2 HDD Bootstrap Manager 0.4.2 — Michishirube

0.4.2 is an emergency display-safety hotfix. Physical testing of 0.4.1 showed
that only native output and 480p have correct geometry; PAL 576i could also
stall before the confirmation timeout ran. The five unvalidated modes have
therefore been removed rather than promoted from “experimental” to “creative
interpretation of a rectangle.”

## Video-mode safety fix

- **System -> Video mode** now exposes only hardware-proven `native` and
  hardware-tested `480p`.
- Removed NTSC 480i, PAL 576i, 576p, 720p and 1080i from the GS backend, menu
  and supported configuration values.
- Existing v0.4.1 configs using any removed identifier are mapped to `native`
  before the GS is touched and rewritten safely when storage is writable.
- Kept the physically validated 480p geometry unchanged: 720x448 visible,
  768x448 stride, 32-bit color and true double buffering.
- Reduced the fixed GS allocation from 3.875 MiB to 3.75 MiB, restoring 256 KiB
  of free VRAM.

The APA recovery policy, disk-write authorization and bootstrap transaction
logic are unchanged.

## Supported configuration

```text
theme=aqua
video_mode=native
```

Valid `video_mode` values are now `native` and `480p`.

---

## Historical 0.4.1 release notes

0.4.1 was the performance and display maintenance release for Michishirube. It kept the 0.4.0 recovery policy intact, made the forensic hot paths substantially cheaper on the EE, and gave the manager enough video modes to remind us that the PS2 was designed before displays learned to introduce themselves politely.

## Highlights since 0.4.0

- Faster APA forensic reconstruction: sorted LBA lookup replaces repeated linear node scans, compact bitsets replace repeated map-membership scans, impossible grid candidates are rejected before checksum work, and accepted headers reuse their existing checksum result.
- R5900 link-time optimization (`-O2 -flto`) across EE modules while section garbage collection remains enabled.
- Compatibility `scr_printf` screens are assembled in RAM and submitted once instead of rebuilding the complete display after every line.
- True VBlank-swapped double buffering for native and alternate output.
- Cached stable GS state, blend state, and glyph color setup, plus one reusable GIF packet that returns 256 KiB of EE heap to the application.
- Complete GS draw-state restoration after every video-mode reset.

## Video modes and persistent configuration

**System -> Video mode** now offers:

| `HDDMAN.CFG` value | Manager view | Color | Status |
|---|---:|---:|---|
| `native` | 640x224 FIELD | 32-bit | hardware-proven default/fallback |
| `ntsc-480i` | 640x448 FRAME | 32-bit | experimental |
| `pal-576i` | 640x512 FRAME | 32-bit | experimental |
| `480p` | 720x448 progressive | 32-bit | hardware-tested |
| `576p` | 656x512 progressive | 32-bit | experimental; ROM 2.20+ |
| `720p` | 1280x448 progressive | 16-bit | experimental |
| `1080i` | 960x448 interlaced | 16-bit | experimental |

Confirmed modes can be saved beside the ELF:

```text
theme=aqua
video_mode=480p
```

Every non-native switch still requires X confirmation within ten seconds. The same gate runs when a saved alternate mode is applied at startup. TRIANGLE, timeout, an unsupported 576p ROM, or setup failure restores native output. A failed startup also writes `video_mode=native`, because requiring a hex editor to escape a permanent black screen would be a remarkably authentic but unhelpful retro experience.

720p and 1080i use 16-bit framebuffers so both retain true double buffering inside the GS's 4 MiB VRAM. The fixed framebuffer/font allocation consumes 3.875 MiB and leaves 128 KiB free.

## Validation

- Complete portable host suite: **PASS**.
- 30 generated mounted-HDD fixtures: **PASS**.
- 9 sparse forensic raw-HDD fixtures: **PASS**.
- APA format, forensic graph, and video-mode identifier tests under ASan/UBSan: **PASS**.
- Guarded hardware fault-injector self-test: **PASS**.
- Stripped R5900 build with pinned PS2DEV v2.0.0 and LTO: **PASS**.
- Native <-> 480p switching and continued UI operation on physical hardware: **PASS**.

The additional NTSC, PAL, 576p, 720p and 1080i signals remain experimental until they receive their own console/cable/display coverage. Their confirmation and fallback path is shared with the hardware-tested 480p implementation.

## Recovery safety contract

0.4.1 does not change APA evidence weights, confidence thresholds, repair authorization, snapshot policy, source-stability checks, non-master-first/master-last ordering, flush/read-back verification, or the payload-first/pointer-last normal bootstrap transaction.

Exceptional direct APA master and topology repair remains experimental. Use sacrificial or fully imaged media for destructive recovery testing.

## Release assets

The recommended download is:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.4.1.zip
```

It contains:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.4.1.ELF
HDDMAN.CFG
SHA256SUMS.txt
```

The ELF, configuration file and checksum list also remain available as individual assets for anyone who prefers downloading three quest items instead of one chest.
