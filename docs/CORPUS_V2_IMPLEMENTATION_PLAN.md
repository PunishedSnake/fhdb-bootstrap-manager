# PS2 Optimization Corpus v2 integration plan

This branch exists to integrate the PS2 Optimization Research Library v2 into the
runtime architecture without destabilising the known-good HDL installer branch.

## Safety boundary

- Branch: `perf/corpus-v2-integration`
- Baseline: `4b5aa8d85e86c9de570a2128b52d1eaa5b334844`
- Parent line: `feature/hdl-game-installer`
- No optimization is merged back until correctness tests, CI and the relevant
  real-hardware A/B benchmark pass.
- PCSX2 remains useful for correctness and inspection, but timing/cache/DMA/
  FIFO/device claims require a real PlayStation 2.

## Source-of-truth routing

Always begin with `PS2_Optimization_Library_v2_MANIFEST.md`, then use
`PS2_PERFORMANCE_BIBLE.md` for the engineering workflow and the authoritative
subsystem corpus for each change. The project uses the following conflict order:

```text
v2
> more specialised current corpus
> current source/manual
> real-hardware reproduction
> integrator corpus
> emulator/reverse engineering
> historical forum/anecdote
```

Epistemic labels used in review notes and benchmark records:

- **POTWIERDZONE**: manual, current source or real-hardware reproduction.
- **CURRENT IMPLEMENTATION**: behaviour of the pinned PS2SDK/toolchain source.
- **HISTORYCZNE**: old toolchains, stacks or community measurements.
- **INFERENCJA**: architectural conclusion not directly measured.
- **HIPOTEZA DO TESTU**: proposed change requiring a benchmark on real hardware.

## Optimization order

Every work item follows this order unless evidence justifies skipping a step:

1. remove unnecessary work;
2. do work less often;
3. reduce data volume;
4. improve data layout and locality;
5. batch;
6. remove unnecessary copies and dynamic allocation;
7. add buffering and overlap;
8. use specialised hardware when the workload fits;
9. only then specialise a measured hot kernel.

Compiler flags, VU/MMI, Scratchpad, larger buffers, 64-byte alignment and custom
low-level APIs are never accepted as universal optimizations.

## Dataset contract

Every major runtime dataset or streaming path should eventually document:

