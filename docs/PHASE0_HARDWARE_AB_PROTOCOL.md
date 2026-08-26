# Corpus v2 Phase 0 real-hardware A/B protocol

Phase 0 exits only after the measurement build itself has been measured on a
real PlayStation 2. PCSX2 may be used for correctness/debugging but is not an
arbiter for EE cache, IOP scheduling, USB service latency, SIF DMA or DEV9
throughput.

## Frozen hardware-test pair

Until a newer green pair is explicitly recorded here, use the artifacts from CI
run #666, project head:

```text
project_git_sha  7875b14d837d6332f5edc37f1c12a55527d7dd87
project_git_ref  perf/corpus-v2-integration
ps2sdk_commit    b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b
toolchain        mips64r5900el-ps2-elf GCC 15.2.0
container        ps2dev/ps2dev:v2.0.0
```

PROFILE ON:

```text
ELF bytes      638388
ELF SHA-256    964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7
IRX bytes        9861
IRX text         8595
IRX data          144
IRX SHA-256     8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db
```

PROFILE OFF:

```text
ELF bytes      632884
ELF SHA-256    4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916
IRX bytes        8405
IRX text         7139
IRX data          144
IRX SHA-256     f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
```

The static footprint delta is evidence that the compiled-out path is real; it is
**not** a runtime speedup measurement. CI also enforces that the ON/OFF
`hdl_stream.irx` files are not byte-identical.

An earlier pair through CI #661 is invalid for IOP profiler-overhead measurement:
PS2SDK's IOP rules place objects under `obj/`, while the old top-level clean did
not remove that directory. PROFILE OFF therefore reused the previously compiled
PROFILE ON `hdl_stream.o`. The corrected module Makefile makes the profile mode
part of the dependency graph with `obj/profile-0/` and `obj/profile-1/`, and its
own clean removes the entire object tree. Do not use the old OFF ELF/IRX as an
authoritative profiler-overhead sample.

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

CI emits both ELFs, both embedded `hdl_stream.irx` variants, linker maps,
optimization audits, binary hashes and provenance records from one checkout and
one toolchain image. The profile mode and the final ELF/IRX hashes are written
explicitly to each provenance file.

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
benchmark_elf_sha256:
hdl_stream_irx_sha256:
workload:
direction:
buffering:
alignment:
sample_count:
units:
correctness_hash:
```

The CI-generated `BENCHMARK_PROVENANCE_PROFILE_OFF.yml` and
`BENCHMARK_PROVENANCE_PROFILE_ON.yml` supply build-side fields, including exact
ELF/IRX hashes and sizes. Hardware fields remain explicit manual measurements
rather than guessed metadata.

Before timing, verify the ELF SHA-256 on the medium used to launch each build.
A filename such as `PROFILE_ON.ELF` is not provenance.

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

Run at least four complete comparable samples per mode, preferably distributed
through an interleaved order. Add samples if wall time or PROFILE ON tail latency
is unstable.

## Host comparison record

Store the comparable run timings in a JSON array. Every sample must include:

```json
{
  "mode": "OFF",
  "project_git_sha": "7875b14d837d6332f5edc37f1c12a55527d7dd87",
  "workload_id": "<stable workload/device/layout identifier>",
  "correctness_hash": "<same verified result for every sample>",
  "source_bytes": 0,
  "total_us": 0,
  "copy_us": 0,
  "verify_us": 0
}
```

`copy_us` and `verify_us` are optional if the external measurement setup cannot
isolate those phases. `source_bytes` and `total_us` are mandatory. Compare the
record with:

```text
python3 tools/compare_hdl_profile_ab.py samples.json --output profile-ab.json
```

The comparator refuses mixed project SHAs, workloads, correctness hashes or
source sizes and requires four samples of each mode by default. It reports
p50/p95/p99/max for available wall-time metrics plus signed PROFILE ON vs OFF
percent deltas. Throughput is derived from the recorded bytes and time. Rich EE
and IOP latency distributions remain sourced from `parse_hdl_perf.py` for
PROFILE ON only.

## Phase-0 acceptance

Phase 0 may be marked hardware-complete only when:

1. PROFILE OFF and PROFILE ON are from the same project SHA and their recorded
   ELF/IRX hashes match the selected CI pair;
2. both builds pass the same correctness workload;
3. PROFILE ON produces internally consistent stage counters and traffic
   accounting;
4. the measurement overhead of PROFILE ON is quantified rather than assumed
   negligible;
5. PROFILE ON retains p50/p95/p99/max instead of replacing distributions with
   an average;
6. the result includes console/toolchain/IRX/workload provenance;
7. R5900 counter calibration is checked on the real EE before counter-derived
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
