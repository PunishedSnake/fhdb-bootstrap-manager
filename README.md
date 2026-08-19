# FHDB Bootstrap Manager

FHDB Bootstrap Manager is a small PlayStation 2 ELF that safely disables or restores the HDD bootstrap pointer stored in the APA `__mbr` header.

It exists because uninstalling Free HDBoot can remove the files while leaving the console enthusiastically trying to boot those now-missing files forever. Apparently, "uninstall" and "stop launching it" were separate optional features.

## The problem it solves

On a PS2 FAT configured for HDD boot, the ROM checks two fields in the APA master header:

- `osdStart`: the starting sector of the signed HDD bootstrap program;
- `osdSize`: the size of that program.

Some FHDB removal paths delete the software but leave these fields populated. The console still sees an enabled HDD update, attempts to execute a payload that is no longer usable, resets, and repeats the performance indefinitely. Removing the entire HDD works, naturally, because unplugging storage is apparently a perfectly reasonable uninstaller.

This program clears only those two fields through PS2SDK's documented `HDIOC_SETOSDMBR` interface. It does not format the disk, rewrite the partition table manually, delete games, remove partitions, or change the console's EEPROM HDD-boot setting.

## Safety model

The program deliberately refuses to write until all of the following are true:

1. `hdd0:` reports a valid APA disk.
2. The 1024-byte sector-zero header contains the `APA`, `__mbr`, and Sony MBR signatures.
3. The APA checksum is valid.
4. The disk is not using the hybrid APA/GPT layout.
5. A complete header backup has been written to a memory card.
6. That backup has been read back and compared byte for byte.
7. The user confirms the operation with `L1 + R1 + X`.

After changing the pointer, the program flushes the HDD cache, reads the header again, validates its checksum again, and verifies that both fields contain the requested values.

## Hardware validation

Version 0.1.1 successfully removed a real post-uninstall FHDB boot loop on:

- PlayStation 2 FAT `SCPH-50000` (Japanese model);
- MechaPWN-enabled console;
- cross-model Free McBoot memory card;
- standard non-GPT APA HDD.

The console subsequently cold-booted with the HDD connected and without entering the previous reset loop.

This is one confirmed hardware configuration, not a promise that every adapter, disk, ROM revision, or creative twenty-year-old storage arrangement will behave identically.

## Usage

If the current HDD bootstrap causes a boot loop, first enable this option in Free McBoot Configurator:

```text
Configure OSDSYS Options -> Skip HDD Update Check = ON
```

Then:

1. Start the console with the HDD and FMCB memory card connected.
2. Launch `FHDB_BOOTSTRAP_MANAGER.ELF` using wLaunchELF or an FMCB menu entry.
3. Confirm that the program reports `APA header: valid` and shows non-zero `osdStart`/`osdSize` values.
4. Press `X` to create and verify the memory-card backup.
5. Keep the displayed backup file somewhere safe.
6. Hold `L1 + R1` and press `X` to disable the bootstrap.
7. Wait for `Bootstrap disabled and verified.` before powering off.

Do not reset or remove power while the HDD update is in progress.

## Controls

| Input | Action |
|---|---|
| `X` | Back up the active header and prepare to disable the bootstrap |
| `L1 + R1 + X` | Confirm disabling the bootstrap |
| `Square` | Load a matching backup when the bootstrap is disabled |
| `L1 + R1 + Square` | Confirm restoring the saved pointer |
| `Triangle` | Cancel or shut down without further changes |

## Backups and restoration

The first available matching path is used:

```text
mc0:/FHDBMBR.BIN
mc0:/FHDBMBR2.BIN
mc1:/FHDBMBR.BIN
mc1:/FHDBMBR2.BIN
```

Existing unrelated backups are not overwritten. A restoration backup must:

- be exactly 1024 bytes;
- contain a valid standard APA master header;
- have a valid checksum;
- contain non-zero bootstrap fields;
- match the currently connected disk, ignoring only the checksum and the two fields being restored.

Keep a second copy of the backup on a PC. The program preserves the original pointer, but it does not preserve files that another tool, a dying disk, or an unusually determined user deletes later.

## Technical operation

The program embeds the required PS2SDK IOP modules and uses:

- `HDIOC_READSECTOR` (`0x6836`) to read sectors 0 and 1;
- `HDIOC_SETOSDMBR` (`0x6833`) to set `osdStart` and `osdSize`;
- `HDIOC_FLUSH` (`0x4804`) to flush the device cache.

Disabling sends `{ start = 0, size = 0 }` to `HDIOC_SETOSDMBR`. The standard `ps2hdd` driver updates the fields through its APA cache and recalculates the header checksum when the dirty header is written.

No raw-sector write command is used.

## Building

Install an up-to-date PS2DEV/PS2SDK environment, then run:

```sh
export PS2DEV=/opt/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin"
make release
```

The resulting file is `FHDB_BOOTSTRAP_MANAGER.ELF`. Release 0.1.1 was built with the official PS2DEV v2.0.0 prebuilt toolchain.

## Version 0.1.1 checksum

```text
SHA-256  bba8de49d535adc7a91fd1ff08ee7eacbdad0c297273e963e76610d022f28e59
```

## License

The application source is released under the MIT License. Embedded PS2SDK modules retain the Academic Free License 2.0 terms included in `PS2SDK_LICENSE.txt`.

## Acknowledgements

- PS2DEV and PS2SDK contributors for documenting and implementing the APA HDD interfaces.
- The Free McBoot/FHDB community for keeping the PS2 useful long after its warranty stopped being emotionally relevant.
- PunishedSnake for reproducing the failure, testing both builds on real hardware, preserving the original header, and confirming the successful cold boot.
