# PS2 HDD Bootstrap Manager 0.4.0 — Michishirube

Michishirube turns the project from a narrowly focused bootstrap-pointer utility into a modular PS2-side recovery and forensic toolkit.

The short version: the manager can now inspect much more of a damaged APA disk, explain what it believes happened, preserve exact recovery evidence, and only then decide whether a write path is allowed. It also finally has a real Graphics Synthesizer UI instead of asking the PS2 to cosplay as a debug terminal forever.

## Highlights

- Application-wide GS-rendered UI with hierarchical navigation, full-row `LOCKED` states, themes, live HDD operation status, contextual error explanations, and VBlank-synchronized presentation.
- Faster startup: expensive boot-chain/PFS diagnostics are deferred until requested instead of blocking the dashboard.
- Portable APA forensic graph reconstruction with forward/reverse/geometry hypotheses, explicit evidence/confidence, overlap/conflict detection, and read-only shadow-map browsing.
- `FORENSIC.TXT` export with discovered headers, active-map evidence, dormant historical free-space remnants, and repair-plan state.
- Guarded deterministic recovery for narrowly provable damaged APA masters.
- Guarded multi-header topology repair limited to `prev`, `next`, and checksum changes.
- Exact pre-write recovery evidence through `HDDRAW*.BIN` and `HDDMETA*.BIN` snapshots protected by SHA-256.
- Non-master-first / master-last forensic write ordering, source-stability checks, flush/read-back verification, final touched-set verification, and mandatory restart after exceptional writes.
- One-bit and exact two-bit stale-checksum topology recovery regressions.
- 30 deterministic mounted-HDD fixtures, 9 sparse forensic HDD fixtures, large-HDL regression coverage, mutation tests, contextual-error tests, and a guarded physical-HDD fault injector.
- Stable UI configuration file: `HDDMAN.CFG` (`aqua`, `amber`, `sakura`, `mono`).

## Real-hardware findings folded into 0.4.0

Hardware testing materially changed this release rather than merely rubber-stamping CI.

A healthy large HDL-heavy disk exposed the old 512-node forensic limit. The partial graph looked plausible enough to manufacture two fake endpoint patches. Michishirube now treats **every truncated scan as strictly read-only** at the map policy, planner, UI, and raw-writer layers. Capacity was also raised to 2048 nodes.

The same disk exposed random grid false positives and historical checksum-valid `__empty` headers left behind inside a later coalesced active free extent. Direct grid discovery is now stricter, while the latter are retained as forensic evidence and classified as `DORMANT_FREE` instead of lowering active-chain confidence.

The final healthy-disk scan used for release validation reports:

```text
Nodes        : 1621 / 2048
Dormant free : 8
Truncated    : no

MAP 1: forward links
 confidence=100
 nodes=1613
 reciprocal=1612
 inferred=0
 conflicts=0
 overlaps=0
 patches=0
 automatic=no
 manual=no
```

The GS frontend was also iterated on real hardware: mixed-renderer displacement, black-screen CRT initialization, vertically mangled glyphs, and scan-time screen tearing were all found physically and converted into fixes. The release uses the proven 640x224 FIELD display path and VBlank-synchronized status rendering.

## Important recovery disclaimer

**The read-only diagnostics, backups, forensic scanning, report generation, UI safety gates, and non-destructive workflows have received substantial real-console validation. The exceptional raw metadata repair paths remain experimental in 0.4.0.**

That includes:

- direct sectors 0-1 APA master repair;
- forensic `prev` / `next` topology writes;
- multi-header recovery transactions;
- behavior during real power loss, unusual adapters, marginal media, and controller/storage combinations not yet tested by multiple users.

The code deliberately fails closed, requires verified snapshots, revalidates source bytes immediately before writes, flushes and reads back every mutation, and requires a restart after exceptional recovery. Those safeguards reduce risk; they do not magically turn limited hardware coverage into proof.

If you test recovery on expendable or fully imaged media, please report:

- console model / ROMVER;
- HDD/SSD model and capacity;
- network/HDD adapter or SATA/IDE bridge;
- exact scenario or corruption introduced;
- `FORENSIC.TXT`, `HDDMAN.LOG`, and relevant `HDDRAW` / `HDDMETA` evidence;
- whether the proposed diff matched the intended corruption;
- raw before/after header comparison if available.

Do **not** use experimental repair as your first move on irreplaceable data. Make an external image when possible.

## Normal bootstrap operations

The long-standing normal paths retain their established safety model:

- verified backup before mutation;
- payload writes restricted to the reserved `__mbr` bootstrap area;
- payload-first / pointer-last activation;
- `HDIOC_SETOSDMBR` for normal pointer changes;
- flush and exact read-back verification;
- no raw sectors 0-1 writes during normal install/restore/disable workflows.

`MBR.XIN` remains the preferred install source name, with `MBR.XLF` accepted as a compatibility fallback.

## Release assets

The GitHub release publishes:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.4.0.ELF
SHA256SUMS.txt
HDDMAN.CFG
```

`HDDMAN.CFG` can normally be placed beside the ELF. The manager also supports changing the active theme from **System -> UI theme**.

## What comes next

0.4.x is now feature-frozen except for defects and narrowly scoped validation fixes. Additional destructive testing on sacrificial disks can land as 0.4.x maintenance work.

The next feature train is **0.5.x Kakehashi**, focused on versioned recovery artifacts and interoperability between the PS2 manager and host-side tooling such as PS2 DriveForge. Host analysis may propose recovery actions; PS2-side policy remains the final write authority.
