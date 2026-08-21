# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 ELF for inspecting, backing up, disabling, restoring, and installing the HDD OSD bootstrap stored in the APA `__mbr` partition.

It began after a real console got trapped in a post-uninstall FHDB boot loop: FHDB was gone, but the bootstrap pointer was still enabled, so the machine faithfully rebooted into software that no longer existed. Apparently uninstalling a program and persuading the console to stop launching it were separate premium features.

Version `0.3.1` **Torii** is a small compatibility update to the stable 0.3.x line. It prefers the Sony-style `MBR.XIN` filename for manual bootstrap installation while retaining `MBR.XLF` as a compatibility fallback for existing community installer layouts. Version 0.3.0 added the larger Torii features: a complete, verifiable backup of the active MBR payload and a read-only boot-chain inspector, plus persistent diagnostics on `mass:`, `mc0:`, or `mc1:`—because diagnosing a twenty-year-old encrypted boot path exclusively from photographs of a CRT was becoming a little too authentic.

## Why this exists

The PS2 ROM decides whether to launch an HDD update from two fields in the APA master header:

- `osdStart`: the starting sector of the signed HDD bootstrap program;
- `osdSize`: the program size in sectors.

If removal software deletes FHDB but leaves those fields populated, the ROM still sees an enabled update, attempts to execute a missing or unusable payload, resets, and repeats. The traditional recovery method is to disconnect the HDD, change a setting, or reach for increasingly archaeological utilities. The difficult part, naturally, was clearing two 32-bit values without treating the rest of the disk as acceptable collateral damage.

A 1024-byte header backup preserves the pointer, but it does not preserve the program to which that pointer refers. Version `0.3.0` therefore added a rescue capsule containing the APA master header, the exact sector-aligned active payload, metadata, and SHA-256 digests. Restoration writes and verifies the payload first and exposes it to the ROM only after that verification succeeds.

Manual installation has the opposite problem. Copying an MBR program to a disk is not enough: the KELF must be signed through the PS2 security hardware, written into the reserved `__mbr` payload area, verified, and only then exposed through the APA pointer. This manager turns those jobs into explicit guarded operations in one ELF.

## Release status

`0.3.1` **Torii** is the current stable release. The complete `0.2.0` manager—including selectable USB storage, standalone header backup, disable, restore, restart, shutdown, MagicGate signing, and guarded installation—was exercised successfully on real PlayStation 2 hardware.

Torii keeps the full-payload rescue and boot-chain inspection work stable while preserving the write-safety model. Portable SHA-256/capsule logic is covered by host-side tests and release builds are cross-compiled with the pinned PS2DEV v2.0.0 toolchain. Broader real-console coverage is still welcome; stable means the release gates are satisfied, not that twenty-year-old disks have suddenly become immortal. Keep irreplaceable data copied elsewhere.

## Development branch

The active `dev/0.4.0-michishirube` branch is refactoring internals behind regression-gated module boundaries while preserving Torii's normal write semantics. Read-only transport and diagnostics live in `hdd_read`, `boot_payload`, `boot_chain`, and the report modules; backup/rescue, source preparation, MagicGate signing, raw payload mechanics, and payload-first/pointer-last transaction ordering are isolated behind narrow interfaces. Michishirube now also has a deliberately separate **guarded APA master-header recovery path** for narrowly reconstructable sector-zero corruption: it saves an exact `HDDRAW*.BIN` snapshot first, accepts only a single canonical-field repair corroborated by the stale APA checksum, rejects checksum collisions/ambiguous damage/GPT, raw-writes exactly sectors 0-1, flushes, performs exact read-back, and requires restart. Pointer corruption and invalid active payloads continue to use the safer normal backup + `HDIOC_SETOSDMBR(0,0)` workflow instead of raw header repair. Host CI regenerates **30 deterministic sparse HDD images** covering valid, interrupted, corrupted-payload, pointer-boundary, checksum-collision, physical-bitflip, GPT/hybrid, and torn-header states; every image is run through parser policy and the repair matrix, with successful repair postconditions verified. `main.c` is intentionally being reduced toward application state, confirmation/UI, error presentation, and composition of these narrow interfaces. This work remains development-only until the new recovery boundary completes its real-HDD validation gate.

