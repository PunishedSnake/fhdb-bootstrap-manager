# PS2 HDD Bootstrap Manager

PS2 HDD Bootstrap Manager is a standalone PlayStation 2 ELF for inspecting, backing up, disabling, restoring, and installing the HDD OSD bootstrap stored in the APA `__mbr` partition.

It began as FHDB Bootstrap Manager after a real console got trapped in a post-uninstall boot loop: FHDB was gone, but the HDD bootstrap pointer was still enabled, so the PS2 kept trying to launch software that no longer existed. Apparently uninstalling a program and persuading the machine to stop booting it were separate premium features.

Version `0.2.0` broadens the tool into a general bootstrap manager. It can independently back up the current APA master header, select `mc0:`, `mc1:`, or `mass:` for storage, restore compatible old backups, return to the PS2 Browser or power off, and prepare and install a stock `MBR.XLF` for manual FHDB or HDD-OSD setups.

## Why this exists

The PS2 ROM decides whether to launch an HDD update from two fields in the APA master header:

- `osdStart`: the starting sector of the signed HDD bootstrap program;
- `osdSize`: the program size in sectors.

If removal software deletes FHDB but leaves those fields populated, the ROM still sees an enabled update, attempts to execute a missing or unusable payload, resets, and repeats. The traditional recovery method is to disconnect the HDD, change a setting, or reach for increasingly archaeological utilities. Naturally, the difficult part turned out to be clearing two 32-bit values without treating the rest of the disk as acceptable collateral damage.

Manual installation has the opposite problem. Copying an MBR program to a disk is not enough: the KELF/XLF must be signed through the PS2 security hardware, written into the reserved `__mbr` payload area, verified, and only then exposed to the ROM through the pointer. That procedure is perfectly reasonable if your preferred user interface is a collection of half-documented sector operations from different decades.

This manager turns both jobs into explicit, guarded operations in one ELF.

## Release status

The original disable workflow eliminated a real FHDB boot loop, and the completed `0.2.0` manager has now been exercised successfully on real PlayStation 2 hardware. Storage selection, standalone verified backups, USB mass storage, restart and shutdown handling, restoration, MagicGate signing, and guarded payload installation were reported working as intended.

This is the first full release. Keep important backups on another machine anyway, because passing a hardware test does not make twenty-year-old disks immortal or user-selected payloads clairvoyant.

## Features

- Validates the complete 1024-byte APA `__mbr` header and checksum.
- Rejects hybrid APA/GPT layouts.
- Shows the current `osdStart` and `osdSize` values.
- Lets the user select `mc0:`, `mc1:`, or `mass:` as the working storage device.
- Provides a dedicated `START` action that saves and verifies the current 1024-byte APA master header without modifying the HDD.
- Creates and reads back a byte-for-byte header backup before any HDD-changing operation.
- Disables only the bootstrap pointer through `HDIOC_SETOSDMBR`.
- Restores a non-zero pointer only from a valid backup matched to the same disk.
- Accepts legacy `FHDBMBR.BIN` and `FHDBMBR2.BIN` backups made by version `0.1.x`.
- Structurally validates and MagicGate-signs a stock `MBR.XLF`.
- Writes the signed payload only to the reserved `__mbr` area beginning at sector `0x2000`.
- Flushes and compares every written payload sector before enabling its pointer.
- Offers both a controlled power-off and a restart to the PS2 Browser.

## What it deliberately does not do

The manager does not format a disk, create APA partitions, install the FHDB or HDD-OSD file tree, copy applications, configure OSDSYS, or decide which third-party distribution you should trust this week.

The `MBR.XLF` installation option installs only the signed MBR bootstrap program. The partitions and files expected by that program must already exist. Installing the bootstrap without its corresponding environment will merely give the ROM a beautifully verified way to launch something incomplete.

## Safety model

The manager refuses an HDD-changing operation unless the relevant safety checks pass.

For every operation:

1. `hdd0:` must report a valid APA disk.
2. The master header must contain the `APA`, `__mbr`, and Sony MBR signatures.
3. The APA checksum must be valid.
4. Hybrid APA/GPT layouts are rejected.
5. A complete 1024-byte header backup must be saved to the selected device.
6. The backup must be read back and compared byte for byte.
7. The operation requires a distinct `L1 + R1 + action` confirmation chord.
8. The final APA pointer is read back and verified.