```yaml
name:
producer:
consumers:
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

Alignment must state the actual domain: allocator, EE cache line, DMAC/SIF,
device sector/transfer unit, VIF/GIF packet, or another explicit contract.

## Phase 0: measurement foundation

Goal: establish evidence before altering architecture.

- [x] Record project SHA/ref, pinned PS2SDK source ref/SHA, toolchain identity and
      build flags automatically in benchmark provenance artifacts.
- [ ] Record console SCPH/hardware revision, adapters, runtime active IRX and
      workload during a real-hardware benchmark. Build CI intentionally leaves
      those fields `UNRECORDED` rather than inferring them.
- [x] Add per-stage HDL fast-path timing for direct/fallback source I/O,
      prefetch consumer wait, HDD read/write, SIF DMA and EE consumer work.
- [x] Record useful bytes, SIF DMA bytes, HDD/source sectors, fallback bytes and
      EE cache-maintenance bytes.
- [x] Add p50/p95/p99/max reporting for I/O latency, not just average throughput.
- [x] Keep hot-path logging counter/histogram based; format only at phase exit.
- [x] Add an R5900 performance-counter harness using the EE Core Manual event
      table and dedicated `mfpc/mtpc/mfps/mtps` instructions. The harness
      preserves/restores prior counter state and has passed current-toolchain CI.
- [x] Preserve linker map, symbol sizes and optimization audit in CI artifacts.
- [x] Add a host parser that converts `HDDMAN.LOG` corpus-v2 performance records
      to stable JSON and self-tests in CI.
- [x] Add a same-source profiling-on/profiling-off build pair for authoritative
      instrumentation-overhead A/B on real hardware.
- [x] Isolate IOP objects by profile mode and reject byte-identical ON/OFF IRX
      artifacts in CI.
- [x] Bind each benchmark provenance record to the final stripped ELF and
      embedded `hdl_stream.irx` SHA-256 and byte size.
- [x] Add a host preflight verifier for the frozen hardware pair and emit an
      interleaved eight-run sample template.
- [ ] Exercise the R5900 counter harness in a bounded hardware benchmark and
      measure empty-scope overhead before instrumenting application kernels.

### Phase-0 implementation notes

The first EE profiler pass increased `execute_transaction()` from the audited
6420 B baseline to 7492 B under LTO. This was rejected. Profiling helpers were
then isolated with selective `noinline` and report paths with `cold,noinline`.
The instrumented transaction body later became 6156 B after Phase-1 work while
retaining the counters. Static footprint alone is not a runtime-overhead claim.

The IOP path records logarithmic microsecond latency histograms without `printf`
in the hot path. Current categories are:

```text
usb-direct-read
source-fallback-read
prefetch-consumer-wait
hdd-write
hdd-read
sif-dma-completion
```

EE-side companion categories are:

```text
pump-ioctl
source-ioctl
target-ioctl
copy-ee-consumer
verify-ee-consumer
```

The pinned `ps2dev/ps2dev:v2.0.0` image is tied to the `ps2dev` v2.0.0 build
bundle. Its tagged build passes `v2.0.0` to the PS2SDK build script, which checks
out PS2SDK `v2.0.0`; that annotated tag resolves to commit
`b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b`. CI records that exact source
provenance rather than substituting current PS2SDK master.

The Phase-0 same-source A/B switch is `HDL_PROFILE=1/0`. PROFILE OFF compiles
EE/IOP latency timers, histograms, traffic counters and phase-end telemetry out
while preserving pump/prefetch, SIF DMA, EE cache maintenance, SHA verification,
journal, flush and metadata durability paths.

An early A/B pair through CI #661 was invalid for IOP profiler-overhead work.
PS2SDK's IOP build rules store objects in `obj/`, while the old top-level clean
did not remove that directory. PROFILE OFF therefore reused the PROFILE ON
`hdl_stream.o`, producing a byte-identical embedded IRX. The corrected module
build makes the profile mode part of the object path as `obj/profile-0/` and
`obj/profile-1/`, removes the object tree in the module clean target, and CI now
fails if the two IRX files compare equal.

The frozen hardware pair is CI #666 at project commit
`7875b14d837d6332f5edc37f1c12a55527d7dd87` with PS2SDK
`b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b` and GCC 15.2.0:

```text
                         PROFILE ON   PROFILE OFF   delta OFF vs ON
