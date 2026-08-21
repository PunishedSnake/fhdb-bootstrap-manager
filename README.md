# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 ELF for inspecting, backing up, disabling, restoring, installing, and—on the Michishirube development line—narrowly repairing the HDD OSD bootstrap and APA master metadata without formatting the disk.

It began after a real console got trapped in a post-uninstall FHDB boot loop: FHDB was gone, but the bootstrap pointer was still enabled, so the machine faithfully rebooted into software that no longer existed. Apparently uninstalling a program and persuading the console to stop launching it were separate premium features.

## Release status

`0.3.1` **Torii** is the current stable release. It keeps the complete rescue capsule, boot-chain diagnostics, guarded MagicGate installation, payload-first/pointer-last writes, and Sony-style `MBR.XIN` preference with `MBR.XLF` compatibility.

`0.4.x` **Michishirube** is the active development line. Its goal is not to turn the program into a disk editor with a hero complex; it is to split policy, device mechanics, controllers, UI, diagnostics, and recovery into testable boundaries while preserving Torii's proven normal write semantics.

## Why this exists

The PS2 ROM decides whether to launch an HDD update from two fields in the APA master header:

- `osdStart` — starting sector of the signed HDD bootstrap program;
- `osdSize` — program size in sectors.

If removal software deletes FHDB but leaves those fields populated, the ROM still sees an enabled update and can repeatedly launch a missing or unusable payload. The manager can back up the current state, clear only that pointer, restore a verified payload, or sign/install a replacement MBR program without touching partition contents.

A 1024-byte header backup preserves the pointer but not the program it references. Torii therefore added a rescue capsule containing the APA master header, exact sector-aligned active payload, metadata, and SHA-256 digests. Restoration writes and verifies the payload first and exposes it through `osdStart`/`osdSize` only afterward.

## Michishirube architecture

The development branch is now explicitly layered:

- portable policy/format core — `apa`, `apa_repair`, `repair_health`, `hdd_bounds`, `kelf`, `bootstrap_transaction`, rescue/boot-report formats and hashing;
- PS2 device/service adapters — `hdd_read`, `hdd_write`, `hdd_repair_ps2`, backup/rescue storage, MagicGate, diagnostics acquisition and persistence;
- application controllers — `bootstrap_controller_ps2`, `diagnostics_controller_ps2`, and `repair_controller_ps2`;
- shared presentation/lifecycle — `app_ui_ps2`;
- `main.c` — startup, normal APA admission, top-level menu state, and dispatch only.

The normal manager and exceptional recovery paths deliberately do not share authority merely because they both eventually touch the HDD. Full ownership and trust boundaries are documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Features

- Validates the complete 1024-byte APA `__mbr` master header and checksum.
- Rejects hybrid/protective APA/GPT layouts on write-capable paths.
- Shows `osdStart`, `osdSize`, detected bootstrap family, and report status.
- Selects `mc0:`, `mc1:`, or `mass:` for backups, rescue data, logs, reports, and raw recovery snapshots.
- Creates and verifies non-overwriting 1024-byte header backups.
- Creates versioned full rescue capsules containing the current header and exact active payload sectors.
- Protects rescue metadata/header/payload identity with SHA-256 and read-back verification.
- Restores a full payload before enabling its pointer.
- Disables only the HDD bootstrap pointer through `HDIOC_SETOSDMBR(0,0)`.
- Restores compatible `HDDMBR*.BIN` / legacy `FHDBMBR*.BIN` pointer backups.
- Structurally validates and MagicGate-signs stock MBR KELFs.
- Prefers `MBR.XIN` and accepts `MBR.XLF` as the historical compatibility name.
- Writes normal bootstrap payloads only inside the reserved `__mbr` program area beginning at sector `0x2000`.
- Produces `HDDMAN.LOG` and `BOOTCHAIN.TXT` diagnostics.
- Fingerprints sector images and unpadded KELFs with SHA-256.
- Inspects FMCB HDD-skip settings/modules and characteristic FHDB, OSDMenu, PSBBN, HOSDMenu, HDD-OSD, and custom downstream evidence.
- Provides `L2` **HDD structure health / repair** in the normal menu.
- On Michishirube only, provides a separately gated raw APA master recovery path for narrowly provable metadata corruption.

## What it deliberately does not do

The stable/normal manager does not format a disk, create arbitrary APA partitions, repair PFS contents, recreate deleted games/files, or invent missing filesystem data.

