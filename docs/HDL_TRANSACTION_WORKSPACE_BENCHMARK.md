# HDL transaction workspace hardware benchmark

This document defines the real-PS2 acceptance gate for the isolated Phase-5
transaction-workspace experiment.

The experiment does not change the default runtime source. CI materializes a
temporary `transaction.inc` in which one 64 KiB / 64-byte-aligned EE buffer is
owned by `execute_transaction()` and borrowed sequentially by source-hash,
copy and HDD-verification helpers.

## Source-of-truth routing

- `PS2_Optimization_Library_v2_MANIFEST.md`
- `PS2_PERFORMANCE_BIBLE.md`
- `PS2_Memory_Allocators_optimization_research_corpus_v2.md`
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`
- `PS2_Whole_System_Scheduling_research_corpus_v2.md`
- `PS2_IOP_SIF_optimization_research_corpus_v2.md`
- pinned PS2SDK `b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b`

The relevant corpus rule is lifetime ownership, not a blanket preference for a
custom allocator. The 64-byte alignment remains because the current HDL fast
path explicitly requires it for the EE destination used by SIF DMA.

## Epistemic status

**POTWIERDZONE**

- the frozen baseline allocates one 64 KiB aligned helper buffer for
  `copy_payload()` and then a second, non-overlapping 64 KiB aligned helper
  buffer for `verify_target_digest()` on a successful fresh COPY path;
- a resumed `PAYLOAD_VERIFIED` legacy/fallback path similarly allocates one
  buffer for `hash_source_payload()` and a later one for target verification;
- these helper buffers are not live concurrently;
- `hdl_fast_dma_read()` requires the EE destination to be 64-byte aligned;
- CI #724 materializes one transaction-owned aligned buffer and passes it to the
  three helpers without changing the IOP source, pump, SIF DMA, cache,
  journal, flush, metadata or durability paths;
- CI #724 proves the experiment IRX is byte-identical to the corresponding
  frozen PROFILE IRX.

**CURRENT IMPLEMENTATION**

- workspace bytes: 65536;
- workspace alignment: 64;
- owner: `execute_transaction()`;
- borrowers: `copy_payload()`, `hash_source_payload()`,
  `verify_target_digest()`;
- successful fresh COPY+verify changes two phase-local `memalign/free` pairs to
  one transaction-owned pair;
- successful resumed stage-4 hash+verify changes two phase-local pairs to one;
- allocation failure remains `HDL_INSTALL_MEMORY_FAILED`;
- all helper error returns still converge on transaction cleanup;
- peak payload workspace size remains one 64 KiB buffer in both baseline and
  experiment.

**INFERENCJA**

- removing one general-heap allocation/free pair per successful bulk phase pair
  should reduce allocator churn and may reduce small latency/jitter at the
  copy->verify boundary;
- because the allocation count is per phase rather than per 64 KiB chunk, the
  wall-time effect may be below storage noise;
- the static code reduction is valuable evidence that ownership centralization
  did not trade allocator churn for I-cache growth, but it is not a runtime
  speedup measurement.

**HIPOTEZA DO TESTU**

- real-PS2 transaction latency or tail jitter improves measurably, or at minimum
  does not regress while correctness remains identical;
- retaining the workspace until transaction cleanup does not create a harmful
  EE heap-pressure interaction in the tested transaction path.

## Frozen baseline and CI #724 experiment identity

Frozen Phase-0 source point:

```text
7875b14d837d6332f5edc37f1c12a55527d7dd87
```

CI #724 experiment/materializer source point:

```text
5c2b4d34a7cee05f330be0f6de2d44552d46a136
```

PS2SDK:

```text
b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b
```

Toolchain:

```text
mips64r5900el-ps2-elf GCC 15.2.0
ps2dev/ps2dev:v2.0.0
```

### PROFILE OFF, release-like acceptance pair

Baseline:

```text
PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF
bytes   632884
sha256  4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916
```

Experiment:

```text
PS2_HDD_BOOTSTRAP_MANAGER_TX_WORKSPACE_PROFILE_OFF.ELF
bytes   632756
sha256  23bbf6dfc28eb87bc7d484875a8940b9309eb5e3994d9c922388c9a0249415c6
```

Both use:

```text
hdl_stream.irx
bytes   8405
sha256  f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
```

Static delta:

```text
stripped ELF                 -128 B
.text                        -192 B  (230440 -> 230248)
EE named text                -192 B  (229956 -> 229764)
EE named functions              0    (609 -> 609)
EE instructions               -48    (57539 -> 57491)
execute_transaction()        -148 B  (6156 -> 6008)
execute_transaction insn      -38    (1540 -> 1502)
```

### PROFILE ON, attribution pair

Baseline:

```text
PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_ON.ELF
bytes   638388
sha256  964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7
```

Experiment:

```text
PS2_HDD_BOOTSTRAP_MANAGER_TX_WORKSPACE_PROFILE_ON.ELF
bytes   638260
sha256  09185cd6a21bbb9990d0b7f8cfe70fa80b4e9ba01a00e9648cb9fa70d9b3d693
```

Both use:

```text
hdl_stream.irx
bytes   9861
sha256  8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db
```

Static delta:

```text
stripped ELF                 -128 B
.text                        -224 B  (233280 -> 233056)
EE named text                -220 B  (232780 -> 232560)
EE named functions              0    (618 -> 618)
EE instructions               -56    (58246 -> 58190)
execute_transaction()        -148 B  (6156 -> 6008)
execute_transaction insn      -38    (1540 -> 1502)
```

The PROFILE OFF/ON difference outside `execute_transaction()` is compiler/LTO
layout around profiler-enabled code. The transaction function itself has the
same static reduction in both variants.

CI #724 artifact digest:

```text
sha256:cddf67a31e55e50e23bd6f98ce30739c3a93fff37e3a87d8096c8f9c77416c17
```

Exact binary identity is mandatory. Do not rebuild later and call the result
this A/B pair merely because the source looks equivalent.

## Correctness gate

Before timing, both baseline and experiment must complete the same functional
matrix:

1. fresh install from zero progress;
2. ordinary guarded cancel during COPYING;
3. resume from COPYING;
4. resume from persisted PAYLOAD_VERIFIED;
5. mandatory HDD SHA-256 read-back succeeds;
6. metadata commit + read-back succeeds;
7. final game metadata/startup/title are identical;
8. transaction journal reaches COMPLETE/removal exactly as baseline;
9. memory-allocation failure injection, where practical in host/test scaffolding,
   still maps to `HDL_INSTALL_MEMORY_FAILED` without leaking a target/source FD.

Any correctness mismatch rejects the experiment before performance data is
considered.

## Hardware workload

Use the PROFILE OFF pair for acceptance and PROFILE ON only for attribution.
Keep console, HDD, adapter, USB source, ISO, video mode and launch method fixed.

Use at least eight interleaved successful fresh transactions with an equivalent
target state restored between runs:

```text
BASE, EXP, EXP, BASE, EXP, BASE, BASE, EXP
```

The CI artifact contains:

```text
TRANSACTION_WORKSPACE_AB_IDENTITY.json
TRANSACTION_WORKSPACE_AB_PROFILE_OFF_TEMPLATE.json
TRANSACTION_WORKSPACE_AB_PROFILE_ON_TEMPLATE.json
```

Record:

```yaml
console_scp:
hardware_revision:
romver:
storage_adapter:
hdd_model:
usb_device:
active_irx:
source_iso_sha256:
source_iso_bytes:
profile_mode:
baseline_elf_sha256:
experiment_elf_sha256:
hdl_stream_irx_sha256:
sample_count:
correctness_hash:
```

For each run record at least:

```text
copy elapsed us
verify elapsed us
total transaction elapsed us
result
correctness hash
```

Report p50, p95, p99 and max. Eight runs are a smoke gate; if the delta is near
noise, collect more samples instead of manufacturing certainty from two decimal
places.

## Acceptance rule

Promote the ownership change only if all are true:

1. zero correctness regressions in the functional matrix;
2. exact frozen baseline and experiment identities match this document;
3. experiment IRX remains byte-identical to baseline for each PROFILE mode;
4. peak payload workspace does not exceed the baseline one-buffer 64 KiB peak;
5. real hardware shows no meaningful p95/p99/max regression;
6. any claimed latency/jitter improvement is repeatable rather than a single-run
   difference;
7. no new allocation failure/error-path leak appears;
8. after promotion, whole-system profiling is repeated because the bottleneck
   may move.

If timing is indistinguishable but correctness is equal and the smaller code /
clearer ownership are considered sufficient maintenance benefits, that may
justify a code-quality promotion. In that case document it explicitly as a
static/lifetime improvement, not as a measured performance speedup.
