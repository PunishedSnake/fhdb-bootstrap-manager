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

- [ ] Record exact PS2SDK/toolchain/build flags automatically in benchmark logs.
- [ ] Record console SCPH/hardware revision, adapters, active IRX and workload.
- [ ] Add per-stage HDL fast-path timing for source I/O, prefetch wait, HDD,
      SIF DMA and EE consumer work.
- [ ] Record useful bytes, DMA bytes, CPU-copy bytes and cache-maintenance bytes.
- [ ] Add p50/p95/p99/max reporting for I/O latency, not just average throughput.
- [ ] Keep hot-path logging binary/counter based; format only outside the path.
- [ ] Add R5900 performance-counter harness with companion non-instrumented run.
- [ ] Preserve linker map, symbol sizes and optimization audit in CI artifacts.

Exit gate: measurements are reproducible on at least one real console and the
instrumented build has a documented overhead A/B against an uninstrumented build.

## Phase 1: remove known unnecessary code/work

- [ ] Replace the broad `draw2d` dependency used by the UI with the minimum GIF
      primitives actually required, if the ELF A/B confirms removal of unused
      arc/trigonometry/libm code.
- [ ] Audit formatted-I/O callsites and replace hot/control-only formatting with
      bounded lightweight formatting where this materially reduces `.text`.
- [ ] Investigate current PS2SDK fileXio/newlib timestamp glue that pulls scanf/
      timezone machinery into the ELF. Treat any SDK change as a separate,
      source-pinned compatibility patch.
- [ ] Remove source-level work duplicated across transaction stages when the
      result can be safely retained under the same ownership/lifetime.

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

Exit gate: smaller active I-cache footprint plus equal correctness/error paths.

## Phase 3: storage, APA and HDL dataflow

- [ ] Describe APA catalogue, ISO source, HDL transaction and payload stream with
      producer/consumer/lifetime/ownership contracts.
- [ ] Add a persistent compact HDL catalogue index with version, drive identity,
      APA-chain validation and checksum; mismatch always falls back to full scan.
- [ ] Keep large sequential transfers and persistent descriptors; avoid repeated
      small fileXio/RPC control-plane operations.
- [ ] Re-measure USB source, HDD target and verification independently.
- [ ] Tune chunk/batch size only with a sweep on the same device/workload.
- [ ] Audit sync/flush frequency against transaction durability requirements.

Exit gate: lower non-hideable storage time without weakening journal or metadata
commit safety.

## Phase 4: IOP/SIF service architecture

- [ ] Measure queue/service/transport/completion latency separately.
- [ ] Maintain the IOP-local producer path where the final device consumer is on
      the IOP; do not bounce payload through EE without a consumer requirement.
- [ ] Keep control metadata coarse-grained and bulk payload on DMA/data-plane paths.
- [ ] Express double buffering as explicit producer/consumer ownership states.
- [ ] Evaluate triple buffering only if telemetry shows producer/consumer jitter
      that a third slot can actually hide within the IOP RAM budget.
- [ ] Add a static IOP RAM budget including IRX, staging buffers, fragment maps,
      stacks and safety headroom.
- [ ] Sweep IOP worker priorities only after measuring service slack and stalls.

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
