# Michishirube 0.4 real-hardware validation log

This document records the physical-PS2 evidence used to promote Michishirube to 0.4.0. It deliberately distinguishes broadly exercised read-only/normal workflows from exceptional raw metadata repair paths that still need independent destructive testing.

## Release conclusion

0.4.0 is released with the following split:

### Broadly validated on real PS2 hardware

- startup and normal APA admission;
- hierarchical GS UI and state-dependent `LOCKED` actions;
- native 640x224 font/UI rendering;
- live themes and `HDDMAN.CFG` behavior;
- VBlank-synchronized live status without the earlier scan-time screen tearing;
- deferred boot-chain diagnostics / fast dashboard entry;
- boot-chain/PFS/MC diagnostics;
- header and full-rescue backup/reuse behavior;
- report/log persistence;
- negative fail-closed source-load behavior;
- healthy large-HDD raw forensic scanning;
- large-chain capacity/truncation policy;
- dormant historical free-space classification;
- zero-patch healthy-map behavior.

### Still experimental in 0.4.0

- direct sectors 0-1 master repair on deliberately corrupted media;
- physical `prev` / `next` forensic topology writes;
- multi-header recovery transactions;
- partial-failure and power-loss behavior on real disks/adapters;
- broad adapter/HDD/SSD/controller/storage compatibility coverage from independent testers.

The release therefore asks users to test destructive recovery only on sacrificial or fully imaged media and submit evidence.

## 2026-08-21 — healthy-disk baseline

### Console identity

ROMVER reported by the manager:

```text
0170JC20030206
```

Expected regional FMCB system folder:

```text
BIEXEC-SYSTEM
```

### Supplied baseline artifacts

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

The 1024-byte master is internally consistent:

