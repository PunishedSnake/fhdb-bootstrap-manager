# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 ELF for inspecting, backing up, disabling, restoring, installing, diagnosing, and recovering the HDD OSD bootstrap and APA metadata without formatting the disk.

It began after a real console got trapped in a post-uninstall FHDB boot loop: FHDB was gone, but the bootstrap pointer was still enabled, so the machine faithfully rebooted into software that no longer existed. Apparently uninstalling a program and persuading the console to stop launching it were separate premium features.

## Release status

`0.3.1` **Torii** is the current stable release. It keeps the hardware-proven normal pointer workflow, full rescue capsules, boot-chain diagnostics, guarded MagicGate installation, payload-first/pointer-last writes, and Sony-style `MBR.XIN` preference with `MBR.XLF` compatibility.

`0.4.x` **Michishirube** is the active development line. Its purpose is to turn the project into a modular recovery toolkit while preserving Torii's normal write semantics. Michishirube now includes a portable forensic APA graph engine, degraded read-only reconstruction, candidate-map inspection, guarded multi-header topology repair, hierarchical UI, controller activity indication, guarded physical-HDD fault injection for validation, startup-phase telemetry, and an application-wide Graphics Synthesizer frontend.

## What the manager understands

The PS2 ROM decides whether to launch an HDD update from two fields in the APA master header:

- `osdStart` — starting sector of the signed HDD bootstrap program;
- `osdSize` — program size in sectors.

If uninstall software removes FHDB but leaves those fields populated, the ROM can repeatedly launch a missing or unusable payload. The manager can back up the current state, clear only that pointer, restore a verified payload, or sign/install a replacement MBR program without touching unrelated partition contents.

A 1024-byte header backup preserves metadata but not the program referenced by the OSD pointer. Full rescue capsules therefore contain the APA master header, the exact sector-aligned active payload, metadata, and SHA-256 digests. Restoration writes and verifies the payload first and exposes it through `osdStart`/`osdSize` only afterward.

## Michishirube architecture

The development branch is explicitly layered:

- portable policy/format core — `apa`, `apa_repair`, `apa_forensic`, `repair_health`, `hdd_bounds`, `kelf`, `bootstrap_transaction`, rescue/report formats and SHA-256;
- PS2 device/service adapters — `hdd_read`, normal `hdd_write`, exceptional `hdd_repair_ps2`, multi-header `hdd_forensic_repair_ps2`, backup/rescue/forensic snapshot storage, MagicGate, diagnostics acquisition and persistence;
- application controllers — `bootstrap_controller_ps2`, `diagnostics_controller_ps2`, `repair_controller_ps2`, `forensic_controller_ps2`;
- shared navigation/presentation/lifecycle — `manager_menu_ps2`, `app_ui_ps2`, `gs_ui_ps2`, `disk_status_ps2`, and `platform`;
- `main.c` — startup, normal APA admission, lightweight pending diagnostics state, and hand-off to the manager dashboard. Full PFS/MC boot-chain diagnostics are deferred until requested so they do not block ordinary startup.

Portable recovery code decides what evidence means. PS2 adapters perform narrow I/O. Controllers authorize operations and present them. The composition root does not contain disk algorithms or raw write loops.

### Graphics Synthesizer frontend

Michishirube's development UI is rendered by one application-wide GS frontend rather than mixing libdebug text screens with a separate status overlay.

Physical validation established the display contract now used by the branch:

- `init_scr()` is retained only as the hardware-proven CRT/read-circuit bootstrap;
- the visible framebuffer remains at VRAM address 0 in the proven 640x224 FIELD drawing space;
- `gs_ui_ps2` renders every normal application pixel through libdraw/GIF DMA and does not reprogram the proven display mode;
- `draw_setup_environment()` uses the standard libdraw coordinate environment (`XYOFFSET = 2048,2048`);
- the built-in PS2SDK MSX glyph data is converted once into a 128x64 RGBA atlas and uploaded once to VRAM;
- every glyph is now rendered 8x8 source -> 8x8 destination in native 640x224 coordinates with nearest sampling — there is no 448->224 fractional Y transform;
- menu cards, panels, selection/disabled states, outlines and progress bars are ordinary GS primitives;
- frames are submitted as GIF DMA packets through two alternating EE packet buffers;
- live operation telemetry remains event-driven and unthrottled by presentation code.

The hardware history matters. The first mixed-renderer HUD was displaced into the lower-right because the code added a second coordinate compensation on top of libdraw's existing GS bias. A later standalone `graph_initialize(640x448)` path produced a black screen on the real console. Returning to the proven CRT bootstrap fixed video output and full-screen placement, but the first full-screen build still compressed a virtual 448-line UI into the 224-line field raster; physical photographs showed missing/stretched glyph rows. The current renderer therefore authors the entire UI directly in native 640x224 coordinates.

