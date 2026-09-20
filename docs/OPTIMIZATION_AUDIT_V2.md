# fhdb-bootstrap-manager: PS2 Optimization Research Library v2 audit

Audit snapshot: 2026-08-25

Target branch: `feature/hdl-game-installer`

This document applies the complete PS2 Optimization Research Library v2 to the
runtime project. It is deliberately not a list of generic compiler tricks. The
library's source-of-truth routing and optimization order are used throughout:

1. remove unnecessary work;
2. do work less often;
3. reduce data volume;
4. improve layout/locality;
5. batch;
6. remove copies and dynamic allocations;
7. buffer and overlap;
8. use specialized hardware;
9. hand-specialize a measured hot kernel last.

## Epistemic labels

- **POTWIERDZONE**: hardware manual, current source, or real-hardware reproduction.
- **CURRENT IMPLEMENTATION**: behavior/structure of the inspected project or current PS2DEV source.
- **HISTORYCZNE**: old stack/toolchain/forum evidence, not a current limit.
- **INFERENCJA**: architectural conclusion not yet measured on the target console.
- **HIPOTEZA DO TESTU**: candidate optimization that requires A/B hardware measurement.

## Source-of-truth routing

The audit follows `PS2_Optimization_Library_v2_MANIFEST.md` first. Relevant
authoritative sources are then routed as follows:

| Domain | Primary corpus |
| --- | --- |
| general performance decisions | `PS2_PERFORMANCE_BIBLE.md` |
| R5900/cache/MMI/codegen | `MIPS_R5900_optimization_research_corpus_v2.md` |
| data layout/lifetime/producer-consumer | `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md` |
| linker/ABI/code layout | `PS2_Linker_ELF_ABI_Layout_optimization_research_corpus.md` |
| allocators/alignment | `PS2_Memory_Allocators_optimization_research_corpus_v2.md` |
| IOP/SIF | `PS2_IOP_SIF_optimization_research_corpus_v2.md` |
| USB | `PS2_USB_1_1_optimization_research_corpus.md` |
| HDD/APA/PFS/HDL | `PS2_HDD_APA_PFS_HDL_filesystem_optimization_research_corpus_v2.md` |
| DMAC/RDRAM/Scratchpad/Main Bus | `PS2_DMAC_RDRAM_Scratchpad_MainBus_optimization_research_corpus_v2.md` + base corpus |
| whole-system overlap/contention | `PS2_Whole_System_Scheduling_research_corpus_v2.md` |
| current SDK behavior | `PS2_PS2SDK_optimization_research_corpus_v2.md` + current source |
| GS | `PS2_Graphics_Synthesizer_optimization_research_corpus_v2.md` |
| VU | `PS2_VU0_VU1_optimization_research_corpus_v2.md` |
| VIF | `PS2_VIF_optimization_research_corpus.md` |
| IPU | `PS2_IPU_optimization_research_corpus.md` |
| fast math | `PS2_Fast_Math_libc_math_optimization_research_corpus.md` |

VU/VIF/IPU/MMI/Scratchpad are not checklist items. They are only candidates if
the measured workload has the correct access pattern and system-level placement.

## Audit coverage

The CI corpus audit currently covers:

- 117 runtime source/header/include files under `src/`, `include/`, and `iop/`;
- 17,266 runtime source lines;
- approximately 493 C functions;
- 30 test/tool/CI files and 6,556 support lines;
- the final unstripped EE ELF after LTO;
- the final linker map;
- the effective R5900 GCC target settings.

`third_party/` is excluded from source-policy findings, but code pulled from
third-party/SDK archives remains visible in the final ELF and linker-map audit.

## Build policy

### CURRENT IMPLEMENTATION

The EE baseline is:

```text
-O2 -flto -G0
-ffunction-sections -fdata-sections
--gc-sections
```

The current PS2DEV toolchain already targets/tunes R5900 and enables the R5900
erratum workaround. No global `-O3`, `-Ofast`, `-ffast-math`, loop-unroll or
non-default small-data flag is present.

### Assessment