The installation path adds these checks:

1. Installation is available only while the current pointer is disabled.
2. `MBR.XLF` must pass bounded KELF structure validation and be no larger than 4 MiB.
3. The `__mbr` partition must report enough reserved capacity after sector `0x2000`.
4. The payload must be signed successfully through a PS2 memory card and the console security hardware.
5. The signed payload is written in two-sector chunks.
6. The HDD cache is flushed and every written sector is compared with the signed buffer.
7. `osdStart` and `osdSize` are set only after the payload passes read-back verification.

No raw write is ever issued to sectors 0 or 1. Raw writes are used only for the reserved bootstrap payload area; the APA header itself is updated by the standard `ps2hdd` driver so its checksum is recalculated normally.

## Preparing the files

Copy `PS2_HDD_BOOTSTRAP_MANAGER.ELF` somewhere your existing launcher can run it.

For bootstrap installation, also copy the stock, unsigned `MBR.XLF` supplied with the matching FHDB/HDD-OSD installer to the root of the device you intend to select:

```text
mc0:/MBR.XLF
mc1:/MBR.XLF
mass:/MBR.XLF
```

Do not rename an ordinary ELF to `MBR.XLF`; the manager checks the KELF container and rejects plain ELF files. Use a payload from the installation package that matches the environment already present on the HDD.

A genuine PS2 memory card is required for MagicGate signing. When `mc0:` or `mc1:` is selected, that card is used automatically. When `mass:` is selected, the manager asks whether `mc0` or `mc1` should perform the signing.

## Recovering from an FHDB boot loop

If the HDD bootstrap prevents a normal boot, first enable this option in Free McBoot Configurator:

```text
Configure OSDSYS Options -> Skip HDD Update Check = ON
```

On some Free McBoot configurations, that option alone does not stop the console from loading the HDD. If the PS2 still attempts to boot from the HDD, the active FMCB memory card may be loading its HDD support modules from `BIEXEC-SYSTEM` before or independently of the OSDSYS setting.

With the HDD disconnected if necessary, use wLaunchELF to back up and then remove these files from `BIEXEC-SYSTEM` on the memory card containing FMCB:

```text
mc0:/BIEXEC-SYSTEM/hddload.irx
mc0:/BIEXEC-SYSTEM/dev9.irx
mc0:/BIEXEC-SYSTEM/atad.irx
```

If FMCB is installed on the card in slot 2, use the equivalent `mc1:/BIEXEC-SYSTEM/` paths. These are files on the memory card, not files on the HDD. Keep a copy so they can be restored later if required.

After removing the modules, power the console off completely, reconnect the HDD, boot through FMCB, and launch the manager. This workaround is necessary only when `Skip HDD Update Check = ON` does not actually prevent HDD loading.

Then:

1. Boot with the HDD and FMCB memory card connected.
2. Launch `PS2_HDD_BOOTSTRAP_MANAGER.ELF` through wLaunchELF or an FMCB entry.
3. Confirm that `APA header: valid` is shown with non-zero pointer values.
4. Press `SELECT`, choose the backup device, and confirm with `X`.
5. Press `X` on the main screen.
6. Confirm that the displayed backup path is correct.
7. Hold `L1 + R1` and press `X`.
8. Wait for `Bootstrap disabled and verified.`
9. Open the power menu and either power off or restart to the PS2 Browser.

Do not reset or remove power while an HDD write or verification is in progress.

## Installing a bootstrap payload

This is intended for a manual FHDB or HDD-OSD setup whose required partitions and files already exist.

1. Make sure the current HDD bootstrap pointer is disabled. If it is enabled, back it up and disable it first.
2. Put the correct stock `MBR.XLF` in the root of `mc0:`, `mc1:`, or `mass:`.
3. Select that storage device with `SELECT`.
4. Press `CIRCLE`.
5. If using `mass:`, select the PS2 memory card used for MagicGate signing.
6. Review the source, backup, target sector, byte size, and sector count.
7. Hold `L1 + R1` and press `CIRCLE`.
8. Wait until both the payload and pointer report successful verification.
9. Restart only after confirming that the expected HDD environment is complete.

## Controls

