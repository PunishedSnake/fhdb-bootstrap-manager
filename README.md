# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 ELF for inspecting, backing up, disabling, restoring, installing, diagnosing, and recovering the HDD OSD bootstrap and APA metadata without formatting the disk.

It began after a real console got trapped in a post-uninstall FHDB boot loop: FHDB was gone, but the bootstrap pointer was still enabled, so the machine faithfully rebooted into software that no longer existed. Apparently uninstalling a program and persuading the console to stop launching it were separate premium features.

## Release status

`0.3.1` **Torii** is the current stable release. It keeps the hardware-proven normal pointer workflow, full rescue capsules, boot-chain diagnostics, guarded MagicGate installation, payload-first/pointer-last writes, and Sony-style `MBR.XIN` preference with `MBR.XLF` compatibility.

`0.4.x` **Michishirube** is the active development line. Its purpose is to turn the project into a modular recovery toolkit while preserving Torii's normal write semantics. Michishirube now includes a portable forensic APA graph engine, degraded read-only reconstruction, candidate-map inspection, guarded multi-header topology repair, hierarchical UI, controller activity indication, guarded physical-HDD fault injection for validation, and startup-phase telemetry.

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
- shared navigation/presentation/lifecycle — `manager_menu_ps2`, `app_ui_ps2`, and `platform`;
- `main.c` — startup, normal APA admission, lightweight pending diagnostics state, and hand-off to the manager dashboard. Full PFS/MC boot-chain diagnostics are deferred until requested so they do not block ordinary startup.

Portable recovery code decides what evidence means. PS2 adapters perform narrow I/O. Controllers authorize operations and present them. The composition root does not contain disk algorithms or raw write loops.

Full ownership and trust boundaries are documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

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

## What it deliberately does not do

The manager does not format a disk, create arbitrary APA partitions, recreate deleted games/files, or manufacture missing filesystem data.

Forensic reconstruction is about **metadata and topology**. It can infer where intact partitions probably are when redundant APA headers survive. It cannot recreate PFS inodes, directory blocks, game data, or other sectors that are physically gone.

A candidate map is not automatically considered healthy. Read-only reconstruction and write authorization are separate trust levels.

## UI model

Michishirube no longer assigns one feature to every spare DualShock button. The main screen is a dashboard with five sections:

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

Unavailable operations remain visible with an explanation instead of disappearing. Confirmation chords are still used for destructive operations, but feature discovery and navigation no longer consume `R1`, `L2`, `START`, `SELECT`, etc. as global one-action shortcuts.

### Bootstrap

Contains backup/disable, restore, and sign/install operations. Actions that conflict with the current pointer state are shown disabled with a reason.

### Diagnostics

Runs boot-chain inspection and saves reports through the selected external storage target. In Michishirube development builds this heavy PFS/MC evidence pass is deliberately lazy rather than part of ordinary startup.

### Recovery

Contains two deliberately different trust paths:

1. **Deterministic structure health / repair** — canonical master repair and safe pointer-clear recommendations.
2. **APA forensic / degraded read-only** — raw scan, candidate maps, shadow-map browsing, `FORENSIC.TXT`, and separately authorized topology repair.

### Backup & Storage

Creates full rescue data and selects `mc0:`, `mc1:`, or `mass:`.

### System

Controlled restart/power actions and application information.

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
Configure OSDSYS Options -> Skip HDD Update Check = ON
```

Some FMCB layouts also load HDD modules from their regional system directory. If necessary, back up and temporarily remove the relevant `hddload.irx`, `dev9.irx`, and `atad.irx` from the active `BIEXEC-SYSTEM`, `BAEXEC-SYSTEM`, `BEEXEC-SYSTEM`, or `BCEXEC-SYSTEM` folder.

Once the manager starts, use the **Bootstrap** section for the ordinary stale-pointer workflow. If the master itself is damaged but raw-readable, startup may offer guarded single-master recovery before normal admission. Heavier corruption belongs in **Recovery -> APA forensic / degraded read-only**.

## Technical details

The ELF embeds the required PS2SDK IOP dependencies for fileXio, memory cards, MagicGate, USB mass storage, power, DEV9/ATA, APA HDD, and read-only PFS diagnostics.

Important device operations include:

- `HDIOC_READSECTOR` (`0x6836`) — raw APA/payload/forensic reads;
- `HDIOC_WRITESECTOR` (`0x6837`) — normal reserved bootstrap payload writes and separately gated recovery metadata writes;
- `HDIOC_SETOSDMBR` (`0x6833`) — normal OSD pointer changes;
- `HDIOC_FLUSH` (`0x4804`) — durability boundary before read-back verification;
- `SecrDownloadFile()` — console-side KELF signing;
- read-only `pfs0:` mounts — downstream diagnostics;
- `padSetMainMode()` — best-effort ANALOG lamp/activity state through controller mode switching;
- `ExecOSD("BootBrowser")` — controlled restart.

The normal bootstrap writer retains the conservative two-sector raw transfer size. Forensic metadata repair writes one APA header at a time and verifies every write.

## Testing

Portable tests do not require PS2SDK:

```sh
make test-host
python3 tools/hardware_fault_injector.py selftest
```

The host suite includes:

- APA/capsule/SHA-256 format tests;
- conservative master repair policy;
- portable forensic graph reconstruction;
- healthy, broken-link, heuristic-only, off-grid subpartition, missing-master write-gate, and exact two-bit link-corruption cases;
- boot-chain/payload/report/KELF suites;
- bootstrap transaction ordering/failure injection;
- rescue validation;
- **30 deterministic sparse HDD fixtures** for parser/bounds/KELF and mounted repair policy;
- **9 sparse 512 MiB raw-HDD forensic E2E fixtures** using production `apa_forensic_scan()` at realistic APA grid LBAs;
- byte-level mutation tests for normal payload and pointer ordering;
- guarded hardware fault-injector self-test for one-bit master, one-bit link, exact two-bit link, verified write, and exact restoration.

For the PS2 ELF, use PS2DEV/PS2SDK or the pinned CI container:

```sh
docker run --rm -v "$PWD:/work" -w /work ps2dev/ps2dev:v2.0.0 \
  sh -c 'apk add --no-cache make >/dev/null && make clean && make release'
