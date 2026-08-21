# Michishirube hardware fault-injection workflow

This document describes the **development-only** procedure for creating narrowly controlled APA metadata damage on a sacrificial or fully backed-up PS2 HDD and validating Michishirube's recovery paths on real hardware.

The goal is not to create a generally broken disk. Each run changes the smallest useful amount of metadata, preserves the exact original 1024-byte header on the host first, and tests one recovery invariant at a time.

## Tool

`tools/hardware_fault_injector.py` supports raw image files everywhere and `\\.\PhysicalDriveN` on Windows.

It is intentionally harder to write than to read:

- `probe` is always read-only;
- `mutate` is a dry run unless `--apply` is present;
- every mutation requires the exact current master-header SHA-256 printed by a fresh `probe`;
- a physical drive additionally requires `--confirm-physical-write`;
- the exact original 1024-byte header is saved before the first write;
- every write is flushed and read back exactly;
- `restore` refuses to overwrite a header unless it is exactly the expected mutated state from the manifest;
- restore writes non-master headers before LBA 0 when a future manifest contains more than one touched header.

The script is not a general raw-disk editor and deliberately exposes only test scenarios that correspond to Michishirube recovery contracts.

## Before connecting the test disk to the PC

1. Use only the sacrificial/test PS2 HDD or a disk for which a complete external backup exists.
2. Shut the PS2 down normally before removing the disk.
3. Do not run partitioning, formatting, CHKDSK, Disk Management initialization, or other host write tools against the PS2 disk.
4. Identify the correct Windows physical-drive number with DriveForge or another read-only method. Do not guess it.
5. Keep all generated `ps2-hdd-fault-*` backup directories until the complete test cycle is finished.

## Step 1 — read-only probe

Example for `PhysicalDrive3`:

```powershell
python tools\hardware_fault_injector.py probe --physical 3
```

The probe requires a currently valid APA master and live `next` chain. It prints:

- physical size;
- exact master-header SHA-256;
- live-chain header count;
- master `next`, `prev`, and length;
- one suggested internal header for topology testing.

No bytes are modified.

Copy the printed master SHA-256. Every mutation command must present it again through `--expect-master-sha`; this makes selecting a different valid APA disk by accident insufficient to authorize the write.

## Step 2 — always preview first

Example one-bit master corruption:

```powershell
python tools\hardware_fault_injector.py mutate `
  --physical 3 `
  --scenario master-magic-1bit `
  --expect-master-sha <SHA_FROM_PROBE>
```

Without `--apply`, the command only prints the exact target LBA, before/after SHA-256, field change, bit distance, and byte offsets.

For a physical drive, an actual write additionally requires both:

```text
--apply --confirm-physical-write
```

## Scenario A — deterministic master recovery

Use this first because it is the narrowest exceptional write path.

```powershell
python tools\hardware_fault_injector.py mutate `
  --physical 3 `
  --scenario master-magic-1bit `
  --expect-master-sha <SHA_FROM_PROBE> `
  --backup-dir D:\PS2-tests\master-1bit `
  --apply --confirm-physical-write
```

The mutation changes only the first byte of `APA\0` by one bit and deliberately retains the old checksum. It reproduces the physical-style single-master corruption already accepted by the portable deterministic repair policy.

Expected PS2 behavior:

1. normal `ps2hdd` admission fails;
2. the first-status recovery wrapper raw-reads sectors 0-1;
3. `apa_repair` identifies exactly one canonical repair;
4. the stale checksum corroborates that exact correction;
5. Michishirube requires `HDDRAW*.BIN` preservation and explicit confirmation;
6. exactly sectors 0-1 are repaired, flushed, read back and compared;
7. the console requires restart;
8. after restart, normal admission succeeds again.

If any step differs, do not attempt another scenario. Preserve all logs/backups and restore the PC-side manifest instead.

## Scenario B — one-bit interior topology corruption

Return the disk to a fully healthy baseline first, run a fresh `probe`, and use the newly printed master SHA.

```powershell
python tools\hardware_fault_injector.py mutate `
  --physical 3 `
  --scenario next-1bit `
  --expect-master-sha <FRESH_SHA_FROM_PROBE> `
  --backup-dir D:\PS2-tests\next-1bit `
  --apply --confirm-physical-write
```

By default the script chooses a middle non-master live-chain header whose `next` link is nonzero. A specific header can be selected from the probe output with, for example:

```text
--lba 0x12340000
```

Only the 32-bit `next` value is changed. XOR mask `0x1` flips exactly one bit; the old header checksum is retained.

Expected Michishirube behavior:

- raw forensic discovery still reconstructs the neighboring topology from independent evidence;
- at least one candidate map remains coherent and non-overlapping;
- the expected `next` value is derived from the graph rather than brute-forced;
- replacing the corrupted link restores the old stored checksum;
- the plan contains one checksum-corroborated topology patch;
- `HDDMETA*.BIN` is required before write;
- source bytes must still equal the scan-time bytes immediately before write;
- the interior header is written/flushed/read back;
- restart is required before normal HDD work continues.

## Scenario C — exact two-bit interior topology corruption

After another complete return to baseline and fresh probe:

```powershell
python tools\hardware_fault_injector.py mutate `
  --physical 3 `
  --scenario next-2bit `
  --expect-master-sha <FRESH_SHA_FROM_PROBE> `
  --backup-dir D:\PS2-tests\next-2bit `
  --apply --confirm-physical-write
```

The script XORs the live `next` field with `0x3`, producing exactly two flipped bits while retaining the stale checksum.

Expected behavior is the same as Scenario B, except the forensic patch inspector must report a bit distance of exactly 2 and the plan must still be checksum-corroborated from the graph-derived original link.

## Restoring from the host backup

Every applied mutation writes a directory containing:

```text
manifest.json
LBA_XXXXXXXX.bin
```

Preview restore:

```powershell
python tools\hardware_fault_injector.py restore `
  --physical 3 `
  --manifest D:\PS2-tests\next-1bit
```

Apply restore:

```powershell
python tools\hardware_fault_injector.py restore `
  --physical 3 `
  --manifest D:\PS2-tests\next-1bit `
  --apply --confirm-physical-write
```

The restore is intentionally fail-closed:

- if the current header already equals the original bytes, nothing is written;
- if it equals the exact expected mutated bytes, the original 1024 bytes may be restored;
- if it matches neither state, restore refuses to overwrite it.

This means a successful Michishirube repair does not get overwritten merely because the PC restore command was run afterward.

## Validation artifacts to keep for each scenario

Preserve:

- fault-injector `manifest.json`;
- original `LBA_XXXXXXXX.bin`;
- Michishirube `HDDMAN.LOG`;
- `BOOTCHAIN.TXT` when available;
- `FORENSIC.TXT` for topology scenarios;
- `HDDRAW*.BIN` for master recovery;
- `HDDMETA*.BIN` for forensic topology repair;
- a fresh post-repair PC `probe` result;
- notes on console model, adapter, HDD model/capacity, storage target and controller.

Any hardware-only discrepancy should become a deterministic host regression before its code fix is considered complete.

## Built-in self-test

The script has an image-only self-test and CI runs it without physical hardware:

```sh
python3 tools/hardware_fault_injector.py selftest
```

It creates a temporary APA image and validates probe/mutate/verified-write/restore cycles for:

- one-bit master magic corruption;
- one-bit `next` corruption;
- exact two-bit `next` corruption.

Passing this test validates the host utility's byte-level contract. It does not validate Windows physical-drive semantics or PS2 ATA durability; those remain hardware gates.