**POTWIERDZONE / keep.** This is a good corpus-compliant baseline. Per-TU `-Os`
or `-O3` remains an A/B experiment after profiling, not a new global default.

The build now preserves `PS2_HDD_BOOTSTRAP_MANAGER.map` in CI because final code
placement and archive provenance are performance evidence on a 16 KiB I-cache.

---

# 1. Highest-priority findings

## P0-A: measure the USB install pipeline by stage

### CURRENT IMPLEMENTATION

The custom `hdl_stream.irx` already implements the correct broad architecture:

```text
USB BDM fragment-map read
        -> IOP 64 KiB double buffers
        -> ps2hdd HIOCTRANSFER
        -> one-way SIF DMA to EE for SHA-256
```

It starts the next USB read before the current buffer is consumed by HDD/SIF.
This eliminates the former payload round trip `IOP -> EE -> IOP`.

### Corpus result

IOP/SIF v2 requires storage throughput to be decomposed into:

```text
device -> IOP
IOP copy/work
IOP -> EE SIF
EE consumer
```

and USB performance must measure each stage instead of inferring the bottleneck
from total MB/s.

### HIPOTEZA DO TESTU

Add low-overhead stage telemetry for at least:

```text
USB BDM read time
prefetch wait time
HDD HIOCTRANSFER time
SIF DMA submit/completion time
EE SHA-256 time
whole chunk time
```

Also report:

```text
direct BDM reads
fallback reads
prefetch hits/misses
bytes copied by CPU
DMA bytes
cache-maintenance bytes
useful payload bytes
```

This is the next performance task before changing the 64 KiB block size again.

## P0-B: remove unused libdraw 2D/trigonometry footprint

### CURRENT IMPLEMENTATION

The application UI uses simple GS rectangles, outlines and textured glyph
rectangles. However it links PS2SDK `draw2d.o` to obtain those helpers and the
blend-state switch.

Current `draw2d.o` also contains arc/rounded-rectangle helpers. Those depend on
`sinf/cosf`, which pulls a non-trivial libm range-reduction chain into the final
ELF even though the manager UI does not use arcs.

### INFERENCJA

A tiny project-local GS packet builder for the three required 2D primitives can
remove a meaningful amount of cold text and libm code. This is primarily an
I-cache/code-footprint optimization, not an expected USB-throughput change.

### HIPOTEZA DO TESTU

A/B:

```text
A = current libdraw draw2d linkage
B = minimal local rectangle/glyph packet emission
```

Measure:

```text
.text bytes
final named-function bytes
libm symbols retained
I-cache events during UI redraw
frame/update time
pixel/correctness screenshots on real PS2
```

Do not copy PS2SDK implementation text blindly. Build the minimal packet path
from the GS/GIF contract or change the upstream compilation strategy cleanly.

## P0-C: formatting/runtime footprint

### CURRENT IMPLEMENTATION

The source audit finds 416 formatting calls/references. The final ELF contains:

```text
_svfprintf_r    14264 B
__ssvfiscanf_r   8776 B
_dtoa_r          6868 B
_vfiprintf_r     6160 B
```

This is especially expensive in a program whose R5900 I-cache is only 16 KiB.

### Important current-source discovery

The `scanf` machinery is not evidence that application code calls `scanf`.
Current PS2SDK fileXio libcglue retains `__fileXioGetstatHelper`; that helper
converts IOP timestamps through `mktime()`. The time-zone path pulls formatted
input support into the image. Therefore simply deleting the application's
`fileXioGetStat()` call does not prove that `__ssvfiscanf_r` will disappear.

### Plan

1. separate application formatter cost from PS2SDK libcglue cost using the map;
2. replace repeated fixed string copies currently written through `snprintf`;
3. design a bounded project formatter for the actually needed integer/string
   formats;
4. evaluate an upstream/current-source-compatible way to avoid the fileXio
   `mktime -> tzset -> siscanf` chain when POSIX timestamps are irrelevant.

Step 4 is an API/runtime change and must not be silently patched without tests.

## P0-D: giant cold/control functions and I-cache