```

Generated binaries and synthetic HDD images are not tracked in the source tree.

## Hardware validation

The original pointer-disable workflow eliminated a real post-uninstall FHDB boot loop on PlayStation 2 hardware and Torii preserves that normal write behavior.

Michishirube now also has an initial healthy-disk hardware pass with mutually consistent `HDDMBR.BIN`, `HDDRESCUE.BIN`, `BOOTCHAIN.TXT`, and `HDDMAN.LOG` evidence. See [`docs/HARDWARE_VALIDATION_0.4.md`](docs/HARDWARE_VALIDATION_0.4.md).

Controlled destructive validation must use [`docs/HARDWARE_FAULT_INJECTION.md`](docs/HARDWARE_FAULT_INJECTION.md). The fault injector defaults to dry-run and requires a fresh master SHA plus extra confirmation before a physical-drive write.

The following Michishirube additions remain **not yet fully hardware-proven**:

- exceptional raw sectors 0-1 master repair after deliberately injected corruption;
- raw forensic scanning across the test HDD compared with a known host-side topology;
- multi-header topology repair and master-last commit behavior;
- `HDDMETA` snapshot/write/read-back behavior during destructive recovery;
- ANALOG-lamp activity switching across original and third-party controllers;
- the fast-start/timing change that defers full PFS/MC boot-chain diagnostics;
- controlled interruption/power-loss boundaries.

Host tests cannot prove DEV9/ATA timing, DMA/fileXio behavior, cache durability, APA journaling, adapter quirks, or exact physical power-loss effects. Physical-HDD validation remains the final gate before these paths can be treated as release-ready.

## Project documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module ownership, trust ladder, normal and recovery write invariants.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — release codenames, scope boundaries, and engineering milestones.
- [`docs/FORENSIC_RECOVERY.md`](docs/FORENSIC_RECOVERY.md) — forensic evidence model, shadow maps, `HDDMETA`, confidence and write authorization.
- [`docs/HDD_FIXTURES.md`](docs/HDD_FIXTURES.md) — 30 mounted/deterministic raw images plus 9 full 512 MiB forensic regressions.
- [`docs/HARDWARE_VALIDATION_0.4.md`](docs/HARDWARE_VALIDATION_0.4.md) — current real-console evidence and hardware gaps.
- [`docs/HARDWARE_FAULT_INJECTION.md`](docs/HARDWARE_FAULT_INJECTION.md) — guarded controlled-corruption procedure for sacrificial/backed-up HDDs.
- [`docs/RESCUE_FORMAT.md`](docs/RESCUE_FORMAT.md) — full rescue capsule format and restore rules.
- [`docs/KELF_FORMAT.md`](docs/KELF_FORMAT.md) — KELF validation/signing assumptions.
- [`docs/BOOT_REPORT.md`](docs/BOOT_REPORT.md) — boot-chain evidence and report semantics.

## Current development warning

`0.4.0-dev` is a hardware-validation build, not a stable release. The normal Torii-compatible write paths retain their existing contract, but Michishirube's exceptional raw recovery paths remain experimental until the destructive validation matrix is complete.

Current final software gate: branch head `c8359422b17cf621497f8bea28d65ca330cd163d`, **CI #323 / run `32495714384`**, host tests + fault-injector self-test + PS2DEV R5900 build all PASS.

Kitsune ahead. Bring backups.
