# HDL transaction workspace v2 experiment rejection

This record preserves the second Phase-5 allocator/lifetime experiment so it is
not rediscovered later as an apparently new optimization.

## Source-of-truth routing

- `PS2_Optimization_Library_v2_MANIFEST.md`
- `PS2_PERFORMANCE_BIBLE.md`
- `PS2_Memory_Allocators_optimization_research_corpus_v2.md`
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`
- `docs/HDL_TRANSACTION_WORKSPACE_BENCHMARK.md`

## Hypothesis

Workspace v1, frozen at CI #724, changes the mutually-exclusive bulk helpers:

```text
copy_payload / hash_source_payload -> verify_target_digest
```

from two separate 64 KiB `memalign(64)` lifetimes to one transaction-owned
workspace.

V2 tested extending that ownership backwards through `execute_transaction()`
source admission so the transaction's `source_fingerprint()` also borrowed the
same 64 KiB buffer. The pre-confirmation UI fingerprint deliberately retained
its short helper-owned allocation.

Expected transaction allocation policy:

```text
baseline transaction
  source fingerprint alloc/free
  copy or source-hash alloc/free
  HDD verify alloc/free

v1
  source fingerprint alloc/free
  transaction workspace alloc
    copy or source-hash
    HDD verify
  transaction workspace free

v2
  transaction workspace alloc
    source fingerprint
    partition/open path
    copy or source-hash
    HDD verify
  transaction workspace free
```

## Epistemic status

**POTWIERDZONE**

- current pinned fileXio source does not require the source-fingerprint caller
  buffer to be 64-byte aligned;
- v2 preserves the existing 64-byte transaction workspace alignment required by
  the custom EE/SIF fast path later in COPY/verify;
- v2 does not change the IOP source or transport;
- CI #733 completed host tests, EE/IOP builds, frozen identity checks,
  resume-hash build and artifact upload successfully;
- both v2 experiment IRX files are byte-identical to the corresponding frozen
  Phase-0 IRX files.

**CURRENT IMPLEMENTATION / CI #733**

Source point:

```text
98988fcdca78dd392f95aa40d5b61157ac0bea27
```

Artifact digest:

```text
sha256:8b69850212fe9928d16efb3d4d7610a3b3c2576e4917ff25cd306380ebbcdbe8
```

PROFILE OFF v2:

```text
ELF bytes       632756
ELF sha256      b8865fe6a95a3d0e7d54fb0b72519a68f080eb234f6b6167ad55467c3faed86f
named text      229804 B
instructions     57500
execute_transaction 6032 B / 1508 instructions
IRX sha256      f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
```

PROFILE ON v2:

```text
ELF bytes       638260
ELF sha256      c6865cace130952befa06c2c06fa43485973f08ca14847c322578c5847f61d66
named text      232600 B
instructions     58201
execute_transaction 6032 B / 1508 instructions
IRX sha256      8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db
```

## V2 versus v1 #724

```text
                                      v1 #724       v2 #733       v2 delta
PROFILE OFF ELF                       632756        632756             0 B
PROFILE OFF named text                229764        229804           +40 B
PROFILE OFF instructions               57491         57500            +9
PROFILE ON ELF                        638260        638260             0 B
PROFILE ON named text                 232560        232600           +40 B
PROFILE ON instructions                58190         58201           +11
execute_transaction                     6008          6032           +24 B
execute_transaction instructions        1502          1508            +6
removed alloc/free pairs vs baseline       1             2            +1
```

## Decision

**REJECTED AS THE ACTIVE STATIC OPTIMIZATION.**

The additional source-admission reuse removes one more general-heap alloc/free
pair per transaction, but that pair is not inside the 64 KiB chunk loop. V2 also:

- lengthens the 64 KiB workspace lifetime across source admission and partition
  creation/opening;
- increases `execute_transaction()` by 24 bytes / 6 instructions compared with
  v1;
- increases whole EE named text by 40 bytes;
- gives no final stripped-ELF size reduction beyond v1.

The corpus prioritizes shortest correct lifetime and measured cost rather than
minimum textual allocator-call count. With no real-hardware evidence that one
additional per-transaction `memalign/free` is material, v1 has the better static
tradeoff.

V2 remains **HIPOTEZA DO TESTU** only if future allocator profiling shows the
source-admission allocation itself contributes measurable latency/jitter or heap
fragmentation. Until then CI should materialize v1 for the active workspace A/B.

This is not evidence that v2 is slower on hardware. It is evidence that the
available static data do not justify paying its longer lifetime and larger hot
controller solely to remove one unmeasured allocation pair.