Existing controller screens that still construct text incrementally with `scr_clear()` / `scr_printf()` are intercepted by linker wrappers and `gs_debug_compat_ps2`, then rendered through the same GS frontend. Real libdebug drawing remains available only as a last-resort renderer-init failure screen.

Full ownership and physical validation history are documented in [`docs/GS_UI_0.4.md`](docs/GS_UI_0.4.md).

### Themes and UI config

Michishirube includes four predefined palettes that keep the same layout and safety colors:

- `aqua` — default cyan/blue;
- `amber` — warm service-console palette;
- `sakura` — pink/violet accent palette;
- `mono` — neutral grayscale/high-compatibility palette.

Themes can be changed live through **System -> UI theme** without restarting. The preference is stored in:

```text
MICHISHIRUBE.CFG
```

with the intentionally tiny format:

```text
theme=aqua
```

When the launcher supplies a useful `argv[0]`, the manager reads/writes that file beside the ELF. If no usable launch directory is available, it falls back to the selected backup/report storage root. Missing or unwritable config never blocks the manager; the chosen palette remains active for the current session. CI artifacts ship with a default `MICHISHIRUBE.CFG` beside the ELF.

## Main features

- Validates the complete 1024-byte APA `__mbr` master header and checksum.
- Rejects hybrid/protective APA/GPT layouts on write-capable paths.
- Creates non-overwriting verified header backups and full rescue capsules.
- Restores full payloads before exposing their OSD pointer.
- Disables only the HDD bootstrap pointer through `HDIOC_SETOSDMBR(0,0)`.
- Restores compatible `HDDMBR*.BIN` / legacy `FHDBMBR*.BIN` pointer backups.
- Structurally validates and MagicGate-signs stock MBR KELFs.
- Prefers `MBR.XIN` and accepts historical `MBR.XLF` as a compatibility name.
- Writes normal bootstrap payloads only inside the reserved `__mbr` program area beginning at sector `0x2000`.
- Produces `HDDMAN.LOG` and `BOOTCHAIN.TXT` diagnostics.
- Fingerprints sector images and unpadded KELFs with SHA-256.
- Inspects FMCB HDD-skip settings/modules and characteristic FHDB, OSDMenu, PSBBN, HOSDMenu, HDD-OSD, and custom downstream evidence.
- Provides deterministic mounted-disk structure health/repair policy through portable `repair_health`.
- Provides narrowly proven exceptional APA master recovery with exact `HDDRAW*.BIN` snapshots.
- Provides raw read-only APA forensic reconstruction independent of normal `ps2hdd` admission.
- Builds forward-link, reverse-link, and geometry candidate maps with explicit confidence/evidence.
- Allows read-only browsing of a reconstructed shadow APA map without pretending the physical disk is healthy.
- Exports `FORENSIC.TXT` with discovered headers, geometry, links, checksums, candidate maps, confidence, and proposed topology changes.
- Builds guarded multi-header repair plans limited to reconstructable `prev`/`next` topology and checksum changes.
- Saves every touched original header in a SHA-256-protected `HDDMETA*.BIN` snapshot before topology repair.
- Commits non-master headers first and the LBA-0 master last, with flush/read-back verification and a mandatory restart.
- Tracks one/two-bit link changes explicitly; stale checksum plus independent graph evidence can corroborate an exact two-bit repair.
- Includes `tools/hardware_fault_injector.py`, a deliberately gated host test utility for controlled master/link corruption on sacrificial or fully backed-up HDDs.
- Records per-phase pre-dashboard startup timing in `HDDMAN.LOG`.
- Renders live operation/action/location/I/O/LBA/progress state throughout startup, diagnostics, backup, install/restore/disable, deterministic recovery and forensic recovery.

## What it deliberately does not do

The manager does not format a disk, create arbitrary APA partitions, recreate deleted games/files, or manufacture missing filesystem data.

Forensic reconstruction is about **metadata and topology**. It can infer where intact partitions probably are when redundant APA headers survive. It cannot recreate PFS inodes, directory blocks, game data, or other sectors that are physically gone.

A candidate map is not automatically considered healthy. Read-only reconstruction and write authorization are separate trust levels.

## UI model

Michishirube no longer assigns one feature to every spare DualShock button. The main screen is a GS-rendered dashboard with five sections:

```text
Bootstrap
Diagnostics
Recovery
Backup & Storage
System
```

Normal navigation is consistent:

```text
UP / DOWN   select
X           enter / execute
TRIANGLE    back
```

Unavailable operations remain visible because their reason is useful information. They now use a dedicated **LOCKED** visual state: muted full-row background, warning border/accent, dim text, explicit `LOCKED` marker, and the existing explanatory hint. They remain non-executable.

Confirmation chords are still used for destructive operations, but feature discovery and navigation no longer consume `R1`, `L2`, `START`, `SELECT`, etc. as global one-action shortcuts.

### Bootstrap

Contains backup/disable, restore, and sign/install operations. Actions that conflict with the current pointer state are shown locked with a reason.

### Diagnostics

Runs boot-chain inspection and saves reports through the selected external storage target. The live status view walks through ROM identity, active payload evidence, FMCB `Skip_HDD`, memory-card boot files, `__sysconf`, `__system`, classification and report persistence. This heavy PFS/MC evidence pass is deliberately lazy rather than part of ordinary startup.

### Recovery

Contains two deliberately different trust paths:

1. **Deterministic structure health / repair** — canonical master repair and safe pointer-clear recommendations.
2. **APA forensic / degraded read-only** — raw scan, candidate maps, shadow-map browsing, `FORENSIC.TXT`, and separately authorized topology repair.

### Backup & Storage

Creates full rescue data and selects `mc0:`, `mc1:`, or `mass:`.

### System

Controller/activity information, live UI-theme selection, and controlled restart/power actions.

## Live operation monitor

The status view is shared infrastructure, not a forensic-only widget. It can display:

```text
OPERATION   high-level workflow
ACTION      current phase
LOCATION    semantic region/device being inspected or modified
I/O         READ / WRITE / VERIFY / FLUSH / POINTER UPDATE / SCAN
PROGRESS    operation step or disk-relative position
SECTOR      exact physical LBA/range when a raw HDD command exists
```

Low-level HDD transport publishes exact LBA/ranges. Higher layers publish the reason for that access. As a result a raw read can be shown as e.g. **Boot-chain diagnostics -> active bootstrap payload -> reserved __mbr -> READ -> LBA ...** instead of the old context-free `Raw HDD activity / READ`.

Current instrumentation covers startup admission, diagnostics, header/rescue backup preparation, disable, legacy/full restore, MBR source validation, MagicGate signing, install, payload read/write/read-back, pointer update/read-back, deterministic health assessment, HDDRAW snapshot creation, exceptional master repair, HDDMETA snapshot creation, and multi-header forensic repair.

Non-HDD phases such as memory-card signing or writing evidence to external storage say so explicitly and do not invent a physical HDD sector. Presentation is deliberately unthrottled; if rapid event updates expose field tearing, synchronization should be fixed rather than hiding I/O events.

## Controller ANALOG lamp as activity indication

Michishirube includes a best-effort controller activity mode for supported DualShock-compatible pads.

The red **ANALOG** lamp is not an independently addressable LED. On a DualShock 2 it follows the controller's main digital/analog mode, so repeatedly blinking it would also repeatedly switch the input report mode. Michishirube therefore does **not** strobe the lamp.

Instead:

- idle manager state requests digital mode / lamp off;
- a long or storage-sensitive operation requests analog mode / lamp on;
- nested operations keep the activity state active until the outer operation finishes;
- the initial controller mode is restored before restart/power-off;
- if a pad does not support main-mode switching, the feature fails open to screen-only progress and does not block the operation.

The on-screen activity/progress display remains authoritative. ANALOG-lamp behavior still needs physical validation across original and third-party controllers before release.

## Safety model

### Normal Torii-compatible writes

Every normal HDD-changing path requires:

1. `ps2hdd` accepts the device as APA;
2. the current master passes APA/signature/checksum/non-hybrid validation;
3. a current 1024-byte header backup is saved and read back;
4. the specific operation receives explicit confirmation;
5. raw payload writes stay inside the reserved `__mbr` program area;
6. payload is flushed and compared before pointer exposure;
7. `osdStart`/`osdSize` changes go through `HDIOC_SETOSDMBR` and are read back.

Normal install/restore/disable paths do **not** raw-write sectors 0-1.

### Exceptional single-master recovery

A damaged master may prevent normal APA admission. Michishirube therefore keeps a separately gated startup recovery path:

1. raw sectors 0-1 must remain readable;
2. portable `apa_repair` must prove one narrowly reconstructable canonical field;
3. the stale checksum must corroborate exactly that correction;
4. checksum-valid ambiguous corruption, unexplained multiple changes, low identity, and GPT/protective layouts are blocked;
5. the exact original 1024 bytes are saved/read back as `HDDRAW.BIN` / `HDDRAW2.BIN`;
6. the completed candidate master is revalidated;
7. exactly sectors 0-1 are written, flushed, read back and compared;
8. success requires restart.

