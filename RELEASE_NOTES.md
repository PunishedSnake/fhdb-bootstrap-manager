# PS2 HDD Bootstrap Manager 0.2.0

The small FHDB boot-loop repair tool has grown into a general PS2 HDD bootstrap manager, because apparently once you safely clear two fields you become responsible for the entire lifecycle of those fields.

The release candidates survived the console, the disk survived the release candidates, and the backup manager now contains a standalone backup action. This first full release provides selectable `mc0:`, `mc1:`, and `mass:` storage, controlled shutdown or restart, legacy-backup restoration, and guarded installation of a stock `MBR.XLF` for manual FHDB/HDD-OSD setups.

## Highlights

- Select the working storage device instead of letting the program guess.
- Press `START` to save and verify the current APA master header without modifying the HDD, regardless of whether the bootstrap pointer is enabled or disabled.
- Save and verify backups as `HDDMBR.BIN` or `HDDMBR2.BIN` on memory card or USB.
- Restore compatible `FHDBMBR*.BIN` files created by version 0.1.x.
- Restart directly to the PS2 Browser or power off cleanly.
- Validate a stock `MBR.XLF` and reject plain or malformed files.
- Sign the KELF through the console security hardware and a genuine PS2 memory card.
- Write only to the reserved `__mbr` payload area at sector `0x2000` and above.
- Flush and compare the complete signed payload before enabling its APA pointer.
- Require a verified header backup and `L1 + R1 + CIRCLE` before installation.

## Important limitation

The installation option installs only the MBR bootstrap program. It does not format the disk, create APA partitions, install FHDB/HDD-OSD files, or repair an incomplete environment. Use the `MBR.XLF` that belongs to the setup already present on the HDD.

## Validation status

The completed `0.2.0` manager was exercised successfully on real PlayStation 2 hardware. The original disable workflow eliminated a real FHDB post-uninstall boot loop on an SCPH-50000, and the final storage-selection and standalone-backup workflow was verified with `mass:` without modifying HDD data. Hifu Himejima reports the complete manager working as intended.

Read `README.md` before using the install option and keep a copy of the generated header backup on another machine.

## Included asset

`PS2_HDD_BOOTSTRAP_MANAGER.ELF`

SHA-256:

```text
9f41f9cfc647e1f21db0a02b39c2fe04a4842f5d052bfba7c93da45a77d9ae48
```
