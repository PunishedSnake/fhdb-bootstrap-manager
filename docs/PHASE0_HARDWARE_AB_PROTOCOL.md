# Corpus v2 Phase 0 real-hardware A/B protocol

Phase 0 exits only after the measurement build itself has been measured on a
real PlayStation 2. PCSX2 may be used for correctness/debugging but is not an
arbiter for EE cache, IOP scheduling, USB service latency, SIF DMA or DEV9
throughput.

## Compared builds

### A: audited pre-instrumentation baseline

- project commit: `4b5aa8d85e86c9de570a2128b52d1eaa5b334844`
- purpose: known-good HDL installer immediately before corpus-v2 measurement
  instrumentation
- expected Phase-0 telemetry: absent

### B: corpus-v2 measurement build

Use the newest green commit on `perf/corpus-v2-integration` before accepting a
Phase-1 runtime optimization. The build must retain:

- EE pump/source/target latency histograms;
- IOP direct-source/fallback/prefetch/HDD/SIF histograms;
- useful/DMA/cache-maintenance/fallback traffic counters;
- benchmark provenance artifact;
- linker/ELF audit artifacts.

The project source, toolchain and HDL transaction semantics must otherwise stay
unchanged for the measurement comparison. Any Phase-1 change must be measured
separately and must not be folded into the Phase-0 overhead result.

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
workload:
direction:
buffering:
alignment:
sample_count:
units:
correctness_hash:
```

The CI-generated `BENCHMARK_PROVENANCE.yml` supplies build-side fields. Hardware
fields remain explicit manual measurements rather than guessed metadata.

## Workload contract

Use the same:

- console and adapters;
- HDD contents/layout before each timed run where practical;
- USB device, filesystem and USB port;
- ISO file and fragment layout;
- source direction and target operation;
- video mode and active background services;
- cold/warm policy.

For HDL copy throughput, use one ISO large enough that startup/allocation noise
is negligible compared with the bulk copy phase. Do not compare different ISOs,
USB sticks or HDD layouts and call the result an instrumentation delta.

## Measurements

For each build record at least:

- bulk copy wall time and useful KiB/s;
- p50, p95, p99 and max EE pump ioctl latency;
- p50, p95, p99 and max IOP direct-source latency;
- p50, p95, p99 and max prefetch consumer wait;
- p50, p95, p99 and max HDD write latency;
- p50, p95, p99 and max SIF DMA completion latency;
- prefetch hit/miss counts;
- fallback-source/fallback-target bytes;
- useful payload, SIF DMA and EE cache-maintenance bytes;
- final correctness result/hash;
- any cancellation, journal or metadata-commit failure.

Run at least three complete comparable samples for an initial engineering
answer. More samples are required if p95/p99 or wall time is unstable.

## Phase-0 acceptance

Phase 0 may be marked hardware-complete only when:

1. build A and build B both pass the same correctness workload;
2. B produces internally consistent stage counters and traffic accounting;
3. the measurement overhead of B is quantified rather than assumed negligible;
4. p50/p95/p99/max are retained, not replaced by an average;
5. the result includes console/toolchain/IRX/workload provenance;
6. R5900 counter calibration is checked on the real EE before counter-derived
   optimization claims are made.

If instrumentation materially changes throughput or tail latency, keep a
companion non-instrumented performance build and use the measurement build only
for diagnosis.

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
