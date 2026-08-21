# Live HDD status and contextual errors (0.4 Michishirube)

Michishirube exposes two complementary diagnostic surfaces: a live HDD activity monitor for long/raw operations, and contextual error descriptions that preserve the original numeric return code while explaining what the failing stage was trying to do.

## Live HDD monitor

The PS2-specific `disk_status_ps2` layer is fed by the actual HDD transports and recovery writers. It is presentation-only: it does not decide whether an operation is allowed and cannot weaken a recovery gate.

The monitor shows:

- current high-level operation;
- current action/stage;
- current semantic location/device;
- current I/O kind (`READ`, `WRITE`, `VERIFY`, `FLUSH`, `POINTER UPDATE`, `SCAN`);
- current LBA and sector range when raw HDD access exists;
- current physical-disk position when `HDIOC_TOTALSECTOR` is available;
- progress bar/percentage;
- explicit write-sensitive warning during destructive phases.

### VBlank synchronization

Physical testing found visible screen tearing during a large forensic scan because status frames were being submitted much faster than the display refresh.

0.4.0 synchronized status-frame submission with `graph_wait_vsync()`. The
0.4.x renderer now goes one step further: every complete application frame is
drawn into an off-screen native buffer, then the visible framebuffer is swapped
on VBlank.

It deliberately does **not** wait for one VBlank after every raw disk read. The healthy large-disk release test performed 14,905 grid reads; serializing every read to 50/60 Hz would turn UI synchronization into the dominant scan cost.

Instead:

- high-rate `DISK_STATUS_READ` telemetry is coalesced and the newest state is presented every 32 ordinary read events;
- WRITE / VERIFY / FLUSH / POINTER UPDATE and semantic phase changes remain immediate;
- every complete frame becomes visible through a VBlank framebuffer swap;
- disk I/O is therefore not artificially limited to the display frame rate.

Physical retesting confirmed that this removed the visible forensic-scan tearing. The status panel refreshes less frequently during rapid reads, as intended, while remaining responsive for write-sensitive transitions.

The renderer keeps a dedicated 640x224 pair for native output and a dedicated
720x448 pair for the optional progressive application mode. This preserves the
correct read-circuit stride in both modes. The 3.75 MiB GS allocation removes
the tearing race without weakening I/O throughput and leaves 256 KiB free.

## Current status coverage

The monitor is wired into:

- startup HDD admission/master validation;
- active bootstrap payload reads;
- boot-chain diagnostics and PFS/MC evidence stages;
- header/full-rescue backup preparation and persistence;
- bootstrap source loading/validation;
- MagicGate signing stages;
- bootstrap payload WRITE -> FLUSH -> VERIFY;
- `HDIOC_SETOSDMBR` pointer updates and verification;
- deterministic structure-health assessment;
- `HDDRAW` snapshot creation/read-back;
- exceptional sectors-0/1 master recovery;
- `HDDMETA` snapshot creation/read-back;
- forensic raw scanning;
- forensic multi-header topology repair, including source-stability checks, interior writes, master-last commit, immediate read-back, and final touched-set verification.

High-level controllers provide the semantic action/location. Low-level transport adds the exact LBA/range. This keeps a raw read meaningful to the user instead of showing only a context-free sector number.

## Contextual error catalog

Existing numeric return values remain unchanged. The `app_error` layer attaches a domain and stage to a failure and maps project-owned codes to a symbolic ID, explanation and recommended next action.

Standard error output can include:

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

## Release validation state

The 0.4.0 UI/status path has been exercised on physical PS2 hardware for:

- full-screen native GS rendering;
- locked-state readability;
- live themes/config;
- long forensic scan telemetry;
- VBlank synchronization with no observed scan-time tearing;
- diagnostics/backup/source-load status;
- contextual negative-path error presentation.

Exceptional raw repair statuses are instrumented but remain part of the release's experimental destructive-recovery surface until broader independent tests exist.