## Features

- Validates the complete 1024-byte APA `__mbr` header and checksum.
- Rejects hybrid APA/GPT layouts.
- Shows the current `osdStart` and `osdSize` values.
- Selects `mc0:`, `mc1:`, or `mass:` for backups, payloads, logs, and reports.
- Automatically selects the launch device when `argv[0]` begins with `mc0:`, `mc1:`, or `mass:`.
- Creates and verifies a legacy-compatible 1024-byte header backup without modifying the HDD.
- Creates a versioned full rescue capsule containing the header and exact active payload sectors.
- Protects both capsule parts with SHA-256 and verifies the saved file by reading it back.
- Restores a full capsule payload first, verifies every sector, and enables its pointer last.
- Disables only the bootstrap pointer through `HDIOC_SETOSDMBR`.
- Restores compatible `HDDMBR*.BIN` and old `FHDBMBR*.BIN` pointer backups.
- Structurally validates and MagicGate-signs a stock MBR KELF.
- Prefers `MBR.XIN` and falls back to `MBR.XLF` for compatibility with existing installer layouts.
- Writes signed payloads only inside the reserved `__mbr` payload area.
- Produces `HDDMAN.LOG` and a separate `BOOTCHAIN.TXT` report.
- Fingerprints both the sector image and unpadded KELF with SHA-256.
- Inspects FMCB `OSDSYS_Skip_HDD` (and the legacy `Skip_HDD` spelling), regional memory-card HDD modules, `__sysconf`, `__system`, OSDMenu configuration, and characteristic PSBBN partitions.
- Distinguishes probable FHDB, PSBBN/OSDMenu, HDD-OSD/HOSDMenu, custom OSDMenu, invalid, and unknown boot chains where the observable evidence permits it.
- Offers controlled power-off and restart to the PS2 Browser.

## What it deliberately does not do

The manager does not format a disk, create APA partitions, install the FHDB, PSBBN, or HDD-OSD file tree, copy applications, repair PFS filesystems, or decide which third-party distribution you should trust this week.

The `MBR.XIN`/`MBR.XLF` installation option installs only the signed MBR bootstrap program. The partitions and files expected by that program must already exist. Installing the bootstrap without its corresponding environment merely gives the ROM a beautifully verified way to launch something incomplete.

The family detector is evidence-based, not clairvoyant. Signed KELFs are encrypted; the manager validates their structure and correlates them with downstream files and configuration. `BOOTCHAIN.TXT` labels the result as probable and records the evidence used instead of inventing certainty from encrypted bytes.

## Safety model

The manager refuses an HDD-changing operation unless the relevant safety checks pass.

For every normal Torii-style write path:

1. `hdd0:` must report a valid APA disk.
2. The master header must contain the `APA`, `__mbr`, and Sony MBR signatures.
3. The APA checksum must be valid.
4. Hybrid APA/GPT layouts are rejected.
5. A complete 1024-byte current header backup must be saved and read back before the write.
6. The operation requires a distinct `L1 + R1 + action` confirmation chord.
7. The final APA pointer is read back and verified.

Full rescue restoration adds these rules:

1. The capsule metadata, size, flags, header digest, and payload digest must validate.
2. Its APA header must match the currently connected disk, excluding checksum and mutable pointer fields.
3. The payload range must fit the current `__mbr` partition.
4. The complete payload is written and compared before `osdStart`/`osdSize` are enabled.
5. A damaged or wrong-disk capsule blocks silent fallback to a pointer-only restore.

Installation additionally requires a structurally valid KELF no larger than 4 MiB, sufficient reserved capacity, successful console-side MagicGate signing, a full post-flush payload comparison, and pointer-last activation.

