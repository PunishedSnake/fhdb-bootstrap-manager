# Corpus v2 Phase 0 real-hardware A/B protocol

Phase 0 exits only after the measurement build itself has been measured on a
real PlayStation 2. PCSX2 may be used for correctness/debugging but is not an
arbiter for EE cache, IOP scheduling, USB service latency, SIF DMA or DEV9
throughput.

## Frozen hardware-test pair

The binary identity frozen by CI run #666 originates from project head:

```text
frozen_source_git_sha  7875b14d837d6332f5edc37f1c12a55527d7dd87
project_git_ref        perf/corpus-v2-integration
ps2sdk_commit          b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b
toolchain              mips64r5900el-ps2-elf GCC 15.2.0
container              ps2dev/ps2dev:v2.0.0
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

The exact ELF/IRX hashes define the frozen binary pair. Later commits that touch
only documentation or host-side tooling may be rebuilt by CI into byte-identical
ELFs. In that case the CI provenance correctly records the newer artifact
`project_git_sha`, while the binary identity remains the hash pair above. A run
record must therefore preserve both:

1. the `project_git_sha` from the artifact actually tested;
2. the mode-specific frozen ELF/IRX hashes.

Do not rewrite the artifact SHA to `7875b14...` merely because the bytes match
the original CI #666 pair.

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

The authoritative instrumentation-overhead comparison is a same-runtime-source
pair built from one green artifact head with the same PS2DEV container, PS2SDK
source, optimization flags and runtime implementation. The frozen hashes above
are the final guard against accidentally testing a different binary.

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

Build the exact same artifact head with:

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

## Preflight before copying to the console

Run the host-side guard on the exact two ELF files selected for the test:

```text
python3 tools/phase0_profile_pair_preflight.py \
  --profile-on PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_ON.ELF \
  --profile-off PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF \
  --project-git-sha <project_git_sha from that CI artifact> \
  --output-template PHASE0_AB_SAMPLES_TEMPLATE.json
```

The command fails if either ELF size or SHA-256 differs from the frozen pair. It
also creates the eight-run interleaved sample template with the artifact SHA and
mode-specific ELF/IRX hashes already filled in. Do not hand-edit those hashes.

After copying the files to the launch medium, verify the ELF hashes there as
well. A filename such as `PROFILE_ON.ELF` is not provenance.

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
project_git_sha:
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
artifact project SHA, ELF/IRX hashes and sizes. Hardware fields remain explicit
manual measurements rather than guessed metadata.

## Workload contract

Use the same:

- console and adapters;
- HDD contents/layout before each timed run where practical;
- USB device, filesystem and USB port;
- ISO file and fragment layout;
- source direction and target operation;
- video mode and active background services;
- cold/warm policy.

Use the generated eight-run order:

```text
OFF, ON, ON, OFF, ON, OFF, OFF, ON
```

rather than running every sample of one build first. This distributes both modes
through the session and reduces temperature, device-state and session-order
bias. Recreate or otherwise control the target layout between destructive
install samples so the compared workload stays meaningfully equivalent.

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

Run at least four complete comparable samples per mode. Add samples if wall time
or PROFILE ON tail latency is unstable.

## Host comparison record

Use the `PHASE0_AB_SAMPLES_TEMPLATE.json` emitted by preflight. Each sample has
this shape:

```json
{
  "run": 1,
  "mode": "OFF",
  "project_git_sha": "<artifact head actually tested>",
  "benchmark_elf_sha256": "4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916",
  "hdl_stream_irx_sha256": "f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751",
  "workload_id": "<stable workload/device/layout identifier>",
  "correctness_hash": "<same verified result for every sample>",
  "source_bytes": 0,
  "total_us": 0,
  "copy_us": 0,
  "verify_us": 0
}
```

`copy_us` and `verify_us` are optional if the external measurement setup cannot
isolate those phases. `source_bytes` and `total_us` are mandatory. Replace every
zero placeholder before comparison, otherwise input validation rejects it.

Compare the completed record with:

```text
python3 tools/compare_hdl_profile_ab.py PHASE0_AB_SAMPLES_TEMPLATE.json \
  --output profile-ab.json
```

The comparator refuses:

- mixed artifact project SHAs;
- an ELF or IRX hash not belonging to the frozen mode-specific pair;
- mixed workloads;
- differing correctness hashes;
- differing source sizes;
- fewer than four samples of either mode by default.

It reports p50/p95/p99/max for available wall-time metrics plus signed PROFILE
ON vs OFF percent deltas. Throughput is derived from the recorded bytes and
time. Rich EE and IOP latency distributions remain sourced from
`parse_hdl_perf.py` for PROFILE ON only.

## Phase-0 acceptance

Phase 0 may be marked hardware-complete only when:

1. PROFILE OFF and PROFILE ON samples share the artifact `project_git_sha`
   recorded by the CI pair actually tested;
2. every sample carries the frozen mode-specific ELF/IRX hashes;
3. both builds pass the same correctness workload;
4. PROFILE ON produces internally consistent stage counters and traffic
   accounting;
5. the measurement overhead of PROFILE ON is quantified rather than assumed
   negligible;
6. PROFILE ON retains p50/p95/p99/max instead of replacing distributions with
   an average;
7. the result includes console/toolchain/IRX/workload provenance;
8. R5900 counter calibration is checked on the real EE before counter-derived
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