APA's checksum is additive rather than collision-resistant. A matching checksum is supporting evidence, not proof of health.

### Forensic multi-header recovery

Broader recovery uses a different contract:

1. raw scan discovers candidate headers without writing;
2. surviving `next`, `prev`, `main`, `subs[]`, geometry, bounds, checksums, and reciprocal links are combined into candidate maps;
3. multiple plausible maps may coexist and are shown explicitly;
4. read-only shadow-map browsing is allowed before any repair is authorized;
5. a write plan may alter only the topology fields it explicitly previews (`prev`, `next`, checksum);
6. every touched original 1024-byte header is preserved in `HDDMETA.BIN` / `HDDMETA2.BIN` with per-header SHA-256 and a whole-snapshot digest;
7. immediately before each write, the current on-disk header must still match the bytes seen by the scan;
8. internal headers are committed and verified before the master at LBA 0;
9. every touched header is raw-read again after the transaction;
10. success or partial failure requires restart before any further HDD work.

A plan containing only checksum-corroborated exact topology corrections can satisfy the automatic-safe gate. A high-confidence plan with heuristic-only changes remains an explicit expert path and requires a stronger confirmation chord.

## Forensic evidence and two-bit recovery

For link corruption, the engine does not brute-force arbitrary disk bytes. It first obtains an expected `prev`/`next` value from the candidate graph. It then compares that expected value with the bytes currently stored and records the bit distance.

For a physical-style stale-checksum corruption, replacing the damaged link with the graph-derived value must restore the original stored APA checksum before the correction is considered checksum-corroborated. The portable regression suite contains a case with **exactly two flipped bits** in a link and requires all of the following:

- graph reconstruction chooses the correct neighbor;
- bit distance equals 2;
- the old checksum corroborates the exact correction;
- the candidate satisfies the automatic-safe repair gate.

Checksum-valid but semantically wrong links remain heuristic/manual because the checksum provides no independent evidence.

## Recovery artifacts

Normal header/full-rescue files include:

```text
<device>:/HDDMBR.BIN
<device>:/HDDMBR2.BIN
<device>:/HDDRESCUE.BIN
<device>:/HDDRESCUE2.BIN
```

Exceptional one-master recovery uses:

```text
<device>:/HDDRAW.BIN
<device>:/HDDRAW2.BIN
```

Forensic analysis and topology repair use:

```text
<device>:/FORENSIC.TXT
<device>:/HDDMETA.BIN
<device>:/HDDMETA2.BIN
```

`HDDMETA` is versioned recovery evidence, not a normal APA format. It contains the disk/plan metadata and exact pre-repair bytes for every header the plan intends to touch, protected by SHA-256.

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

If both names are present, `MBR.XIN` is selected. If the preferred file exists but cannot be opened or fails KELF validation, the operation fails instead of silently hiding it behind the fallback. USB has no MagicGate hardware, so an authentic PS2 memory card in `mc0` or `mc1` is required for signing.

## Recovering from an FHDB boot loop

For the classic stale-pointer boot loop, first make the console capable of reaching homebrew with the HDD connected. In Free McBoot Configurator enable:

```text
Configure OSDSYS Options
  -> Skip HDD Update Check = ON
```

If the machine still takes the HDD path, inspect the expected system folder on the memory card and remove stale HDD launcher modules such as:

```text
hddload.irx
dev9.irx
atad.irx
```

Run the manager, create a verified backup, then disable the active pointer. Do not delete or reformat partitions merely to stop the ROM bootstrap.

## Host regression gates

`make test-host` executes the portable format, KELF, report, transaction, rescue, APA-repair and forensic suites, plus two file-backed HDD laboratories.

The first laboratory contains **30 sparse 16 MiB images** covering healthy masters, malformed/corrupted APA identity fields, one-bit physical-style damage, pointer inconsistencies, GPT/protective signatures, interrupted payload/pointer sequences, damaged active payloads, and the current deterministic repair matrix:

- 4 no-repair;
- 6 guarded header-repair;
- 8 pointer-clear;
- 12 blocked.

The second laboratory contains **9 sparse 512 MiB forensic HDD images** whose headers live at realistic APA LBAs. It exercises healthy forward/reverse reconstruction, stale-checksum link repair, an exact two-bit link flip, checksum-valid wrong links, off-grid subpartitions, a missing master, overlap, two damaged headers, and conflict cases. Map deduplication is part of production behavior, so safety tests assert conflict/overlap invariants on whichever equivalent candidate survives rather than depending on a particular map label.

