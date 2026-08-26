# Corpus v2 Phase 0 real-hardware A/B protocol

Phase 0 exits only after the measurement build itself has been measured on a
real PlayStation 2. PCSX2 may be used for correctness/debugging but is not an
arbiter for EE cache, IOP scheduling, USB service latency, SIF DMA or DEV9
throughput.

## Compared builds

The authoritative instrumentation-overhead comparison is a same-source pair
built from one green commit on `perf/corpus-v2-integration` with the same PS2DEV
container, PS2SDK source, optimization flags and runtime implementation.

### A: PROFILE OFF

Build the selected commit with:

```text
HDL_PROFILE=0
```

This compiles the Phase-0 EE/IOP HDL telemetry out while retaining the same:

- direct-BDM source path;
- double-buffer prefetch worker and ownership flow;
- ps2hdd/HIOCTRANSFER target path;
- SIF DMA transfers;
- EE cache writeback/invalidation required for DMA correctness;
- SHA-256 verification;
- journal, flush, metadata commit and read-back semantics.

`HDL_STREAM_IOCTL2_GET_FAST_STATS` remains ABI-compatible and returns a zeroed
record, but the timed hot-path accounting itself is absent.

### B: PROFILE ON

Build the exact same commit with:

```text
HDL_PROFILE=1
```

This is the diagnostic build and retains:

- EE pump/source/target latency histograms;
- IOP direct-source/fallback/prefetch/HDD/SIF histograms;
- useful/DMA/cache-maintenance/fallback traffic counters;
- benchmark provenance artifact;
- linker/ELF audit artifacts.

CI emits both ELFs, linker maps, optimization audits and provenance records from
one checkout and one toolchain image. The profile mode is written explicitly to
each provenance file.

### Historical pre-instrumentation reference

Commit `4b5aa8d85e86c9de570a2128b52d1eaa5b334844` remains useful as the original
known-good HDL installer baseline, but it is **not** the authoritative profiler
overhead A/B. Runtime and Phase-1 policy changes after that commit make such a
cross-commit timing delta confounded.

## Hardware provenance

Record before each pair of runs:

```yaml
console_scp:
hardware_revision:
romver:
storage_adapter:
usb_device:
ps2sdk_commit:
toolchain:
active_irx:
build_flags:
hdl_profile_enabled:
workload:
direction:
buffering:
alignment:
sample_count:
units:
correctness_hash:
```

The CI-generated `BENCHMARK_PROVENANCE_PROFILE_OFF.yml` and
`BENCHMARK_PROVENANCE_PROFILE_ON.yml` supply build-side fields. Hardware fields
remain explicit manual measurements rather than guessed metadata.

## Workload contract

Use the same:

- console and adapters;
- HDD contents/layout before each timed run where practical;
- USB device, filesystem and USB port;
- ISO file and fragment layout;
- source direction and target operation;
- video mode and active background services;
- cold/warm policy.

Prefer an interleaved order such as `OFF, ON, ON, OFF` or `ON, OFF, OFF, ON`
rather than running every sample of one build first. This reduces temperature,
device-state and session-order bias. Recreate or otherwise control the target
layout between destructive install samples so the compared workload stays
meaningfully equivalent.

For HDL copy throughput, use one ISO large enough that startup/allocation noise
is negligible compared with the bulk copy phase. Do not compare different ISOs,
USB sticks or HDD layouts and call the result an instrumentation delta.

## Measurements

For **both** builds record at least:

- total install wall time;
- bulk copy wall time and useful KiB/s;
- payload verification wall time;
- final correctness result/hash;
- any cancellation, journal or metadata-commit failure.

For PROFILE ON additionally retain:

- p50, p95, p99 and max EE pump ioctl latency;
- p50, p95, p99 and max IOP direct-source latency;
- p50, p95, p99 and max prefetch consumer wait;
- p50, p95, p99 and max HDD write latency;
- p50, p95, p99 and max SIF DMA completion latency;
- prefetch hit/miss counts;
- fallback-source/fallback-target bytes;
- useful payload, SIF DMA and EE cache-maintenance bytes.

For the overhead result report PROFILE ON relative to PROFILE OFF for wall time
and throughput. Do not fabricate p95/p99 for PROFILE OFF from absent telemetry;
the point of the OFF build is to remove that instrumentation.

Run at least four complete comparable samples, preferably two in each half of an
interleaved order, for an initial engineering answer. Add samples if wall time
or PROFILE ON tail latency is unstable.

## Phase-0 acceptance

Phase 0 may be marked hardware-complete only when:

1. PROFILE OFF and PROFILE ON are from the same project SHA and pass the same
   correctness workload;
2. PROFILE ON produces internally consistent stage counters and traffic
   accounting;
3. the measurement overhead of PROFILE ON is quantified rather than assumed
   negligible;
4. PROFILE ON retains p50/p95/p99/max instead of replacing distributions with
   an average;
5. the result includes console/toolchain/IRX/workload provenance;
6. R5900 counter calibration is checked on the real EE before counter-derived
   optimization claims are made.

If instrumentation materially changes throughput or tail latency, keep
`HDL_PROFILE=0` as the performance build and use `HDL_PROFILE=1` only for
diagnosis. If the delta is negligible for the tested workload, that conclusion
still applies only to the recorded console/adapters/workload.

## R5900 counter calibration

Before using a counter event to justify code changes:

1. run an empty serialized scope and record harness overhead;
2. run a deterministic integer loop whose instruction/cycle relationship is
   intentionally simple;
3. repeat the scope several times and confirm stable monotonic results;
4. reject any run with PCR overflow;
5. measure one event pair at a time and do not compare events from different
   workloads as if they were simultaneous;
6. preserve a timer-only companion result.

Only after this calibration should cache miss, branch, single/dual issue or
instruction-completed counts be used as evidence for Phase 1/2 decisions.