Normal install/restore/disable paths do not raw-write sectors 0 or 1: payload writes remain confined to the reserved bootstrap area and pointer updates use the standard `ps2hdd` driver. Michishirube's development recovery path is one explicit exception for a damaged master header that normal APA startup may reject. It can write exactly sectors 0-1 only after the portable planner proves a single canonical-field repair, the old checksum independently corroborates that exact correction, GPT/ambiguous states are rejected, an exact `HDDRAW*.BIN` snapshot is saved and verified, and a separate `L1 + R1 + START` confirmation is given. The write is flushed and read back byte-for-byte, then a restart is required.

## Preparing the files

Copy `PS2_HDD_BOOTSTRAP_MANAGER.ELF` somewhere your existing launcher can run it. Launching it from USB automatically selects `mass:`; launching it from a memory card automatically selects that card. You can change the destination later with `SELECT`.

For bootstrap installation, put the stock unsigned MBR payload at the root of the selected device. `MBR.XIN` is preferred; `MBR.XLF` remains accepted for compatibility with existing community installer layouts:

```text
mc0:/MBR.XIN      preferred
mc0:/MBR.XLF      compatibility fallback
mc1:/MBR.XIN
mc1:/MBR.XLF
mass:/MBR.XIN
mass:/MBR.XLF
```

If both names are present, `MBR.XIN` is selected. If the preferred file exists but cannot be opened or does not pass the existing KELF validation, the manager fails the operation rather than silently hiding that problem behind `MBR.XLF`. Do not rename an ordinary ELF to either filename; the manager checks the KELF container and rejects plain ELF files.

A genuine PS2 memory card is required for MagicGate signing. If `mass:` is selected, the manager asks whether `mc0` or `mc1` should perform the signing.

## Recovering from an FHDB boot loop

First enable this option in Free McBoot Configurator:

```text
Configure OSDSYS Options -> Skip HDD Update Check = ON
```

On some Free McBoot configurations, that option alone does not stop HDD loading. If the console still attempts to boot from the disk, the active FMCB card may load HDD support modules from its regional system folder before or independently of the OSDSYS setting.

With the HDD disconnected if necessary, use wLaunchELF to back up and then remove these files from the active FMCB folder:

```text
mc0:/BIEXEC-SYSTEM/hddload.irx
mc0:/BIEXEC-SYSTEM/dev9.irx
mc0:/BIEXEC-SYSTEM/atad.irx
```

Use `mc1:` for a card in slot 2. Cross-model cards may instead use `BAEXEC-SYSTEM`, `BEEXEC-SYSTEM`, or `BCEXEC-SYSTEM`; `BOOTCHAIN.TXT` checks every known regional folder and lists each module it finds. These are memory-card files, not HDD files. Keep copies so they can be restored later.

Then cold-boot with the HDD attached, launch the manager, confirm that the APA header is valid, select a backup destination, press `X`, review the paths, and confirm with `L1 + R1 + X`. Do not reset or remove power while a write or verification is in progress.

## Inspecting the boot chain

Press `R1` from the main menu. The scan is read-only and saves two files to the selected device:

```text
<device>:/HDDMAN.LOG
<device>:/BOOTCHAIN.TXT
```

`BOOTCHAIN.TXT` is replaced with the latest complete scan. It records:

- ROMVER and the expected regional FMCB folder;
- APA pointer state and bounds-check results;
- sector-image and unpadded-KELF SHA-256 fingerprints;
- KELF structural validation;
- the probable bootstrap family, confidence, and next stage;
- `OSDSYS_Skip_HDD`/`Skip_HDD` values found on `mc0:`, `mc1:`, and `mass:`;
- exact `hddload.irx`, `dev9.irx`, and `atad.irx` locations;
- FHDB, OSDMenu, PSBBN, HOSDMenu, HDD-OSD, and legacy downstream evidence.

`HDDMAN.LOG` is an ordered session log for initialization, scans, backups, validation failures, and write results. New entries are appended; a log at or above 128 KiB is replaced on the next flush. USB writes receive a short retry window while `mass:` finishes mounting.

