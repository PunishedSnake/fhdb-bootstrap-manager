# Live HDD status and contextual errors (0.4 Michishirube)

Michishirube's hardware-validation UI exposes two complementary diagnostic surfaces: a live HDD activity monitor for long/raw operations, and contextual error descriptions that preserve the original numeric return code while explaining what the failing stage was trying to do.

## Live HDD monitor

The PS2-specific `disk_status_ps2` layer is fed by the actual raw HDD transports and exceptional recovery writers. It is presentation-only: it does not decide whether a read/write is allowed and cannot weaken any recovery gate.

The monitor shows:

- current high-level operation when a writer/reader establishes one;
- current phase/action (`READ`, `WRITE`, `VERIFY`, `FLUSH`, `POINTER UPDATE`, `SCAN`);
- current LBA and sector range;
- current position relative to the physical disk when `HDIOC_TOTALSECTOR` is available;
- a 28-character disk-position bar and percentage;
- an explicit warning during write-capable phases.

Read/scan redraws are throttled (currently one redraw per 16 ordinary read events) so screen rendering does not become the dominant cost of a raw scan. Destructive/transactional phases (`WRITE`, `VERIFY`, `FLUSH`, pointer update) force a redraw immediately.

The monitor is currently wired into:

- raw sector reads;
- active bootstrap payload reads;
- bootstrap payload write/flush/read-back;
- `HDIOC_SETOSDMBR` pointer updates and verification;
- deterministic sectors-0/1 master recovery;
- forensic multi-header topology repair, including source-stability checks, interior writes, master-last commit, immediate read-back, and final touched-set verification.

The existing forensic scanner's candidate/progress screen remains authoritative for candidate counts; raw transport activity may temporarily show the exact LBA being read between scanner progress updates.

## Contextual error catalog

Existing numeric return values remain unchanged. The new `app_error` layer attaches a domain and stage to a failure and maps project-owned codes to a symbolic ID, explanation and recommended next action.

Standard error output now includes:

```text
Error ID : FORENSIC_REPAIR_SOURCE_CHANGED
Stage    : pre-write source stability compare
Summary  : A source header changed after the scan.
Reason   : Current disk bytes no longer match the evidence used to build the plan.
Next step: Abort this plan and perform a fresh raw scan.
Raw code : -372
```

For raw IOP/driver errors the manager deliberately does **not** guess the meaning from a small negative number alone, because different PS2 I/O modules can reuse numeric values. The stage/context is shown and the raw code is preserved for logs.

This specifically improves cases such as bootstrap-source load failures: instead of treating a value such as `-4` as self-explanatory, the UI states that loading/opening `MBR.XIN/XLF` from the selected storage failed, recommends checking the selected target/file/media, and retains `-4` for low-level diagnosis.

Current catalog coverage includes:

- bootstrap source load/size/seek/allocation/short-read/capacity;
- KELF structural rejection;
- bootstrap pointer/payload bounds;
- normal payload/pointer write verification;
- `HDDRAW` single-master snapshot errors;
- exceptional master repair errors;
- `HDDMETA` forensic snapshot errors;
- forensic topology repair errors;
- startup APA/hybrid-GPT blocks;
- generic raw IOP/driver failures.

`tests/test_app_error.c` keeps representative mappings and the record/consume lifecycle host-testable.

## Hardware-validation checklist

Before declaring the UI portion of 0.4 complete, validate on a physical console that:

1. raw/forensic reads update the current LBA without materially increasing scan time;
2. bootstrap payload write/read-back visibly transitions WRITE -> FLUSH -> VERIFY;
3. deterministic master repair shows sectors 0-1 throughout the transaction;
4. multi-header repair clearly identifies each touched LBA and shows the LBA-0 master as the last commit point;
5. long error descriptions remain readable on the target video mode/TV;
6. a deliberately missing `MBR.XIN/XLF` produces a useful source-load explanation in addition to the raw code;
7. fault-injected `SOURCE_CHANGED`, compare-failure, snapshot-slot and bounds cases display the intended symbolic ID and recovery advice.
