# FHDB Bootstrap Manager 0.1.1

The first hardware-validated release.

FHDB Bootstrap Manager disables the orphaned HDD OSD bootstrap pointer that can remain after FHDB is uninstalled. This prevents a PS2 FAT from repeatedly attempting to execute a missing or unusable HDD payload while leaving the APA partition table, installed games, and payload sectors untouched.

Because apparently deleting an application and stopping the machine from booting that application were two separate projects.

## Highlights

- Validates the full 1024-byte APA `__mbr` header and checksum.
- Rejects hybrid APA/GPT layouts.
- Creates and verifies a byte-for-byte memory-card backup before offering any HDD write.
- Disables only `osdStart` and `osdSize` through `HDIOC_SETOSDMBR`.
- Flushes and reads the HDD back after the operation.
- Restores the original pointer from a backup matched to the same disk.
- Successfully resolved a real FHDB post-uninstall boot loop on an SCPH-50000.

## Included asset

`FHDB_BOOTSTRAP_MANAGER.ELF`

SHA-256:

```text
bba8de49d535adc7a91fd1ff08ee7eacbdad0c297273e963e76610d022f28e59
```

Read the safety and usage sections in `README.md` before running the program.
