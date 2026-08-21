# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 ELF for inspecting, backing up, disabling, restoring, installing, diagnosing, and recovering the HDD OSD bootstrap and APA metadata without formatting the disk.

It began after a real console got trapped in a post-uninstall FHDB boot loop: FHDB was gone, but the bootstrap pointer was still enabled, so the machine faithfully rebooted into software that no longer existed. Apparently uninstalling a program and persuading the console to stop launching it were separate premium features.

## Current release

**0.4.0 — Michishirube (道標)** is the current stable release.

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

## Important recovery disclaimer

**Read-only diagnostics, backups, forensic scanning, report generation, UI safety gates, and the normal bootstrap workflows have received substantial real-console validation. Exceptional raw metadata repair remains experimental in 0.4.0.**

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
- the PS2SDK MSX font is uploaded once as a texture atlas and rendered native 8x8 -> 8x8;
- menu cards, outlines, locked rows, progress bars and status panels are GS primitives;
- complete frames are drawn off-screen and swapped on VBlank through two
  framebuffers;
- remaining source-level `scr_clear()` / `scr_printf()` compatibility screens are intercepted and rendered through the same GS frontend;
- real libdebug drawing is retained only as a renderer-initialization emergency fallback.

Physical testing found and fixed the earlier mixed-renderer lower-right displacement, a standalone GS black screen, fractional-Y glyph corruption, and scan-time screen tearing.

The current 0.4.x development branch also exposes **System -> Video mode**. Its
experimental 480p option renders the manager into a 640x448 progressive
framebuffer for a steadier, clearer UI. Because the PS2 remains admirably
uninterested in negotiating modern display capabilities, the mode must be
confirmed with X within ten seconds. TRIANGLE, no input, or an internal setup
failure restores the physically proven native output. The choice is session-only
and is not written to `HDDMAN.CFG`.

### Themes and configuration

Available themes:

- `aqua` — default cyan/blue;
- `amber` — warm service-console palette;
- `sakura` — pink/violet accent palette;
- `mono` — neutral grayscale palette.

Themes can be changed live through **System -> UI theme**.

The stable config filename is:

```text
HDDMAN.CFG
```

Typical contents:

```text
theme=aqua
```

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
PS2_HDD_BOOTSTRAP_MANAGER-0.4.0.zip
PS2_HDD_BOOTSTRAP_MANAGER-0.4.0.ELF
SHA256SUMS.txt
HDDMAN.CFG
```

The ZIP is the recommended download and contains the ELF, `HDDMAN.CFG`, and `SHA256SUMS.txt` together. Individual assets remain available for targeted downloads.

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
- [`docs/STATUS_AND_ERRORS.md`](docs/STATUS_AND_ERRORS.md) — live telemetry and contextual error presentation.

## Credits

- **Hifu Himejima** — project author / hardware validation
- **OpenAI Codex / ChatGPT** — implementation assistance
- **PS2DEV / PS2SDK contributors** — EE/IOP toolchain and libraries
- reverse-engineering references and historical PS2 HDD tooling are credited in the source and project history where applicable