### CURRENT IMPLEMENTATION

Largest project functions in the final ELF include approximately:

```text
diagnostics_controller_refresh  8684 B
repair_plan_screen               6440 B
execute_transaction              6420 B
main                             6324 B
bootstrap_controller_install     4756 B
open_new_install                 4364 B
bootstrap_menu                   4200 B
```

### INFERENCJA

Several are control/UI/error-heavy and do not deserve to share one huge hot
working set. Selective `noinline`, explicit hot/cold helper boundaries and
per-TU `-Os` experiments are likely higher-value than aggressive inlining.

### Rule

Do not split merely to reduce source lines. Use final machine-code size and
hardware I-cache/cycle counters to judge the result.

## P0-E: 64-bit divide/modulo helpers

### CURRENT IMPLEMENTATION

The final ELF still contains large software helpers including `__udivdi3` and
`__umoddi3`; `execute_transaction()` directly calls the unsigned 64-bit divide
helper three times and modulo helper once.

### HIPOTEZA DO TESTU

Inspect each call site. Replace only cases where the arithmetic contract proves
a cheaper operation is equivalent, e.g. a power-of-two divisor or safe
quotient/remainder formulation. Do not run a global textual `/ -> >>` rewrite.

---

# 2. Runtime subsystem review

## `main.c`, `platform.c`

### CURRENT IMPLEMENTATION

Bootstraps IOP modules, SIF/fileXio/pad, video/GS and application controllers.
`main` is already a large final function.

### Assessment

- keep initialization correctness and module ordering;
- isolate one-time boot diagnostics and error presentation from steady menu path;
- retain bounded hardware waits/timeouts;
- do not increase thread priorities without deadline evidence;
- record loaded IRX versions/hashes and IOP RAM budget in benchmark builds.

### Priority

**P1 code locality**, not a compute micro-kernel.

## `manager_menu_ps2.c`, `app_ui_ps2.c`, controllers

Includes:

```text
manager_menu_ps2.c
app_ui_ps2.c
diagnostics_controller_ps2.c
bootstrap_controller_ps2.c
forensic_controller_ps2.c
repair_controller_ps2.c
```

### CURRENT IMPLEMENTATION

These are branch-heavy, user-paced and formatting-heavy. They are cold compared
with bulk disk/USB movement but can dominate `.text` and I-cache when active.

### Corpus mapping

R5900 + Linker + DOD:

- hot/cold splitting;
- smaller active code working set;
- avoid specialization explosion;
- prefer `-Os` experiments for cold/control TUs.

### Priority

**P1 footprint/locality.** No VU/MMI justification.

## `gs_ui_ps2.c`, `ui_layout.c`, `ui_font.c`, `spleen_font_data.c`

### CURRENT IMPLEMENTATION

The renderer maintains a fixed packet pool, prebuilt font atlas, two frame
buffers where appropriate, explicit GS waits with timeouts, and calibrated
mode-specific layouts. Font atlas DMA/cache visibility is explicit.

### Findings

1. libdraw 2D dependency is the main immediate code-footprint target.
2. existing `draw_state_dirty`/blend caching is good state-change hygiene.
3. the status UI must remain rate-limited during bulk I/O; rendering every
   small storage chunk can put VBlank/GIF waits back on the transfer path.
4. font atlas `aligned(64)` is a cache/DMA-specific contract, not precedent for
   global alignment.
5. VU/VIF is not justified for this UI.

### Priority

**P0 code-footprint dependency**, **P1 redraw/batching telemetry**.

## `storage.c`

### CURRENT IMPLEMENTATION

Generic fileXio helpers contain several near-identical complete read/write
loops. `read_bounded_file()` uses a variable-size heap allocation. Config and
log paths are cold/user-triggered.

### Findings

- `write_whole_file()` and `append_log_file()` share the same write-until-done
  loop;
- `read_exact_file()`, `read_bounded_file()` and `read_text_file()` share much
  of the read-until-done machinery;
- `path_exists()` uses fileXio metadata, but removing it alone does not remove
  PS2SDK's getstat libcglue dependency;