The current automatic raw master repair also does **not** guess arbitrary fields. It repairs only a single canonical master identity/anchor field when independent evidence proves the exact correction. Heavier reconstruction from multiple partition headers is documented as a future forensic/recovery direction, not silently enabled as a write path.

## Safety model

### Normal Torii-compatible writes

Every normal HDD-changing path requires:

1. `ps2hdd` accepts the device as APA;
2. the current master passes APA/signature/checksum/non-hybrid validation;
3. a current 1024-byte header backup is saved and read back;
4. the operation receives its explicit multi-button confirmation;
5. payload writes remain confined to the reserved `__mbr` program area;
6. a new/restored payload is flushed and compared before pointer exposure;
7. `osdStart`/`osdSize` changes go through `HDIOC_SETOSDMBR` and are read back.

Normal install/restore/disable paths do **not** raw-write sectors 0-1.

### Exceptional Michishirube master recovery

A damaged APA master may prevent the normal rules above from even starting. Michishirube therefore has one deliberately separate raw recovery contract:

1. the first `HDIOC_STATUS` is intercepted only once so readable raw sectors 0-1 can be inspected before normal admission fails;
2. portable `apa_repair` must prove exactly one narrowly reconstructable canonical field;
3. the source checksum must be mismatched and correcting only that field must restore the old stored checksum;
4. checksum-valid ambiguous corruption, multiple unexplained changes, insufficient APA identity, and GPT/protective layouts are blocked;
5. the exact damaged 1024 bytes are saved and verified as `HDDRAW.BIN` / `HDDRAW2.BIN`;
6. the user confirms with `L1 + R1 + START`;
7. the proposed repaired master is revalidated before the write;
8. exactly sectors 0-1 are written, flushed, read back, and compared byte-for-byte;
9. success requires restart so `ps2hdd` re-reads the repaired disk.

Pointer corruption, out-of-bounds OSD pointers, or an active invalid KELF do not use raw master repair. `repair_health` routes them to the ordinary backup + pointer-disable workflow.

APA's checksum is additive rather than collision-resistant. Two changes can cancel each other mathematically, so a matching checksum is never treated as proof that an otherwise suspicious master is healthy.

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

If both names are present, `MBR.XIN` is selected. If the preferred file exists but cannot be opened or fails KELF validation, the manager reports that failure instead of silently hiding it behind the fallback. USB has no MagicGate hardware, so an authentic PS2 memory card in `mc0` or `mc1` is required for signing.

## Recovering from an FHDB boot loop

For the classic stale-pointer boot loop, first make the console capable of reaching homebrew with the HDD connected. In Free McBoot Configurator enable:

```text
Configure OSDSYS Options -> Skip HDD Update Check = ON
```

Some FMCB layouts also load HDD modules from their regional system directory. If necessary, back up and temporarily remove the relevant `hddload.irx`, `dev9.irx`, and `atad.irx` from the active `BIEXEC-SYSTEM`, `BAEXEC-SYSTEM`, `BEEXEC-SYSTEM`, or `BCEXEC-SYSTEM` folder.

Once the manager starts with a normally valid APA disk, use `X` to create the mandatory backup and disable the pointer. If the master itself is damaged but remains raw-readable, Michishirube's startup recovery may instead present the guarded `HDDRAW*.BIN` repair flow before normal menu admission.

## Controls

| Context | Input | Action |
|---|---|---|
| Main menu | `START` | Save and verify current header + rescue capsule without changing the HDD |
| Main menu | `R1` | Run read-only boot-chain inspection and save reports |
| Main menu | `L2` | Open HDD structure health / repair |
| Main menu | `SELECT` | Choose `mc0`, `mc1`, or `mass` |
| Enabled bootstrap | `X` | Back up and prepare to disable |
| Disable confirmation | `L1 + R1 + X` | Clear the active OSD pointer |
| Disabled bootstrap | `SQUARE` | Load full rescue / legacy pointer backup |
| Restore confirmation | `L1 + R1 + SQUARE` | Restore according to selected backup type |
| Disabled bootstrap | `CIRCLE` | Load, sign, and prepare `MBR.XIN` / compatible `MBR.XLF` |
| Install confirmation | `L1 + R1 + CIRCLE` | Write, verify, and enable payload |
| Planner-approved master recovery | `L1 + R1 + START` | Confirm exceptional raw sectors 0-1 repair after snapshot creation |
| Main menu | `TRIANGLE` | Power / restart menu |
| Confirmation screens | `TRIANGLE` | Cancel pending operation |

