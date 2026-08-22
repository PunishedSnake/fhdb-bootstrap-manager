# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 HDD bootstrap,
diagnostic, forensic-backup and guarded-recovery toolkit. It manages the HDD
OSD boot chain and APA metadata without formatting the disk or treating
"delete everything" as a particularly inspired recovery algorithm.

It began after a real console got trapped in a post-uninstall FHDB boot loop: FHDB was gone, but the bootstrap pointer was still enabled, so the machine faithfully rebooted into software that no longer existed. Apparently uninstalling a program and persuading the console to stop launching it were separate premium features.

## Current release

**0.4.1 — Michishirube (道標)** is the current stable release.

Michishirube expands the project into a modular PS2-side recovery toolkit while preserving the established normal bootstrap write contract from Torii. The release includes:

- full-screen Graphics Synthesizer UI;
- hierarchical navigation and explicit `LOCKED` states;
- live HDD operation/LBA telemetry with VBlank-synchronized presentation;
- domain/stage-aware error explanations instead of bare negative integers;
- portable APA forensic graph reconstruction;
- read-only degraded shadow-map inspection;
- guarded deterministic master recovery;
- guarded multi-header topology repair;
- `HDDRAW`, `HDDMETA`, rescue, log and forensic evidence artifacts;
- a large host regression laboratory and guarded physical-HDD fault injector.

0.4.1 builds EE code with `-O2` plus link-time
optimization. This lets the R5900 compiler optimize across module boundaries
while the existing section garbage collection continues to remove unused code
from the stripped release ELF.

The current feature branch identifies itself as **0.5.0-dev3 — Kakehashi**. It
is based on the 0.4.2 display-safety hotfix and introduces a resolution-aware
GS viewport plus selectable bitmap fonts. It remains a hardware-test build,
not a stable release.

## Important recovery disclaimer

**Read-only diagnostics, backups, forensic scanning, report generation, UI safety gates, and the normal bootstrap workflows have received substantial real-console validation. Exceptional raw metadata repair remains experimental in 0.4.1.**

Experimental paths include:

- direct sectors 0-1 APA master repair;
- forensic `prev` / `next` topology writes;
- multi-header metadata repair;
- real power-loss/interruption behavior;
- adapter, bridge, HDD/SSD and controller combinations not yet covered by multiple independent testers.

The implementation is intentionally conservative: it snapshots original bytes, fails closed on ambiguous evidence, checks source stability immediately before writes, writes non-master headers before master LBA 0, flushes and reads back every mutation, and requires a restart after exceptional recovery. Those controls reduce risk; they do not replace broad hardware testing.

If you test recovery, use **sacrificial or fully imaged media** whenever possible and report the console model/ROMVER, disk and adapter, exact corruption scenario, proposed diff, `HDDMAN.LOG`, `FORENSIC.TXT`, relevant `HDDRAW`/`HDDMETA` artifacts, and raw before/after header bytes if available.

Do not make an experimental metadata repair your first move on irreplaceable data.

## What the manager understands

The PS2 ROM decides whether to launch an HDD update from two fields in the APA master header:

- `osdStart` — starting sector of the signed HDD bootstrap program;
- `osdSize` — program size in sectors.

If uninstall software removes FHDB but leaves those fields populated, the ROM can repeatedly launch a missing or unusable payload. The manager can back up the current state, clear only that pointer, restore a verified payload, or sign/install a replacement MBR program without touching unrelated partition contents.

A 1024-byte header backup preserves metadata but not the program referenced by the OSD pointer. Full rescue capsules therefore contain the APA master header, the exact sector-aligned active payload, metadata, and SHA-256 digests. Restoration writes and verifies the payload first and exposes it through `osdStart`/`osdSize` only afterward.

## Main features