- `read_bounded_file()` allocation is acceptable until a real workload shows
  allocator latency/fragmentation pressure.

### Plan

Create small internal exact-read/exact-write primitives if final ELF A/B shows a
code-size benefit. Preserve short-transfer and error semantics.

### Priority

**P1 code size/maintainability**, not a throughput emergency.

## APA core: `apa.c`, `apa_forensic.c`, `apa_repair.c`

### CURRENT IMPLEMENTATION

Portable parsers and validation/recovery logic are intentionally conservative.
Forensic paths are large and cold; correctness matters more than shaving a few
integer instructions.

### Corpus mapping

HDD v2 prefers one-pass compact APA records and avoiding repeated scans. DOD
prefers contiguous compact records over pointer-rich metadata.

### Plan

- keep forensic and normal fast-list paths separate;
- avoid storing full 1 KiB APA headers for UI when a compact record suffices;
- instrument headers read, repeated headers, bytes read and wall time;
- only coalesce adjacent raw header reads after a trace proves useful adjacency.

### Priority

**P1 normal catalogue dataflow**, **P2 forensic micro-optimization**.

## HDD access/recovery: `hdd_read.c`, `hdd_write.c`, `hdd_repair_ps2.c`, `hdd_forensic_repair_ps2.c`, `hdd_recovery_wrap.c`

### Findings

- 64-byte-aligned buffers occur frequently and must be classified individually
  as cache-line, SIF, raw-device or merely historical assumptions;
- large sequential payload reads/writes should use stable DMA, large/coalesced
  requests and overlap before CPU micro-tuning;
- repair barriers/sync points are correctness checkpoints and must not be
  removed for throughput without proof;
- report p95/p99/max operation latency for repair/storage operations where
  stalls matter.

### Priority

**P1 alignment-contract audit and transfer accounting.**

## Snapshots/rescue: `forensic_snapshot.c`, `repair_snapshot.c`, `rescue_image.c`, `rescue_storage.c`, `header_backup.c`

### CURRENT IMPLEMENTATION

These paths allocate/copy relatively large image/snapshot buffers and are
mostly user-triggered/cold.

### DOD/allocator review

For each large buffer record:

```text
producer
consumer
lifetime
representation
alignment
transport
copy budget
ownership state
```

Potential improvement is often eliminating an intermediate full-image copy or
using a bounded arena for one transaction, not replacing Newlib malloc globally.

### Priority

**P1 peak RAM/copy amplification**, **P2 allocator specialization**.

## Boot/KELF/reporting: `boot_chain*`, `boot_payload*`, `boot_report*`, `kelf.c`, `capsule_format.c`, `mbr_compat.c`

### CURRENT IMPLEMENTATION

Mostly bounded parsers, validation, classification and text/report generation.
Formatting dominates many source-level review hits.

### Plan

- replace literal-only formatter use with bounded copy helpers;
- keep parsing structs compact and sequential;
- combine repeated endian helpers only if final machine code does not already
  merge them;
- avoid fast-math/MMI/VU: no matching workload.

### Priority

**P1 formatter/code footprint**, otherwise **P2**.

## `sha256.c`

### CURRENT IMPLEMENTATION

SHA-256 is a real bulk kernel used for correctness and install verification.
The dev20 pipeline hashes source data while it is already being transferred.

### Assessment

This is a legitimate R5900/MMI candidate only if timing shows SHA remains on the
critical path instead of being hidden by USB/HDD. First collect cycles/byte and
cache events. Then test a scalar GCC baseline against a carefully bounded MMI
input/schedule implementation.

### Priority

**P1 profiler**, **P2 MMI/assembly experiment**.

---

# 3. HDL Tools review

## `hdl_tools/fast_io.inc`

### CURRENT IMPLEMENTATION

Raw APA traversal replaced stock repeated fileXio metadata walking for large
HDL catalogues/preflight. It uses compact information and hardware-tested LBA
handling.

### Assessment

This matches the HDD v2 priority order: remove repeated scans before tuning the
parser. Next improvements are trace-driven prefetch/coalescing and persistent
indexing, not MMI.