stripped ELF                638388        632884          -5504 B
EE named text               232780        229956          -2824 B
EE named functions             618           609               -9
EE instructions              58246         57539             -707
execute_transaction()         6156          6156                0
execute_transaction insn      1540          1540                0
hdl_stream.irx file           9861          8405          -1456 B
hdl_stream.irx .text          8595          7139          -1456 B
hdl_stream.irx .data           144           144                0
```

Frozen hashes:

```text
PROFILE ON ELF   964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7
PROFILE OFF ELF  4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916
PROFILE ON IRX   8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db
PROFILE OFF IRX  f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
```

These are static deltas only. The authoritative overhead result remains the
interleaved real-console wall-time comparison defined in
`docs/PHASE0_HARDWARE_AB_PROTOCOL.md`. Before a hardware run,
`tools/phase0_profile_pair_preflight.py` verifies that the selected ELF files
match the frozen pair and can emit the eight-run sample template.

Exit gate: measurements are reproducible on at least one real console and the
instrumented build has a documented overhead A/B against its same-source
PROFILE-OFF companion.

## Phase 1: remove known unnecessary code/work

- [x] Evaluate replacing the broad `draw2d` dependency with minimum GIF
      primitives. The tested shim was rejected because final `.text` grew by
      480 B; the existing dependency remains the smaller measured result.
- [x] Audit formatted-I/O callsites and replace the unused floating formatter
      dependency with the bounded integer-only formatting contract guarded by CI.
- [x] Audit current PS2SDK fileXio/Newlib timestamp and timezone glue. Keep direct
      fileXio application semantics, remove unused local-time bootstrap work, and
      guard the source contract in CI.
- [x] Remove source-level work duplicated across transaction stages where the
      result can safely remain under the same ownership/lifetime. The normal
      metadata-commit path now reuses its canonical 1024-byte block for read-back.

Exit gate: same functional output, smaller ELF/hot code footprint, no regression
in hardware smoke tests.

## Phase 2: I-cache and control-flow locality

- [ ] Split `execute_transaction()` into state/stage handlers without changing
      transaction semantics or recovery guarantees.
- [ ] Split other measured multi-kilobyte controller functions only where the
      active path benefits from smaller working sets.
- [ ] Evaluate `-Os` for cold translation units and retain `-O2` for measured hot
      paths; compare size and latency before adopting per-TU flags.
- [ ] Audit compiler-generated 64-bit divide/mod helpers and eliminate only cases
      whose arithmetic contract proves a cheaper transformation correct.

Current note: the isolated resume-hash experiment grows `execute_transaction()`
to 6556 B in PROFILE OFF and 6572 B in PROFILE ON, so the I-cache concern remains
real. Phase 2 is nevertheless held behind the Phase-0/Phase-1 real-hardware gate
rather than treating static size as a timing result.

Exit gate: smaller active I-cache footprint plus equal correctness/error paths.

## Phase 3: storage, APA and HDL dataflow

- [x] Describe APA catalogue, ISO source, HDL transaction, SHA checkpoint and
      payload stream with producer/consumer/lifetime/ownership contracts in
      `docs/HDL_DATAFLOW_CONTRACTS.md`.
- [x] Audit recovery producer lifetime and isolate the resume-hash checkpoint
      experiment. A matching COPY checkpoint removes prefix replay; a matching
      full `PAYLOAD_VERIFIED` checkpoint also removes source reopen/fingerprint/
      ISO-probe work while retaining full HDD SHA-256 read-back.
- [ ] Evaluate a persistent compact HDL catalogue index only after identifying a
      cheap, trustworthy drive/APA mutation-generation signal. If validation
      requires the same full chain walk, retain the current lazy session cache
      instead of adding a cache that merely moves work around.
- [ ] Keep large sequential transfers and persistent descriptors; avoid repeated
      small fileXio/RPC control-plane operations outside semantically required
      journal/checkpoint boundaries.
- [ ] Re-measure USB source, HDD target and verification independently.
- [ ] Tune chunk/batch size only with a sweep on the same device/workload.
- [x] Audit checkpoint/journal small-file frequency for the current recovery
      experiment: the 256-byte sidecar is written at the existing 32 MiB journal
      boundary plus orderly cancel, not per 64 KiB payload chunk. Broader HDD/
      filesystem flush-frequency changes remain measurement-gated.

### Phase-3 implementation notes

The current experiment identity is CI #706 at project commit
`a43b073c32348e020c234fff64615c8c4cddc98d`. The frozen baseline remains CI
#666. The matched experiment keeps `hdl_stream.irx` byte-identical to its
corresponding frozen PROFILE mode, so the resume-hash/source-lifetime experiment
is EE-only.

`docs/HDL_RESUME_HASH_BENCHMARK.md` is the hardware correctness/performance gate.
Its PROFILE OFF pair is the release-like acceptance pair; PROFILE ON exists for
USB/HDD/SIF/EE attribution. The experiment remains **HIPOTEZA DO TESTU** until
real hardware validates recovery gain, uninterrupted-install regression and
crash-window behaviour.

Exit gate: lower non-hideable storage time without weakening journal or metadata
commit safety.

## Phase 4: IOP/SIF service architecture

- [x] Instrument queue-adjacent prefetch wait, service/device work and SIF
      completion independently enough to locate the dominant 64 KiB stage.
- [x] Maintain the IOP-local producer path where the final device consumer is on
      the IOP. Current fast COPY reads into IOP staging, writes directly to
      ps2hdd, and sends only the EE SHA consumer copy over SIF.
- [ ] Keep control metadata coarse-grained and bulk payload on DMA/data-plane paths.
- [ ] Express double buffering as explicit producer/consumer ownership states in
      code only if the current semaphore/stage protocol needs further evolution;
      the current ownership contract is documented in `HDL_DATAFLOW_CONTRACTS.md`.
- [ ] Evaluate triple buffering only if telemetry shows producer/consumer jitter
      that a third slot can actually hide within the IOP RAM budget.
- [x] Add a static incremental IOP RAM budget for IRX sections, staging buffers,
      fragment map, prefetch stack and compiler-observed stream-object size in
      `docs/HDL_IOP_RAM_BUDGET.md`. System-wide free-memory headroom remains a
      required real-runtime measurement before any buffer growth.
- [ ] Record active IOP module text/data/BSS, owned stacks and minimum free IOP
      memory on real hardware before any triple-buffer/ring expansion.
- [ ] Sweep IOP worker priorities only after measuring service slack and stalls.

Current known `hdl_stream` worst-case incremental subtotal for one active
installer stream is 192314 B in PROFILE OFF and 194458 B in PROFILE ON, excluding
ThreadMan control allocations and the rest of the active IOP runtime. Those
unknowns are deliberately not guessed.

Exit gate: higher overlap/lower p99 with no IOP starvation or device regressions.

## Phase 5: allocators, copies and lifetime

- [ ] Inventory dynamic allocation by lifetime class: permanent, menu/session,
      transaction, streaming, temporary and IOP service.
- [ ] Replace allocation churn only where traces show jitter/fragmentation/copy
      amplification. Candidate structures are arenas, pools and aligned rings.
- [ ] Record `copies_per_payload`, `bytes_touched_per_payload` and DMA bytes for
      storage chunks and large recovery/forensic records.
- [ ] Audit every explicit `aligned(64)` and document the concrete consumer
      contract; remove or change alignment only with evidence.

Exit gate: lower allocation/copy cost and no lifetime/ownership regressions.

## Phase 6: GS/frontend

- [ ] Preserve the real-hardware-proven 640x224 native coordinate contract.
- [ ] Measure GIF packet size, submission count, waits and framebuffer/VRAM use.
- [ ] Remove redundant FINISH/waits only after proving ownership/completion.
- [ ] Keep UI data GS-ready and avoid runtime repacking where a persistent
      representation is simpler.
- [ ] Re-run the existing multi-mode real-hardware video regression after every
      renderer synchronization or VRAM-layout change.

Exit gate: equal visual correctness and mode stability with smaller CPU/packet
cost or smaller code footprint.

## Phase 7: specialised hot kernels

Only after the earlier phases have moved the bottleneck:

- [ ] Benchmark SHA-256 and other remaining hot kernels with R5900 counters.
- [ ] Compare portable C, compiler output and a simple specialised baseline.
- [ ] Evaluate MMI only for regular packed data that actually dominates CPU time.
- [ ] Evaluate Scratchpad only for a bounded explicit working set with proven
      transfer/ownership benefit.
- [ ] Do not introduce VU/VIF/IPU merely because the hardware exists; require a
      fitting regular workload and end-to-end win including transport/sync.

Exit gate: real-hardware A/B win including p50/p95/p99/max and correctness hash.

## Benchmark record

Every accepted optimization benchmark should record at least:

```yaml
console_scp:
hardware_revision:
network_adapter:
storage_adapter:
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
p50:
p95:
p99:
max:
deadline_misses:
```

## Merge policy

Each material optimization lands as a small reviewable commit with:

1. bottleneck and evidence;
2. authoritative corpus/current source;
3. performance hypothesis;
4. smallest meaningful change;
5. correctness/error handling retained;
6. measurement method;
7. alignment/lifetime/thread-context risks;
8. simpler A/B baseline for aggressive changes.

After every major optimization, whole-system profiling is repeated because the
bottleneck is assumed to have moved.
