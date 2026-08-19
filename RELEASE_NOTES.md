# PS2 HDD Bootstrap Manager 0.3.0-rc1

The bootstrap manager can now preserve the program behind the pointer, not merely the pointer and a firm belief that its sectors will remain fine forever.

This release candidate adds a complete rescue capsule, full payload restoration, a read-only boot-chain inspector, and persistent diagnostics on `mass:`, `mc0:`, or `mc1:`. It attempts to distinguish FHDB, PSBBN/OSDMenu, HOSDMenu/HDD-OSD, custom OSDMenu, and unknown payloads from KELF structure plus downstream filesystem evidence.

## Highlights

- `START` saves both the traditional 1024-byte APA header backup and a versioned `HDDRESCUE*.BIN` capsule.
- An enabled bootstrap capsule includes every referenced payload sector, the unpadded KELF length, metadata, and SHA-256 digests.
- `SQUARE` prefers a valid same-disk full capsule and restores the payload before enabling its pointer.
- A corrupt or wrong-disk capsule is reported instead of silently falling through to an older pointer-only restore.
- `R1` scans the boot chain and writes a standalone `BOOTCHAIN.TXT` report.
- `HDDMAN.LOG` records ordered session diagnostics and write results.
- The selected destination is inferred from the ELF launch path and can still be changed with `SELECT`.
- The inspector checks ROMVER, all regional FMCB folders, `OSDSYS_Skip_HDD`, external HDD modules, OSDMenu configuration, PFS next-stage files, and characteristic PSBBN partitions.
- Explicit OSDMenu `boot_auto` configuration wins over stale partition evidence.
- Portable SHA-256 and capsule-format tests are included in the source tree.

## Files written to the selected device

```text
HDDMBR.BIN / HDDMBR2.BIN          1024-byte APA header backups
HDDRESCUE.BIN / HDDRESCUE2.BIN    versioned header + payload capsules
HDDMAN.LOG                         append-only session diagnostics
BOOTCHAIN.TXT                      latest complete boot-chain report
```

Existing differing backup and capsule files are preserved. A session log at or above 128 KiB is replaced at the next flush; `BOOTCHAIN.TXT` always represents the latest scan.

## Identification limits

The manager does not pretend encrypted KELFs contain convenient plaintext name tags. Family names are probable classifications derived from structural validation and observable downstream files. The report includes confidence and the exact evidence, allowing a human to disagree with the machine in an informed manner—a feature apparently considered optional by several generations of boot tools.

## Safety changes

- Capsule metadata, size relationships, flags, APA header, same-disk identity, KELF structure, payload bounds, and SHA-256 digests must all validate before full restoration.
- Payload sectors are written, flushed, and compared before the APA pointer is enabled.
- Legacy pointer-only restoration now validates the saved range and creates a fresh safety backup first.
- Raw writes remain confined to the reserved `__mbr` payload area; sectors 0 and 1 are never raw-written.

## Validation status

The source cross-compiles cleanly with `-Wall -Wextra -Werror`, and the portable SHA-256/capsule tests pass. The underlying `0.2.0` workflows are hardware-validated; the new full-rescue and diagnostic paths are release-candidate functionality awaiting real-console testing by Hifu Himejima.

Read `README.md` and `RESCUE_FORMAT.md` before testing. Keep copies of generated backups on another machine.

## Included asset

`PS2_HDD_BOOTSTRAP_MANAGER.ELF`

SHA-256:

```text
b86acd338883418561b97c006d8cc2715336425490f931168f617eea8adc3c05
```