## `hdl_tools/catalog.inc`

### CURRENT IMPLEMENTATION

One raw scan emits installed-game records for the EE UI.

### Next architectural win

A persistent `games.bin`-like compact index with safe invalidation can remove
full scans from normal menu startup. The corpus-recommended design includes at
least version/magic, drive identity/generation evidence, APA chain checksum and
entry CRC, with full-scan fallback on mismatch.

### Priority

**P1**, potentially large on very large APA libraries.

## `hdl_tools/source_ui.inc`

### CURRENT IMPLEMENTATION

`mass:` discovery builds a dynamic list, grows storage and sorts it. This is
user-triggered and typically much smaller than the ISO payload.

### Plan

Measure directory entry count, allocation count, largest list capacity and scan
latency before introducing pools/arenas. A compact fixed-capacity or geometric
growth vector is preferable if allocation churn is observed.

### Priority

**P2 unless large USB directories reproduce a problem.**

## `hdl_tools/transaction.inc`

### CURRENT IMPLEMENTATION

`execute_transaction()` is both a large final machine-code function and the
transaction state machine for allocation/copy/verify/metadata/recovery.

### Plan

Split stage handlers along semantic transaction boundaries while keeping the
journal/error contract unchanged. This should improve code locality and make
per-stage timing natural. Afterwards audit the remaining 64-bit division/modulo
helpers and re-run LTO/ELF measurements.

### Priority

**P0/P1.** Structural split before hand optimization.

## copy/verify path

### CURRENT IMPLEMENTATION

The transaction code now uses the custom IOP streaming service for the main
fast path. Metadata is exposed only after payload verification and final commit.

### Assessment

Correct ordering must remain:

```text
allocate
-> invalidate stale HDL metadata
-> copy
-> durability boundary
-> verify
-> metadata commit last
-> readback
-> complete/remove journal
```

The next speed work belongs in stage measurement and IOP/USB service overlap,
not by weakening verification.

---

# 4. IOP `hdl_stream.irx`

## CURRENT IMPLEMENTATION

The module is deliberately a service CPU pipeline, not an EE-like compute
worker. Important architecture already present:

- BDM fragment map of the USB ISO;
- physical source reads on IOP;
- two 64 KiB staging buffers;
- prefetch worker;
- next read submitted before HDD/SIF consumption of the current block;
- direct ps2hdd transfer;
- one-way SIF DMA for the EE checksum;
- fallback path with counters;
- no payload round trip from EE back to IOP.

## Corpus assessment

This is strongly aligned with IOP/SIF v2 and DOD v2: keep data near the final
IOP-side HDD consumer and treat SIF as the data plane only for the data that EE
actually needs.

## Remaining risks

- the IOP has about 2 MiB RAM, so staging growth must be justified by a memory
  budget rather than instinct;
- `sceSifDmaStat()` polling exists in the EE delivery path and may or may not be
  material;
- blocking DEV9/HIOCTRANSFER sections need timing rather than visual diagnosis;
- priority changes are forbidden until measured queue/deadline behavior exists.

## Next work

Add stage timers/counters first. Then sweep block size and prefetch depth only if
the timings identify USB command gaps, SIF completion, HDD write or CPU work as
the limiter.

---

# 5. Duplicate code/function audit

## CURRENT IMPLEMENTATION

Source-level exact normalized duplicates currently include small endian/access
helpers such as multiple `read_le32`/`write_le32` implementations and a few
case-insensitive compare helpers.

The final machine-code audit does **not** show large project-specific identical
function bodies surviving LTO. Therefore source duplication is currently more
of a maintenance/API consistency issue than a proven runtime cost.

## Rule

Do not create a generic abstraction that adds calls/pointer chasing simply to
make the source look DRY. Consolidate only where it preserves/increases
locality, reduces final code size or fixes a shared correctness contract.

---

# 6. Alignment audit

## CURRENT IMPLEMENTATION

The source scanner finds 23 explicit `aligned(64)` objects/paths.

## Corpus rule