## Diagnostics and backups

`R1` saves the current `HDDMAN.LOG` and `BOOTCHAIN.TXT`. The report records ROMVER, pointer state/bounds, payload/KELF hashes, structural KELF result, likely family/next stage, FMCB HDD settings/modules, and downstream filesystem evidence.

`START` creates the first safe non-overwriting header/rescue slots such as:

```text
<device>:/HDDMBR.BIN
<device>:/HDDMBR2.BIN
<device>:/HDDRESCUE.BIN
<device>:/HDDRESCUE2.BIN
```

Exceptional master recovery uses its own raw snapshot namespace:

```text
<device>:/HDDRAW.BIN
<device>:/HDDRAW2.BIN
```

Keep important copies off the PS2 as well. The manager can preserve metadata bytes; it cannot negotiate with a physically dying disk that has decided entropy is now a feature.

## Technical details

The ELF embeds the required PS2SDK IOP dependencies for fileXio, memory cards, MagicGate, USB mass storage, power, DEV9/ATA, APA HDD, and read-only PFS diagnostics.

Important device operations are:

- `HDIOC_READSECTOR` (`0x6836`) — raw APA/payload/recovery reads;
- `HDIOC_WRITESECTOR` (`0x6837`) — normal reserved bootstrap payload writes and, only through `hdd_repair_ps2`, exact two-sector master recovery;
- `HDIOC_SETOSDMBR` (`0x6833`) — normal OSD pointer changes;
- `HDIOC_FLUSH` (`0x4804`) — durability boundary before read-back verification;
- `SecrDownloadFile()` — console-side KELF signing;
- read-only `pfs0:` mounts — downstream diagnostics;
- `ExecOSD("BootBrowser")` — controlled restart.

The conservative two-sector raw transfer size is intentional and should not be increased without hardware measurement of the fileXio/IOP path.

## Testing

Portable tests do not require PS2SDK:

```sh
make test-host
```

The host suite regenerates **30 deterministic sparse HDD images**, validates APA/bounds/KELF behavior, executes MBR payload/pointer mutation tests, tests portable repair policy—including the additive-checksum collision regression—and runs all 30 images through repair postconditions.

For the PS2 ELF, use PS2DEV/PS2SDK or the same pinned container as CI:

```sh
docker run --rm -v "$PWD:/work" -w /work ps2dev/ps2dev:v2.0.0 \
  sh -c 'apk add --no-cache make >/dev/null && make clean && make release'
```

Generated binaries and synthetic HDD images are deliberately not tracked in the source tree.

## Hardware validation

The original pointer-disable workflow eliminated a real post-uninstall FHDB boot loop on PlayStation 2 hardware and Torii preserves that normal write behavior. The Michishirube raw master recovery path has host and R5900 build coverage but still requires dedicated physical-HDD validation before release.

Host tests cannot prove DEV9/ATA timing, DMA/fileXio behavior, cache durability, APA journaling, or exact physical power-loss effects.

## Project documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — current modular ownership, normal versus exceptional write invariants, and planned forensic/degraded recovery model.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — release codenames and engineering milestones.
- [`docs/RESCUE_FORMAT.md`](docs/RESCUE_FORMAT.md) — stable rescue capsule format.
- [`docs/HDD_FIXTURES.md`](docs/HDD_FIXTURES.md) — 30 synthetic raw-HDD scenarios, interruption/mutation coverage, repair matrix, and checksum-collision policy.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — build, test, and review rules for HDD-touching changes.

## License

The application source is released under the MIT License. Embedded PS2SDK modules retain the Academic Free License 2.0 terms included in `PS2SDK_LICENSE.txt`.

## Acknowledgements

- PS2DEV and PS2SDK contributors for APA, PFS, USB, security, and console services.
- Free McBoot/FHDB contributors for the original signing and MBR installation workflow.
- OSDMenu, HDD-OSD, and PSBBN preservation contributors whose documented layouts make evidence-based identification possible.
- **Berion (PSX-Place)** for identifying the Sony-style `MBR.XIN` naming and historical `MBR.XLF` installer convention.
- Hifu Himejima for reproducing the failure, preserving the disk header, testing on real hardware, and being that one gloriously unhinged developer who decided to correct this great injustice.