The host suite also mutation-tests the normal payload-first/pointer-last model and verifies that the pointer remains disabled whenever the payload write/verification stage is interrupted.

## Guarded physical-HDD fault injection

`tools/hardware_fault_injector.py` is the host-side validation helper for a **sacrificial or fully backed-up test disk**. It is intentionally not a general raw-sector editor.

Supported scenarios currently include:

```text
master-magic-1bit
next-1bit
next-2bit
```

Physical-drive mutation requires a fresh master SHA-256 from `probe`, explicit `--apply`, and `--confirm-physical-write`. The tool saves the exact original 1024-byte header plus a manifest before mutation, flushes and reads back the write, and will not restore from the manifest unless the current bytes match the expected mutated state or are already the saved original. Run one scenario at a time and return to a verified baseline before the next.

The full procedure is documented in [`docs/HARDWARE_FAULT_INJECTION.md`](docs/HARDWARE_FAULT_INJECTION.md).

## Current hardware-validation state

A healthy physical-PS2 pass has already established a useful baseline: normal APA admission succeeded; hierarchical menu navigation and state-dependent feature blocking behaved as intended; boot-chain diagnostics reported a disabled pointer with `$HOSDSYS`; standalone header/full-rescue backup succeeded; and the captured `HDDMBR.BIN` matched the master embedded in `HDDRESCUE.BIN` byte-for-byte.

The same pass exposed the first significant UX regression: startup took roughly 1–2 minutes because the manager automatically ran the full memory-card/PFS boot-chain scan before displaying the dashboard. Michishirube now defers that heavy scan until Diagnostics and logs per-phase startup timings (`iop`, `modules`, `services`, `pad`, `hdd_status`, `header`, `total`) so remaining hardware latency can be measured rather than guessed.

GS UI hardware work has also progressed through the mixed-renderer displacement, standalone-black-screen, and full-screen-but-font-scaled stages described above. The next display pass is specifically for the native 640x224 font raster, LOCKED state, theme/config behavior and operation-wide live status.

A separate sacrificial HDD is intended for destructive fault-injection validation so ordinary healthy-console testing does not share the same physical disk.

## 0.4 hardware gate

`0.4.x` remains a development line until physical testing establishes at least:

- native 640x224 GS font/UI readability on real displays;
- clear disabled/LOCKED state and theme/config behavior;
- live status coverage for startup/diagnostics/bootstrap/recovery paths;
- forensic scan consistency with healthy real-disk topology;
- verified master-header repair on expendable media;
- at least one full multi-header repair with byte-for-byte before/after evidence;
- snapshot/report behavior on the intended storage targets;
- restart behavior after exceptional raw repair;
- regression fixtures for every reproducible hardware discrepancy;
- targeted power-loss testing before any forensic write path is considered release-ready.

## Project documents

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module ownership, write invariants, forensic trust model and future recovery boundaries.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — release train, Michishirube hardware gate and post-0.4 scope guardrails.
- [`docs/HDD_FIXTURES.md`](docs/HDD_FIXTURES.md) — both raw-HDD regression laboratories and expected classifications.
- [`docs/FORENSIC_RECOVERY.md`](docs/FORENSIC_RECOVERY.md) — degraded read-only workflow, candidate maps, `HDDMETA`, authorization and rollback expectations.
- [`docs/HARDWARE_VALIDATION_0.4.md`](docs/HARDWARE_VALIDATION_0.4.md) — hardware-first validation procedure for the healthy physical HDD and later repair tests.
- [`docs/HARDWARE_FAULT_INJECTION.md`](docs/HARDWARE_FAULT_INJECTION.md) — guarded host-side corruption/restore procedure for sacrificial physical HDDs.
- [`docs/GS_UI_0.4.md`](docs/GS_UI_0.4.md) — full-screen GS frontend design and display-validation checklist.

## Build

A normal PS2DEV/PS2SDK environment can build the ELF with:

```sh
make
```

Host-only regression tests do not require PS2SDK:

```sh
make test-host
```

GitHub Actions builds the stripped PS2 ELF with `ps2dev/ps2dev:v2.0.0`, records SHA-256, and uploads the ELF, checksum, and default `MICHISHIRUBE.CFG` artifact.

## Credits

- **Hifu Himejima** — project author / hardware validation
- **OpenAI Codex / ChatGPT** — implementation assistance
- **PS2DEV / PS2SDK contributors** — underlying EE/IOP toolchain and libraries
- reverse-engineering references and historical PS2 HDD tooling are credited in the source and project history where applicable.