Every one must be categorized as one of:

```text
allocator alignment
R5900 cache-line placement
EE DMAC/SIF visibility
IOP/device DMA alignment
packet/format alignment
```

A 64-byte cache line is not a universal allocator contract. This audit will not
replace those 23 sites with either smaller or larger alignment en masse.

---

# 7. Specialized hardware applicability

## R5900/MMI

Potentially applicable only to measured bulk kernels such as SHA-256, large
block compare/packing, or future image transforms. Not a general application
rewrite target.

## Scratchpad

No current path earns a Scratchpad rewrite yet. A 16 KiB explicit working set
would add transport/ownership complexity. Test only after cache counters show a
specific reusable streaming tile is D-cache bound.

## VU0/VU1

No current manager/control/storage workload justifies VU offload. A VU path is
not planned merely to satisfy corpus coverage.

## VIF

No current geometry/packed-VU stream exists. Not applicable to the storage
manager's primary workload.

## IPU

No MPEG/image-decode workload on the install critical path. Not applicable.

## GS

Relevant only to the UI renderer and status-update scheduling. Optimize packet
state/footprint/waits; do not turn a recovery UI into a geometry engine.

## SPU2/network

Not active manager workloads today. Their corpus rules matter mainly for future
coexistence/contention tests if those services become active.

---

# 8. CI and benchmark infrastructure now required

Every performance build should preserve:

```text
PS2_HDD_BOOTSTRAP_MANAGER.ELF        stripped runnable image
PS2_HDD_BOOTSTRAP_MANAGER.map        linker provenance/layout
OPTIMIZATION_AUDIT.txt               final machine-code audit
CORPUS_V2_PROJECT_AUDIT.txt          source/dataflow review triggers
GCC_R5900_TARGET.txt                 effective toolchain target
```

Future hardware benchmark records should add:

```text
SCPH / hardware revision
PS2SDK commit
toolchain image/version
active IRX list + hashes
storage adapter/media
USB device
workload/direction
alignment/buffering/chunk size
sample count
p50/p95/p99/max
correctness hash
```

For the installer specifically record both throughput and stage-tail latency.

---

# 9. Ranked optimization backlog

## P0: do next

1. **Add stage timing to dev20 USB/IOP/SIF/HDD/EE pipeline.**
2. **A/B a minimal GS 2D packet path to drop unused `draw2d` trig/libm code.**
3. **Split `execute_transaction()` by transaction stage and re-audit codegen.**
4. **Map and remove avoidable formatting/runtime footprint without changing error handling.**
5. **Inspect every `execute_transaction()` 64-bit divide/mod call site.**

## P1: after P0 measurements

6. Per-TU `-Os` experiments for cold UI/diagnostic/recovery controllers.
7. Persistent, validated compact HDL catalogue index.
8. Buffer lifetime/copy-amplification audit for forensic/rescue images.
9. Explicit alignment-contract table for all 23 aligned(64) sites.
10. R5900 performance-counter regions for SHA, APA parse, UI redraw and
    transaction stages.
11. Investigate current PS2SDK fileXio timestamp conversion footprint and an
    upstream-compatible smaller path if timestamps are not consumed.

## P2: only after proof

12. MMI/hand-assembly SHA or block helpers.
13. Scratchpad staging experiment.
14. Profile-guided function/section ordering.
15. More aggressive compiler/autotuning matrix.

VU/VIF/IPU are deliberately absent from the automatic backlog because the
current workload does not fit them.

---

# 10. Definition of success

This audit is complete only when optimization is expressed as an end-to-end
producer/consumer graph, not a bag of instruction tricks.

For every significant data path we want:

```yaml
producer:
consumer:
lifetime:
representation:
alignment:
transport:
batch_size:
deadline:
ownership_states:
copy_budget:
validation:
```

For the current HDL install path, the target is the minimum unhideable time from
USB producer to final HDD consumer while preserving the journal, verification,
metadata-last commit rule and compatibility with real OPL/hardware.

After every major improvement, re-profile the whole system because the
bottleneck is expected to move.