- Validates the complete 1024-byte APA `__mbr` master header and checksum.
- Rejects hybrid/protective APA/GPT layouts on write-capable paths.
- Creates non-overwriting verified header backups and full rescue capsules.
- Restores full payloads before exposing their OSD pointer.
- Disables only the HDD bootstrap pointer through `HDIOC_SETOSDMBR(0,0)`.
- Restores compatible `HDDMBR*.BIN` and legacy `FHDBMBR*.BIN` pointer backups.
- Structurally validates and MagicGate-signs stock MBR KELFs.
- Prefers `MBR.XIN` and accepts historical `MBR.XLF` as a compatibility fallback.
- Writes normal bootstrap payloads only inside the reserved `__mbr` program area beginning at sector `0x2000`.
- Produces `HDDMAN.LOG` and `BOOTCHAIN.TXT` diagnostics.
- Fingerprints sector images and unpadded KELFs with SHA-256.
- Inspects FMCB HDD-skip settings/modules and characteristic FHDB, OSDMenu, PSBBN, HOSDMenu, HDD-OSD, and custom downstream evidence.
- Provides deterministic mounted-disk structure-health policy through portable `repair_health`.
- Provides narrowly gated exceptional APA master recovery with exact `HDDRAW*.BIN` snapshots.
- Provides raw read-only APA forensic reconstruction independent of normal `ps2hdd` admission.
- Builds forward-link, reverse-link and geometry candidate maps with explicit evidence/confidence.
- Allows read-only browsing of a reconstructed shadow APA map without pretending the physical disk is healthy.
- Exports `FORENSIC.TXT` with discovered headers, active maps, checksums, conflicts, overlaps, dormant free-space evidence, and repair-plan state.
- Builds guarded topology repair plans limited to reconstructable `prev`, `next`, and checksum fields.
- Saves every touched original header in SHA-256-protected `HDDMETA*.BIN` before topology repair.
- Commits non-master headers first and master LBA 0 last, with flush/read-back and final verification.
- Tracks one/two-bit link changes explicitly; stale checksum plus independent graph evidence can corroborate an exact correction.
- Includes `tools/hardware_fault_injector.py` for guarded, reproducible corruption tests on images or sacrificial physical HDDs.

## Graphics Synthesizer UI

0.4.0 uses one application-wide GS frontend rather than mixing a debug terminal with a separate overlay.

The physically validated display contract is intentionally conservative:

- `init_scr()` is retained only as the known-good CRT/read-circuit bootstrap;
- the visible framebuffer remains at VRAM address 0 in the proven 640x224 FIELD drawing space;
- `gs_ui_ps2` renders normal application pixels through libdraw/GIF DMA;
- the active bitmap font is generated into a reusable RGBA texture atlas;
- menu cards, outlines, locked rows, progress bars and status panels are GS primitives;
- double-buffered modes swap complete frames on VBlank; full 32-bit 576p and
  720p use one VBlank-paced surface to stay inside the GS's 4 MiB VRAM;
- remaining source-level `scr_clear()` / `scr_printf()` compatibility screens are intercepted and rendered through the same GS frontend;
- real libdebug drawing is retained only as a renderer-initialization emergency fallback.

Physical testing found and fixed the earlier mixed-renderer lower-right displacement, a standalone GS black screen, fractional-Y glyph corruption, and scan-time screen tearing.

The 0.4.2 hotfix exposes **System -> Video mode** with the two modes that passed
physical testing: native and 480p. The 0.5 development renderer retains those
unchanged backends and adds guarded 576p, 720p and 1080i test modes with full
surfaces, explicit DISPLAY contracts and independent UI viewports. Explicit
NTSC/PAL choices remain removed; `native` already provides the proven
region-correct interlaced fallback without reviving the PAL VBlank failure.

Because the PS2 remains admirably uninterested in negotiating modern display
capabilities, every non-native choice must be confirmed with X within ten
seconds. TRIANGLE, no input, or an internal setup failure restores the proven
native output. Confirmed choices can be stored in `HDDMAN.CFG`, but are guarded
again at startup. A startup timeout restores native and rewrites the preference
to `native`, preventing a persistent black-screen loop.

The development renderer treats the signal, complete framebuffer, render
surface and logical UI viewport as separate geometry. The existing 640x224 UI
is transformed into the selected viewport with independently snapped edges,
so fractional horizontal scales do not accumulate text or panel drift. Every
frame clears the complete active output before drawing the viewport, providing
deterministic letterboxing for future modes. VBlank waits are bounded; loss of
the expected signal state restores the native display instead of blocking the
confirmation timer forever.

### Themes and configuration

Available themes:

- `aqua` — default cyan/blue;
- `amber` — warm service-console palette;
- `sakura` — pink/violet accent palette;
- `mono` — neutral grayscale palette.

Themes can be changed live through **System -> UI theme**.

Available fonts can be changed live through **System -> UI font**:

- `msx` — the original PS2SDK 8x8 raster;
- `spleen` — Spleen 5x8 in native output and 8x16 in scaled modes.

Spleen uses its native bitmap size where the viewport permits it. The 1080i
viewport applies an integer 2x vertical enlargement to the 8x16 raster rather
than inventing fractional glyph pixels. The logical 8-pixel text advance
remains stable, so changing fonts does not reflow menus or diagnostics.

The stable config filename is:

```text
HDDMAN.CFG
```

Typical contents:

```text
theme=aqua
video_mode=native
font=spleen
```

The 0.5 development build accepts `native`, `480p`, `576p`, `720p` and `1080i`.
Native and 480p retain their hardware-tested 32-bit double buffers. Following
physical dev2 testing, 576p and 720p now use complete single-buffered 32-bit
surfaces, while 1080i FRAME uses two 640x540x32 buffers. This avoids the failed
16-bit scanout path without exceeding the same fixed VRAM reservation. Every
alternate mode remains guarded by confirmation on every startup. Old explicit
`ntsc-480i` and `pal-576i` values are still sanitized to `native` and rewritten
when storage is writable.

An unknown `font` value falls back to `msx` and is rewritten when possible.
Font selection is cosmetic and can never block application startup.

When the launcher provides a usable `argv[0]`, the manager reads/writes the file beside the ELF. Otherwise it falls back to the selected report/backup storage root. Missing or unwritable config never blocks the manager. 0.4.x retains read-only compatibility with the development-only legacy name `MICHISHIRUBE.CFG`, but official assets and saves use only `HDDMAN.CFG`.

### Live operation monitor

Long-running operations publish:

```text
OPERATION   high-level workflow
ACTION      current phase
LOCATION    semantic disk/device region
I/O         READ / WRITE / VERIFY / FLUSH / POINTER UPDATE / SCAN
PROGRESS    operation or disk-relative progress
SECTOR      exact physical LBA/range when a raw HDD command exists
```

Instrumentation covers startup admission, diagnostics, backup, disable, legacy/full restore, MBR source validation, MagicGate signing, install, payload reads/writes, pointer changes, deterministic health assessment, `HDDRAW`, exceptional master repair, `HDDMETA`, and forensic multi-header repair.

Presentation uses a complete off-screen framebuffer and a VBlank swap. High-rate raw `READ` telemetry is coalesced so the UI shows the newest state without turning every disk access into a mandatory 50/60 Hz wait. WRITE/VERIFY/FLUSH/pointer and phase changes remain immediate. This preserves the release fix for visible forensic-scan tearing while removing the remaining draw-versus-scan race for every screen.

## Contextual errors

The manager preserves raw numeric return codes for diagnostics and tests, but user-facing failures also include:

- symbolic error ID;
- failing stage;
- summary;
- likely reason in the current operation context;
- recommended next action;
- raw code.

Small IOP error numbers are not blindly translated without context because different drivers may reuse the same values.

## Forensic APA reconstruction

The forensic scanner treats disk structure as evidence rather than immediately trusting every readable sector.

It combines:

- `next` / `prev` links;
- `main` and `subs[]` references;
- start/length geometry;
- APA magic/type/flags;
- checksums;
- reciprocal links;
- overlap/conflict detection;
- direct-grid and reference-followed evidence.

A candidate map is a hypothesis in RAM. It is **not** silently injected into normal writable `ps2hdd`/PFS state.

### Large HDL disks and truncated scans

Real hardware testing on a healthy large HDL-heavy disk exposed a safety-critical edge case in the old 512-node scanner: the visible partial tail was mistaken for the physical tail and produced two artificial speculative endpoint patches.

0.4.0 therefore enforces:

```text
truncated scan => read-only only
```

This is checked independently by map policy, repair-plan construction, UI authorization and the PS2 raw writer. The current node budget is 2048, but raising the limit is only a usability improvement; the fail-closed rule remains valid at any capacity.

### Dormant free-space remnants

The same disk exposed checksum-valid historical `__empty` headers left behind inside a later coalesced active `__empty` extent. These are retained in forensic evidence as `DORMANT_FREE`, but they do not compete with the canonical active chain when the stale extent is wholly contained by the active free region.

The final healthy release-validation report showed:

```text
Nodes        : 1621 / 2048
Dormant free : 8
Truncated    : no

MAP 1: forward links
 confidence=100
 nodes=1613
 reciprocal=1612
 inferred=0
 conflicts=0
 overlaps=0
 patches=0
 automatic=no
 manual=no
```

That is the intended healthy-disk result: a complete, perfectly reciprocal active chain, historical free-space evidence retained for inspection, and no repair proposal.

## Recovery trust levels

### Normal bootstrap writes

Every normal HDD-changing path requires:

1. normal `ps2hdd` APA admission;
2. valid current master/checksum/non-hybrid layout;
3. fresh verified backup;
4. explicit user confirmation;
5. payload bounds inside reserved `__mbr` space;
6. payload write + flush + exact read-back before pointer exposure;
7. `HDIOC_SETOSDMBR` for normal pointer changes;
8. pointer read-back verification.

