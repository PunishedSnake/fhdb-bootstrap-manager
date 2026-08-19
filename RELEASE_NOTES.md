# PS2 HDD Bootstrap Manager 0.2.0-rc1

The small FHDB boot-loop repair tool has grown into a general PS2 HDD bootstrap manager, because apparently once you safely clear two fields you become responsible for the entire lifecycle of those fields.

This release candidate adds selectable `mc0:`, `mc1:`, and `mass:` storage, controlled shutdown or restart, legacy-backup restoration, and guarded installation of a stock `MBR.XLF` for manual FHDB/HDD-OSD setups.

## Highlights

- Select the working storage device instead of letting the program guess.
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

The original disable workflow is hardware-validated and eliminated a real FHDB post-uninstall boot loop on an SCPH-50000. The new USB, restart, MagicGate-signing, and payload-installation paths are not yet hardware-validated, so this build is intentionally published as a release candidate.

Read `README.md` before using the install option and keep a copy of the generated header backup on another machine.

## Included asset

`PS2_HDD_BOOTSTRAP_MANAGER.ELF`

SHA-256:

```text
db288182e60386afc56e76a6bebcc24ac28a0f3f8869d6419163d9e8a22690b6
```
