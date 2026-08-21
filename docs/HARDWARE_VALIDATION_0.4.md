# Michishirube 0.4 real-hardware validation log

This document records hardware evidence for the development-only `0.4.0-dev` Michishirube line. It distinguishes directly inspectable artifacts from tester observations and from follow-up code that still requires another hardware pass.

## 2026-08-21 — healthy-disk functional pass

### Provenance

The supplied PS2-side artifacts identify themselves as **PS2 HDD Bootstrap Manager v0.4.0-dev**. The text formats do not encode a Git commit SHA, so this record treats source provenance as version-level rather than claiming cryptographic linkage to one exact branch head.

Console ROMVER reported by the manager:

```text
0170JC20030206
```

Expected regional FMCB system folder:

```text
BIEXEC-SYSTEM
```

### Supplied artifact hashes

```text
HDDMBR.BIN
SHA-256 eebab60486774333997266ee13799d8d3b0a2014b95b8a4d49ecbd660402bf5d
size 1024 bytes

HDDRESCUE.BIN
SHA-256 5cf654a88be0cb2f9742f81b412940a3ecf9ff18f6889a4c8f7c6725ce8ac58a
size 1280 bytes

BOOTCHAIN.TXT
SHA-256 dfbe02136b8b95fd82dfabe39bdbfe45b85eef14efd1c6d46ce9c4e5547b478a
size 1312 bytes

HDDMAN.LOG
SHA-256 75566b14d68068d44f4e486482d708cfd62bb449599360402274940575a644e3
size 740 bytes
```

### `HDDMBR.BIN` inspection

The exact 1024-byte header is internally consistent:

```text
APA magic        APA\0
id               __mbr
start            0x00000000
length           0x00040000
type             0x0001 (APA MBR)
flags            0x0000
nsub             0
next             0x00040000
prev             0x9b800000
Sony MBR marker  Sony Computer Entertainment Inc.
MBR version      2
osdStart         0x00000000
osdSize          0x00000000
PC 0x55AA        absent
APA checksum     valid
```

Stored APA checksum and a host-side recomputation over words 1..255 both equal:

```text
0x1428c7ff
```

This is a healthy disabled-bootstrap master and is suitable as the baseline for later controlled metadata-corruption tests.

### `HDDRESCUE.BIN` inspection

The capsule is valid version-1 `PS2HBRC` metadata followed by one APA header:

```text
metadata size    256
complete size    1280
flags            VALID_APA only
payload start    0
payload sectors  0
payload bytes    0
KELF file bytes  0
ROMVER           0170JC20030206
family           No active payload
confidence       certain
```

The capsule records the APA SHA-256:

```text
eebab60486774333997266ee13799d8d3b0a2014b95b8a4d49ecbd660402bf5d
```

and the embedded 1024-byte APA header is byte-for-byte identical to the supplied `HDDMBR.BIN`.

This confirms that the header-only rescue path for a disabled bootstrap produced mutually consistent host-verifiable evidence.

### Boot-chain report

The supplied report shows:

- APA master validation: valid;
- OSD pointer: disabled;
- active payload: intentionally not read because the pointer is disabled;
- FMCB `Skip_HDD`: configuration files not found in the checked locations;
- no memory-card HDD modules found in either slot;
- `__sysconf` mounted successfully;
- `__system` mounted successfully;
- no FHDB `FREEHDB.CNF` / `BOOT.ELF` evidence;
- `OSDMBR.CNF` present;
- `boot_auto=$HOSDSYS`;
- no characteristic PSBBN partition set;
- no HOSDMenu/HDD-OSD/legacy osdmain executable evidence;
- final assessment: no structural contradiction found.

### Session log

The supplied `HDDMAN.LOG` confirms at least the following operations on hardware:

1. valid disabled APA master admitted;
2. boot-chain scan completed with `No active payload / certain`;
3. `BOOTCHAIN.TXT` was written successfully to `mass:`;
4. storage selection changed to `mc0` and back to `mass`;
5. a second diagnostics scan/report completed;
6. an already matching `HDDRESCUE.BIN` was recognized and reused rather than overwritten unnecessarily;
7. standalone `HDDMBR.BIN` + `HDDRESCUE.BIN` backup completed;
8. attempted MBR-source loading from `mass:/MBR.XLF` failed with `-4` and did not proceed to installation;
9. controlled power-off was requested.

The failed source-load case is useful negative-path evidence: the manager remained fail-closed rather than turning a missing/invalid source into an HDD write.

### Tester-observed UI/feature behavior

Tester report for this pass:

- all exercised 0.4 functions appeared to work as intended;
- hierarchical menu navigation worked;
- actions incompatible with the current configuration were visibly blocked/disabled rather than executable;
- no unexpected HDD mutation was observed during the healthy-disk pass;
- the principal usability problem was startup latency: the application remained in initialization for approximately **1–2 minutes** before becoming interactive.

The 1–2 minute figure is a human observation from this pass, not a timer measurement from the supplied log.

## Follow-up: startup latency instrumentation / fast admission

The pre-fix startup performed a full boot-chain diagnostics refresh before entering the dashboard. That refresh includes memory-card probing, FMCB config checks, read-only PFS mounts for `__sysconf` and `__system`, downstream-file probes, report rendering and report/log persistence.

The development branch now defers that expensive boot-chain evidence scan until Diagnostics or another workflow explicitly requests it. Startup retains the safety-critical admission sequence:

```text
IOP reset
embedded modules
services
controller
first HDIOC_STATUS / recovery wrapper
raw sectors 0-1 read
APA/non-hybrid validation
dashboard
```

A new `Startup timing ms:` log record measures:

- IOP reset;
- embedded module loading;
- service initialization;
- controller initialization / ANALOG-mode probe;
- first HDD status call;
- raw master read;
- total pre-dashboard startup.

The initialization screen also names the current phase. The next hardware run should therefore establish whether the former delay was dominated by the now-deferred diagnostics or by `ps2hdd`/DEV9/module startup itself.

This fast-start change is **not yet hardware-validated** in this record.

## Next controlled hardware tests

Use the separate [`HARDWARE_FAULT_INJECTION.md`](HARDWARE_FAULT_INJECTION.md) procedure and return to a fully healthy baseline between every scenario.

Recommended order:

1. new fast-start timing pass on the healthy disk;
2. read-only forensic scan on the healthy disk and comparison with DriveForge's known APA topology;
3. one-bit deterministic master-magic corruption and recovery;
4. one-bit interior `next` corruption and forensic topology repair;
5. exact two-bit interior `next` corruption and forensic topology repair;
6. only after those pass, design a controlled multi-header physical test.

For every anomaly discovered on hardware, add a deterministic host regression before accepting its fix.