Normal install/restore/disable workflows do **not** raw-write sectors 0-1.

### Exceptional single-master recovery — experimental

A damaged master may prevent normal admission. The exceptional path requires:

1. raw sectors 0-1 remain readable;
2. portable `apa_repair` proves one narrowly reconstructable canonical field;
3. stale checksum corroborates that exact correction;
4. checksum-valid ambiguity, multiple unexplained changes, low identity and GPT/protective layouts are blocked;
5. exact original 1024 bytes are saved/read back as `HDDRAW.BIN` / `HDDRAW2.BIN`;
6. completed candidate master is revalidated;
7. exactly sectors 0-1 are written, flushed, read back and compared;
8. restart is mandatory.

APA's checksum is additive, not collision-resistant. It is supporting evidence, not proof of health.

### Forensic multi-header recovery — experimental

Broader topology recovery requires:

1. complete raw scan;
2. coherent candidate map with no blocking conflict/overlap state;
3. exact preview of every proposed `prev` / `next` change;
4. verified `HDDMETA.BIN` / `HDDMETA2.BIN` containing every touched original header;
5. source bytes unchanged since the scan;
6. non-master headers written before master LBA 0;
7. flush/read-back after each write;
8. full final touched-set reread;
9. mandatory restart after success or partial failure.

Checksum-corroborated exact topology corrections can meet the automatic-safe gate. High-confidence heuristic-only plans remain a stronger explicit expert path. A healthy map with zero patches does not authorize or perform any write.

## Recovery artifacts

Normal backup/rescue:

```text
<device>:/HDDMBR.BIN
<device>:/HDDMBR2.BIN
<device>:/HDDRESCUE.BIN
<device>:/HDDRESCUE2.BIN
```

Exceptional master recovery:

```text
<device>:/HDDRAW.BIN
<device>:/HDDRAW2.BIN
```

Forensic analysis/recovery:

```text
<device>:/FORENSIC.TXT
<device>:/HDDMETA.BIN
<device>:/HDDMETA2.BIN
```

Diagnostics:

```text
<device>:/HDDMAN.LOG
<device>:/BOOTCHAIN.TXT
```

Keep important copies off the PS2 as well. Metadata recovery is useful; negotiating with a physically dying drive that has embraced entropy is not a supported protocol.

## Preparing an MBR source

Put the stock unsigned MBR payload at the root of the selected device:

```text
mc0:/MBR.XIN      preferred
mc0:/MBR.XLF      compatibility fallback
mc1:/MBR.XIN
mc1:/MBR.XLF
mass:/MBR.XIN
mass:/MBR.XLF
```

If both names are present, `MBR.XIN` wins. USB has no MagicGate hardware, so an authentic PS2 memory card in `mc0` or `mc1` is required for signing.

## Recovering from an FHDB boot loop

For the classic stale-pointer boot loop, first make the console capable of reaching homebrew with the HDD connected. In Free McBoot Configurator enable:

```text
Configure OSDSYS Options
  -> Skip HDD Update Check = ON
```

If necessary, inspect the expected FMCB system folder for stale HDD launcher modules such as:

```text
hddload.irx
dev9.irx
atad.irx
```

Run the manager, create a verified backup, then disable the active pointer. Do not delete or reformat partitions merely to stop the ROM bootstrap.

## Guarded physical-HDD fault injection

`tools/hardware_fault_injector.py` is a validation helper for a **sacrificial or fully backed-up disk**. It is intentionally not a general raw-sector editor.

Current scenarios include:

```text
master-magic-1bit
next-1bit
next-2bit
```

Physical-drive mutation requires a fresh master SHA-256 from `probe`, explicit `--apply`, and `--confirm-physical-write`. The tool saves the exact original header and manifest, flushes/read-backs the mutation, and refuses a restore if current bytes no longer match the expected mutated state.

See [`docs/HARDWARE_FAULT_INJECTION.md`](docs/HARDWARE_FAULT_INJECTION.md).

## Regression gates

`make test-host` executes the portable format, rescue, KELF, boot-chain, report, transaction, APA-repair, contextual-error and forensic suites.

The raw-HDD laboratories include:

- **30** deterministic sparse mounted-HDD fixtures with current postcondition matrix `4 no-repair / 6 guarded header-repair / 8 pointer-clear / 12 blocked`;
- **9** sparse 512 MiB forensic E2E HDD images;
- one-bit and exact two-bit stale-checksum topology cases;
- overlap/conflict/missing-master write gates;
- healthy chains beyond the old 512-node limit;
- hard read-only truncation beyond the current capacity;
- canonical empty-ID HDL subpartitions;
- direct-grid garbage rejection;
- dormant historical `__empty` coalescing regression;
- normal payload-first/pointer-last mutation tests;
- guarded fault-injector self-test.

Release builds use the pinned `ps2dev/ps2dev:v2.0.0` toolchain with `-Wall -Wextra -Werror`.

## Hardware-validation status

The 0.4 release line has been exercised on real PS2 hardware for startup, normal APA admission, hierarchical UI, action gating, themes/config, VBlank status rendering, diagnostics, backup/rescue evidence, healthy large-HDD forensic scanning and negative fail-closed paths.

The final healthy forensic scan used for release validation produced a single 100%-confidence active map with zero proposed patches and correctly retained eight dormant free-space remnants as non-active evidence.

The **experimental disclaimer remains specifically for destructive exceptional recovery**. More independent reports are wanted before those paths are considered broadly hardware-proven.

See [`docs/HARDWARE_VALIDATION_0.4.md`](docs/HARDWARE_VALIDATION_0.4.md).

## Build

With a normal PS2DEV/PS2SDK environment:

```sh
make
```

Host-only tests:

```sh
make test-host
```

GitHub Actions builds the stripped release ELF using `ps2dev/ps2dev:v2.0.0`, produces SHA-256, and publishes:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.4.1.zip
PS2_HDD_BOOTSTRAP_MANAGER-0.4.1.ELF
SHA256SUMS.txt
HDDMAN.CFG
```

The ZIP is the recommended download and contains the ELF, `HDDMAN.CFG`,
`SHA256SUMS.txt`, the MIT project license, PS2SDK's AFL-2.0 license and the
third-party notices together. Builds containing Spleen also include its
BSD-2-Clause license. Individual runtime assets remain available for targeted
downloads.

## Roadmap

0.4.x Michishirube is feature-frozen except for defects and narrowly scoped validation hardening.

The next feature train is **0.5.x Kakehashi**, focused on versioned recovery evidence and interoperability with host-side tooling such as PS2 DriveForge. Host tools may analyze and propose; the PS2 manager remains final write authority after re-reading and revalidating the physical disk.

See [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Project documents

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module ownership and write invariants.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — release train and product boundary with DriveForge.
- [`docs/HDD_FIXTURES.md`](docs/HDD_FIXTURES.md) — raw-HDD regression laboratories.
- [`docs/FORENSIC_RECOVERY.md`](docs/FORENSIC_RECOVERY.md) — forensic trust and repair model.
- [`docs/HARDWARE_VALIDATION_0.4.md`](docs/HARDWARE_VALIDATION_0.4.md) — physical validation record.
- [`docs/HARDWARE_FAULT_INJECTION.md`](docs/HARDWARE_FAULT_INJECTION.md) — guarded corruption/restore procedure.
- [`docs/GS_UI_0.4.md`](docs/GS_UI_0.4.md) — GS frontend and display-validation history.
- [`docs/VIDEO_MODES_AND_FONTS.md`](docs/VIDEO_MODES_AND_FONTS.md) — GS timing, viewport, scaling, font and licensing design.
- [`docs/GS_RENDERER_0.5.md`](docs/GS_RENDERER_0.5.md) — implemented scalable renderer and font pipeline.
- [`docs/PERFORMANCE_0.4X.md`](docs/PERFORMANCE_0.4X.md) — measured EE/GS optimization record and validation boundary.
- [`docs/STATUS_AND_ERRORS.md`](docs/STATUS_AND_ERRORS.md) — live telemetry and contextual error presentation.

## Credits

- **Hifu Himejima** — project author / hardware validation
- **PS2DEV / PS2SDK contributors** — EE/IOP toolchain and libraries
- **Frederic Cambus and Spleen contributors** — optional bitmap font
- reverse-engineering references and historical PS2 HDD tooling are credited in the source and project history where applicable

## License and third-party components

Original project source is licensed under the [MIT License](LICENSE), copyright
2026 Hifu Himejima (PunishedSnake).

PS2SDK libraries, embedded modules and the `msx` bitmap font retain the PS2SDK
Academic Free License 2.0. Spleen glyph data retains BSD-2-Clause. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
[`PS2SDK_LICENSE.txt`](PS2SDK_LICENSE.txt). Third-party font assets must carry
their own redistribution and embedding license; a convenient download link
and good intentions are not substitutes for permission.
