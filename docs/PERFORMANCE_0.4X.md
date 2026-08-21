# 0.4.x EE and GS optimization record

This record keeps the optimization work measurable without presenting host
timings as physical-PS2 timings. The APA benchmark exercises identical portable
forensic fixtures compiled with `-O2`; the ELF measurements come from the
stripped R5900 artifacts produced by the pinned `ps2dev/ps2dev:v2.0.0` CI image.

## Invariants

Optimization does not change:

- APA evidence weights, confidence thresholds or repairability thresholds;
- fail-closed truncation and ambiguity behavior;
- snapshot, source-stability, write, flush and read-back ordering;
- the payload-first / pointer-last normal bootstrap transaction;
- the native `init_scr()` output as the startup and automatic fallback mode.

## Portable APA benchmark

`tests/test_apa_forensic` was built with `-O2` and executed 100 times on the
same host and fixtures.

| Revision | Wall time | User CPU | System CPU |
|---|---:|---:|---:|
| stable 0.4.0 implementation | 1.081 s | 0.850 s | 0.229 s |
| optimized 0.4.x implementation | 0.498 s | 0.255 s | 0.243 s |

The measured reduction is approximately 54% wall time and 70% user CPU. The
system component is intentionally almost unchanged because fixture process/file
overhead was not optimized.

The main changes are:

- impossible unreferenced grid candidates are rejected before the 256-word APA
  checksum walk;
- a sorted LBA index replaces repeated linear node lookup;
- compact bitsets replace repeated linear map-membership scans;
- accepted headers reuse the checksum already calculated during admission;
- exact same-disk comparison uses two immutable `memcmp` spans instead of a
  branch for every header byte.

These host figures demonstrate algorithmic improvement, not an exact R5900
speedup. Raw HDD RPC latency will still dominate much of a physical scan.

## R5900 release ELF

| Artifact | Stripped ELF | `text` | `data` | `bss` |
|---|---:|---:|---:|---:|
| stable 0.4.0 (`20cbe5f`) | 615,988 B | 279,952 B | 335,056 B | 2,977,120 B |
| optimized source, no LTO (`fe06bf2`) | 620,212 B | 284,040 B | 335,072 B | 2,977,184 B |
| optimized source with LTO (`84b829b`) | 612,276 B | 276,136 B | 335,076 B | 2,977,432 B |

Link-time optimization removes 7,936 bytes from the same optimized source. The
final build is 3,712 bytes smaller than stable 0.4.0 despite adding the mode
switcher, timed fallback and double-buffered presentation.

## GS/EE presentation work

- compatibility `scr_printf` screens are assembled and submitted once when
  they become interactive instead of rebuilding a full frame per line;
- blend state and per-string glyph color setup are cached;
- two 640x448 GS framebuffers are swapped on VBlank, eliminating writes into
  the buffer currently scanned by the display;
- stable GS environment registers are not resent on every frame and are
  refreshed only after a display-mode reset;
- one reusable GIF packet replaces a redundant pair because `end_frame()` waits
  for GS FINISH before returning, releasing 256 KiB of EE heap;
- experimental 480p renders the existing 640x224 layout into all 448 framebuffer
  lines through an exact 2x vertical expansion and automatically restores native
  output unless confirmed within ten seconds.

## Validation

- complete portable host suite: pass;
- 30 generated mounted-HDD fixture cases: pass;
- 9 sparse forensic raw-HDD fixture cases: pass;
- APA forensic and format tests under ASan/UBSan: pass;
- guarded hardware fault-injector self-test: pass;
- stripped R5900 build with PS2DEV v2.0.0 and LTO: pass.

True double buffering and 480p remain pending physical-console validation. The
native startup mode remains the previously validated default.