| Context | Input | Action |
|---|---|---|
| Main menu | `START` | Save and verify the current MBR header without changing the HDD |
| Main menu | `SELECT` | Choose `mc0`, `mc1`, or `mass` |
| Enabled bootstrap | `X` | Back up and prepare to disable |
| Disable confirmation | `L1 + R1 + X` | Clear the active pointer |
| Disabled bootstrap | `SQUARE` | Load a matching backup |
| Restore confirmation | `L1 + R1 + SQUARE` | Restore the saved pointer |
| Disabled bootstrap | `CIRCLE` | Load, sign, and prepare to install `MBR.XLF` |
| Install confirmation | `L1 + R1 + CIRCLE` | Write, verify, and enable the payload |
| Main menu | `TRIANGLE` | Open the power/restart menu |
| Confirmation screens | `TRIANGLE` | Cancel without the pending write |

## Backups

Press `START` on the main menu to make a standalone backup at any time, regardless of whether the bootstrap pointer is enabled or disabled. This reads the current 1024-byte APA master header, saves it to the selected storage device, reads it back, and compares every byte. It does not write to the HDD.

New backups use the first available path on the selected device:

```text
<device>:/HDDMBR.BIN
<device>:/HDDMBR2.BIN
```

Existing unrelated files are not overwritten. Diagnostic value `999998` means a slot was occupied and deliberately preserved.

Restoration also searches the old version `0.1.x` names on the selected device:

```text
<device>:/FHDBMBR.BIN
<device>:/FHDBMBR2.BIN
```

A restoration backup must be exactly 1024 bytes, contain a valid standard APA master header, contain non-zero bootstrap fields, and match the currently connected disk except for the checksum and mutable pointer fields.

Keep another copy on a PC. The manager can preserve a header; it cannot negotiate with a dying drive, an incorrect payload, or a user who has decided that backups are a form of pessimism.

## Technical details

The manager embeds its PS2SDK IOP dependencies, including the fileXio stack, free memory-card drivers, `secrman`/`secrsif`, BDM/FatFs USB mass storage, poweroff, DEV9, ATA, and APA HDD drivers.

The primary device calls are:

- `HDIOC_READSECTOR` (`0x6836`) for the APA header and payload read-back;
- `HDIOC_WRITESECTOR` (`0x6837`) only for the reserved payload area at sector `0x2000` and above;
- `HDIOC_SETOSDMBR` (`0x6833`) for `osdStart` and `osdSize`;
- `HDIOC_FLUSH` (`0x4804`) before verification;
- `SecrDownloadFile()` through `secrman`/`secrsif` for console-side KELF signing;
- `ExecOSD("BootBrowser")` for the restart option.

The payload-write order follows the Free McBoot installer design: sign the KELF, write sector-aligned chunks into the reserved area, and record the final start and size in the APA header. This implementation adds a mandatory backup, size/bounds checks, a disabled-pointer prerequisite, a full post-flush payload comparison, and pointer-last activation.

## Hardware validation

The original `0.1.1` disable workflow successfully removed a real post-uninstall FHDB boot loop on:

- PlayStation 2 FAT `SCPH-50000` (Japanese model);
- MechaPWN-enabled console;
- cross-model Free McBoot memory card;
- standard non-GPT APA HDD.

The console subsequently cold-booted with the HDD connected. The final `0.2.0` workflow was then reported working on the same hardware, including selectable `mass:` storage and a standalone byte-for-byte backup that completed verification without modifying HDD data.

## Building

Install an up-to-date PS2DEV/PS2SDK environment, then run:

```sh
export PS2DEV=/opt/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin"
make release
```

The resulting file is `PS2_HDD_BOOTSTRAP_MANAGER.ELF`. Release `0.2.0` was built with the official PS2DEV v2.0.0 prebuilt toolchain.

SHA-256:

```text
9f41f9cfc647e1f21db0a02b39c2fe04a4842f5d052bfba7c93da45a77d9ae48
```

## License

The application source is released under the MIT License. Embedded PS2SDK modules retain the Academic Free License 2.0 terms included in `PS2SDK_LICENSE.txt`.

## Acknowledgements

- PS2DEV and PS2SDK contributors for the APA, USB, security, and console services.
- Free McBoot/FHDB contributors for the original signing and MBR installation workflow.
- Hifu Himejima for reproducing the failure, preserving the disk header, testing on real hardware, and being that one gloriously unhinged developer who decided to correct this great injustice.