## Backups and rescue capsules

Press `START` at any time. No HDD data is modified.

The manager first saves the current APA master header under the first available name:

```text
<device>:/HDDMBR.BIN
<device>:/HDDMBR2.BIN
```

It then saves a versioned rescue capsule under the first available name:

```text
<device>:/HDDRESCUE.BIN
<device>:/HDDRESCUE2.BIN
```

When the pointer is enabled, the capsule includes the exact sector-aligned active payload. When it is disabled, the capsule is header-only and documents that state. Existing differing files are preserved rather than overwritten. Keep another copy on a PC; the manager can preserve bytes, but it cannot negotiate with a dying disk or a USB stick that has embraced entropy.

The on-disk capsule format is documented in [`docs/RESCUE_FORMAT.md`](docs/RESCUE_FORMAT.md).

## Restoring a bootstrap

Restoration is available while the current pointer is disabled.

Press `SQUARE`. The manager searches `HDDRESCUE.BIN` and `HDDRESCUE2.BIN` first. A valid same-disk full capsule restores and verifies the payload before enabling its saved pointer. If no usable full capsule exists, the manager can fall back to a compatible pointer-only header backup:

```text
HDDMBR.BIN
HDDMBR2.BIN
FHDBMBR.BIN
FHDBMBR2.BIN
```

The legacy fallback cannot prove that the referenced payload sectors still contain the original program, so it checks the saved range, creates a fresh safety backup, and states clearly that only the pointer is being restored. A corrupted or wrong-disk rescue capsule is reported instead of being quietly ignored.

## Installing a bootstrap payload

This is intended for a manual FHDB or HDD-OSD setup whose required partitions and files already exist.

1. Ensure the current pointer is disabled.
2. Put the correct stock `MBR.XIN` at the root of the selected device, or use `MBR.XLF` for compatibility with an existing installer layout.
3. Press `CIRCLE`.
4. Select a signing memory card if the source is `mass:`.
5. Review the source, backup, target sector, byte size, and sector count.
6. Hold `L1 + R1` and press `CIRCLE`.
7. Wait until the payload and pointer both report successful verification.

After a successful installation the manager refreshes the boot-chain report and attempts to create a full rescue capsule of the newly installed state.

## Controls

| Context | Input | Action |
|---|---|---|
| Main menu | `START` | Save and verify the current header and rescue capsule without changing the HDD |
| Main menu | `R1` | Run the read-only boot-chain scan and save both report files |
| Main menu | `SELECT` | Choose `mc0`, `mc1`, or `mass` |
| Enabled bootstrap | `X` | Back up and prepare to disable |
| Disable confirmation | `L1 + R1 + X` | Clear the active pointer |
| Disabled bootstrap | `SQUARE` | Load a full rescue capsule or legacy pointer backup |
| Restore confirmation | `L1 + R1 + SQUARE` | Restore payload/pointer according to the selected backup type |
| Disabled bootstrap | `CIRCLE` | Load, sign, and prepare to install `MBR.XIN` or compatible `MBR.XLF` |
| Install confirmation | `L1 + R1 + CIRCLE` | Write, verify, and enable the payload |
| Startup recovery | `L1 + R1 + START` | Confirm a planner-approved raw APA master-header repair after snapshot creation |
| Main menu | `TRIANGLE` | Open the power/restart menu |
| Confirmation screens | `TRIANGLE` | Cancel without the pending write |

## Technical details

The ELF embeds its PS2SDK IOP dependencies, including fileXio, free memory-card drivers, `secrman`/`secrsif`, BDM/FatFs USB storage, poweroff, DEV9, ATA, APA HDD, and read-only PFS support.

The primary device calls are:

- `HDIOC_READSECTOR` (`0x6836`) for the APA header and payload reads;
- `HDIOC_WRITESECTOR` (`0x6837`) for the reserved payload area and, only in Michishirube's separately gated recovery path, an exact two-sector master-header repair;
- `HDIOC_SETOSDMBR` (`0x6833`) for normal `osdStart` and `osdSize` changes;
- `HDIOC_FLUSH` (`0x4804`) before verification;
- `SecrDownloadFile()` through `secrman`/`secrsif` for KELF signing;
- read-only `pfs0:` mounts of `__sysconf` and `__system` for diagnostics;
- `ExecOSD("BootBrowser")` for restart.

Version 0.3.1 adds `src/mbr_compat.c`, a narrow GNU ld `--wrap=fileXioOpen` compatibility layer. It only intercepts attempts to open a path ending in `/MBR.XLF`; when a sibling `MBR.XIN` exists, that file is opened instead. This keeps the established 0.3.0 installation state machine and all HDD write ordering unchanged.

The source layout and write-order invariants are documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). The complete synthetic raw-HDD state, mutation, and repair-policy contract is documented in [`docs/HDD_FIXTURES.md`](docs/HDD_FIXTURES.md). In particular, the conservative two-sector raw HDD transfer size is intentional and must not be increased without measuring and testing the fileXio/IOP path on hardware.

## Hardware validation

The original disable workflow eliminated a real post-uninstall FHDB boot loop on:

- PlayStation 2 FAT `SCPH-50000` (Japanese model);
- MechaPWN-enabled console;
- cross-model Free McBoot memory card;
- standard non-GPT APA HDD.

The console subsequently cold-booted with the HDD connected. The final `0.2.0` workflow was also reported working on the same hardware, including selectable `mass:` storage and standalone byte-for-byte backup. The full rescue capsule and expanded boot-chain inspector are stable in the Torii 0.3.x line; additional console, adapter, and HDD combinations remain useful validation coverage. The new Michishirube raw master-header recovery path has host and R5900 CI coverage but still requires its own physical-HDD validation before release.

## Building and testing

Portable tests do not require PS2SDK. `make test-host` regenerates all **30** synthetic HDD fixtures, validates their parser/bounds/KELF expectations, runs byte-level disk mutation tests, and executes the complete repair-policy matrix with successful repair postconditions:

```sh
make test-host
```

For the PS2 ELF, use PS2DEV/PS2SDK or the same pinned container as CI:

```sh
docker run --rm -v "$PWD:/work" -w /work ps2dev/ps2dev:v2.0.0 \
  sh -c 'apk add --no-cache make >/dev/null && make clean && make release'
```

The resulting file is `PS2_HDD_BOOTSTRAP_MANAGER.ELF`. Stable `0.3.x` releases are built with the pinned PS2DEV v2.0.0 container. GitHub release assets include the ELF and `SHA256SUMS.txt`; generated binaries are deliberately not tracked in the source tree.

## Project documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — safety invariants, EE/IOP boundaries, and optimization policy.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — release codenames and planned engineering work.
- [`docs/RESCUE_FORMAT.md`](docs/RESCUE_FORMAT.md) — stable on-disk rescue capsule format.
- [`docs/HDD_FIXTURES.md`](docs/HDD_FIXTURES.md) — 30 synthetic raw-HDD scenarios, interrupted states, mutation tests, repair matrix, and checksum-collision policy.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — build, test, and review rules for changes that can touch an HDD.

## License

The application source is released under the MIT License. Embedded PS2SDK modules retain the Academic Free License 2.0 terms included in `PS2SDK_LICENSE.txt`.

## Acknowledgements

- PS2DEV and PS2SDK contributors for the APA, PFS, USB, security, and console services.
- Free McBoot/FHDB contributors for the original signing and MBR installation workflow.
- OSDMenu, HDD-OSD, and PSBBN preservation contributors whose documented layouts make evidence-based identification possible.
- **Berion (PSX-Place)** for pointing out the original Sony-style `MBR.XIN` naming and the historical `MBR.XLF` installer convention.
- Hifu Himejima for reproducing the failure, preserving the disk header, testing on real hardware, and being that one gloriously unhinged developer who decided to correct this great injustice.