```text
APA magic        APA\0
id               __mbr
start            0x00000000
length           0x00040000
type             0x0001
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

Stored and host-recomputed APA checksum:

```text
0x1428c7ff
```

This is a healthy disabled-bootstrap master and remains a useful reference state for later fault-injection work.

### `HDDRESCUE.BIN` inspection

The version-1 rescue capsule is valid and contains the exact same 1024-byte master as `HDDMBR.BIN`.

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

The embedded APA SHA-256 is:

```text
eebab60486774333997266ee13799d8d3b0a2014b95b8a4d49ecbd660402bf5d
```

### Boot-chain / session behavior

The supplied reports/logs established:

- valid disabled APA master admitted;
- disabled bootstrap pointer;
- `__sysconf` and `__system` mounted read-only for diagnostics;
- `OSDMBR.CNF` present with `boot_auto=$HOSDSYS`;
- no contradictory FHDB/PSBBN/HDD-OSD evidence;
- report persistence to `mass:`;
- storage switching between `mc0` and `mass`;
- matching rescue capsule reuse rather than unnecessary overwrite;
- successful standalone header/full-rescue backup;
- missing MBR source failed closed and did not enter a disk write path;
- controlled power-off path worked.

## Startup / UI findings

The first 0.4 hardware pass exposed approximately 1–2 minutes of apparent startup delay. The cause was not simply module initialization: the program was automatically running the full boot-chain diagnostics pass before showing the dashboard.

Michishirube now keeps only the safety-critical admission path before the dashboard:

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

Heavy PFS/MC diagnostics are lazy and run only when requested.

### GS UI iteration

Physical testing found several real display issues during development:

1. mixed libdebug + GS rendering placed the status panel in the lower-right;
2. standalone `graph_initialize(640x448)` produced a black screen;
3. virtual 448-line -> physical 224-line scaling destroyed bitmap-font rows;
4. the full-screen native UI worked, but forensic read telemetry tore visibly because frames were replaced too quickly.

The final 0.4.0 display contract is:

- `init_scr()` only for the proven CRT/read-circuit bootstrap;
- visible 640x224 FIELD drawing space at VRAM 0;
- all normal UI rendered through the GS frontend;
- native 8x8 glyphs with no fractional Y scaling;
- explicit full-row `LOCKED` states;
- `aqua`, `amber`, `sakura`, `mono` themes;
- stable `HDDMAN.CFG` name;
- VBlank-synchronized status presentation.

The VSync build was physically retested: screen tearing during forensic scanning disappeared. The visible status refresh rate is lower by design, while disk I/O itself is not forced to wait for one VBlank per raw read because high-rate read telemetry is coalesced.

### Post-release GS optimization physical result

The 0.4.x optimization branch adds true two-framebuffer VBlank swapping and an
application-owned 480p mode backed by a 720x448 visible / 768x448-stride pair.
The maintainer's 2026-08-22 physical retest reported successful switching in
both directions and correct operation after the change. This validates the
native <-> 480p transition on the tested console/display path, including the
full GS-state restoration that follows each mode reset.

The same backend now exposes guarded NTSC 480i, PAL 576i, 576p, 720p and 1080i
choices. Those additional signals are not covered by this physical result and
remain experimental. Every alternate mode has a ten-second confirmation and
automatic native fallback. A confirmed mode may be saved in `HDDMAN.CFG`, but
is guarded again at startup; failure or timeout persists `native` to avoid a
black-screen boot loop. 576p is disabled on ROM versions older than 2.20.

## Large-HDD forensic finding #1 — old node-cap truncation

The first healthy large-HDL forensic scan hit the old 512-node capacity. The partial forward map still looked extremely coherent, but the visible tail was not the physical tail. The old evaluator therefore inferred two fake endpoint changes.

The fix became a hard invariant:

```text
truncated scan => no write plan
```

It is enforced in:

- portable map policy;
- portable repair-plan construction;
- UI authorization;
- raw PS2 forensic writer.

Capacity was raised to 2048 nodes, direct-grid false-positive admission was tightened, and `FORENSIC.TXT` grew from 64 KiB to 512 KiB. Dedicated regressions cover a healthy 768-header chain and a 2049-header hard-read-only truncation case.

## Large-HDD forensic finding #2 — dormant `__empty` history

The next complete scan discovered 1621 valid APA-shaped headers while the active forward chain contained 1613. Eight extra checksum-valid `__empty` headers lived wholly inside a later, larger active `__empty` extent.

Those records are consistent with historical free-space allocator/coalescing metadata rather than competing active partitions.

The scanner now retains them as forensic evidence marked:

```text
DORMANT_FREE
```

but excludes them from active-map confidence and geometry competition only when all narrow containment/canonical checks pass.

## Final healthy forensic release-validation result

The final physical report used for 0.4.0 promotion produced:

```text
Disk sectors : 0xe8e088b0
Grid reads   : 14905
Reference reads: 0
Unreadable   : 0
Nodes        : 1621 / 2048 capacity
Dormant free : 8
Maps         : 1
Truncated    : no

MAP 1: forward links
 confidence=100
 nodes=1613
 reciprocal=1612
 inferred=0
 conflicts=0
 overlaps=0
 repairable=yes
 patches=0
 corroborated=0
 speculative=0
 automatic=no
 manual=no
```

Interpretation:

- the active chain is completely reciprocal;
- there are no inferred links, conflicts or overlaps;
- the eight historical free-space remnants are retained but do not reduce confidence;
- there is no proposed patch and therefore no write authorization.

The `repairable=yes` field in this zero-patch state means the candidate satisfies the structural prerequisites from which a plan *could* be built if a difference existed; it does not mean the healthy disk needs repair. Future report wording may rename this capability-like flag for clarity without changing policy.

## Independent testing requested

0.4.0 intentionally ships exceptional raw recovery with an experimental disclaimer rather than pretending one development console proves every physical combination.

Useful destructive test reports should include:

- console model / ROMVER;
- HDD/SSD make, model and capacity;
- adapter/bridge;
- exact fault introduced;
- manager version/build hash;
- `HDDMAN.LOG`;
- `FORENSIC.TXT`;
- relevant `HDDRAW`, `HDDMETA`, rescue artifacts;
- exact previewed patch;
- raw before/after header bytes where possible;
- restart and post-restart result.

Use [`HARDWARE_FAULT_INJECTION.md`](HARDWARE_FAULT_INJECTION.md) for the guarded host mutator. The separate DriveForge test HDD can be treated as the sacrificial physical target; the known-good console HDD should remain the healthy baseline.

Every reproducible hardware discrepancy should become a deterministic host regression before the corresponding fix is considered complete.
