# Allocation and lifetime audit

This document classifies the major dynamic allocations by producer, consumer,
lifetime, memory domain and ownership before further Phase-5 allocator work. The
goal is not to replace `malloc()` because it exists. The project corpus requires
allocation changes to remove measured churn, copies, fragmentation risk or peak
working-set pressure.

## Source-of-truth routing

- `PS2_Optimization_Library_v2_MANIFEST.md`
- `PS2_PERFORMANCE_BIBLE.md`
- `PS2_Memory_Allocators_optimization_research_corpus_v2.md`
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`
- `PS2_Whole_System_Scheduling_research_corpus_v2.md`
- `PS2_IOP_SIF_optimization_research_corpus_v2.md`
- pinned PS2SDK `b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b`

The corpus rule applied here is: classify producer, consumer, lifetime and the
actual alignment domain before choosing an allocator. A pool/arena is not
implicitly better than the current heap merely because it sounds console-like.

## Epistemic labels

- **POTWIERDZONE**: current source/current API contract.
- **CURRENT IMPLEMENTATION**: behaviour/layout of this branch/toolchain.
- **INFERENCJA**: likely optimization consequence, not yet hardware measured.
- **HIPOTEZA DO TESTU**: change requiring real-PS2 A/B before acceptance.

`tools/allocation_inventory.py` is a static direct-call inventory. It now covers:

```text
EE/libc heap
  malloc
  calloc
  realloc
  memalign
  free

IOP SysMem
  AllocSysMemory
  FreeSysMemory
```

It deliberately does not guess ThreadMan implementation-owned backing memory
from `CreateThread`/`CreateSema` call counts. Those costs remain part of the
runtime IOP-memory gate.

## Lifetime classes

```text
permanent        process lifetime
menu/session     one UI/session/catalogue lifetime
action           one bounded user operation
transaction      one HDL install/recovery transaction
stream           one open streaming service instance
phase-temporary  one scan/hash/copy/verify sub-phase
```

These are review labels derived from current ownership structure, not allocator
latency measurements.

# EE/libc heap

## Boot-chain and generic bounded-file helpers

| Site | Lifetime | Producer / consumer | Current action |
| --- | --- | --- | --- |
| `boot_chain_ps2.c:read_skip_hdd_setting` | phase-temporary | small config reader -> parser | keep |
| `boot_chain_ps2.c:scan_sysconf_partition` | phase-temporary | config reader -> parser | keep |
| `bootstrap_source.c:load_payload_file` | action | file -> bootstrap payload validation/write flow | keep unless payload peak is measured problematic |
| `storage.c:read_bounded_file` | caller-owned action | bounded file -> caller | keep generic helper |
| `hdd_read.c:hdd_read_payload_image` | action | HDD payload -> recovery consumer | returned dataset, not disposable scratch |

These are ordinary CPU/fileXio data. None has a demonstrated need for a custom
allocator today.

## USB ISO selection array

Producer: `scan_mass_images()`.

Current representation:

```text
initial capacity: 32 hdl_image_entry_t
allocation: calloc
growth: doubling below 1024, then +1024 entries
consumer: ISO selection UI + selected path/size handoff
lifetime: one begin_new_install source-selection session
release: before the long-running destructive transaction
```

The resume-hash `.inc` contains the alternate build equivalent; both fragments
are not linked simultaneously.

**POTWIERDZONE:** the array is function/session owned and released after the
selected source identity is copied out.

**INFERENCJA:** this is an appropriate variable-cardinality session allocation.
Do not replace it with a giant permanent table without catalogue cardinality or
allocator-jitter evidence.

Priority: **LOW until large source catalogues are measured.**

## Installed HDL catalogue array

Producer: raw APA chain walker.

Current representation:

```text
first capacity: 64 entries
growth: capacity * 2
consumer: installed-games menu/details/delete selection
lifetime: one catalogue/menu session
metadata: lazy per visible page, stored inside each entry
release: catalog_free()
```

**POTWIERDZONE:** `realloc` occurs only while discovering main HDL partitions;
metadata itself is not separately heap-allocated per game.

**INFERENCJA:** geometric growth already avoids per-entry allocation churn. A
pool or persistent index is not justified until game-count/catalogue latency is
measured and invalidation can be made cheaper than the APA walk.

Priority: **LOW/MEDIUM depending real catalogue size.**

## HDL transaction / streaming workspaces

Default source contains separate 64 KiB helper allocations for:

```text
source_fingerprint()
hash_source_payload()
copy_payload()
verify_target_digest()
```

The three transaction helpers use a 64-byte-aligned EE destination because the
custom `hdl0:` SIF/DMA path explicitly requires that destination contract. That
alignment must not be confused with ordinary fileXio caller alignment.

### CI #724: transaction workspace v1

**POTWIERDZONE:** COPY/source-hash and target-verify workspaces have mutually
exclusive lifetimes.

The isolated v1 experiment gives ownership to `execute_transaction()` and lends
one 64 KiB / 64-byte-aligned workspace sequentially to those helpers. It removes
one general-heap allocation/free pair on the successful bulk path without
increasing peak workspace.

### CI #733: workspace v2, rejected/held

Extending the same workspace backwards into source admission removes another
allocation pair but grows the transaction control path and lengthens workspace
lifetime. One extra allocation per transaction is too small a prize to accept
that trade without real hardware evidence.

### CI #739: source fingerprint `memalign` -> `malloc`

Pinned `fileXioRead()` accepts ordinary caller alignment, so the standalone
source-fingerprint buffer has no demonstrated 64-byte API requirement.

Static result versus workspace v1 is slightly smaller and does not grow
`execute_transaction()`. This remains **HIPOTEZA DO TESTU** because fileXio's
unaligned-edge handling can cost time even when the API contract permits it.

The custom SIF/DMA transaction workspace remains 64-byte aligned.

## Forensic HDDMETA snapshot

Producer: `build_snapshot_image()`.

Baseline save keeps two variable-size allocations live:

```text
canonical image = 64 + patch_count * (4 + 32 + 1024) + 32
verify buffer   = canonical image size
```

At the current maximum `patch_count = 2048`:

```text
canonical image              2,170,976 B
baseline full verify         2,170,976 B
baseline pair peak           4,341,952 B
```

### CI #749: bounded read-back v1

The first isolated bounded experiment keeps the canonical image but replaces the
second full-size allocation with at most 64 KiB and compares every returned
chunk byte-for-byte. It preserves format, slot selection, non-overwrite policy,
truncation detection and full read-back verification.

```text
canonical image              2,170,976 B
bounded verify                  65,536 B
experiment pair peak         2,236,512 B
peak reduction               2,105,440 B
```

### CI #752: bounded read-back v2

V1 used two `fileXioLseek()` RPCs inside each exact-compare call to prove file
size. V2 instead:

1. reads exactly the expected bytes with short-read handling;
2. compares every chunk exactly;
3. performs one final one-byte read which must return EOF.

This still rejects truncation and trailing data while removing both seek RPCs.
Compared with CI #749, CI #752 reduces `.text` by 32 B, named text by 36 B and
eight R5900 instructions in both PROFILE modes. `execute_transaction()` and the
IOP binary remain unchanged.

**POTWIERDZONE:** maximum pair peak is reduced by 2,105,440 B relative to the
original full-image verify representation.

**HIPOTEZA DO TESTU:** real PS2 must still verify recovery correctness and
fileXio latency. This is primarily a peak-working-set optimization, not a hot
transaction speedup claim.

The next architectural candidate is eliminating the full canonical image with a
streaming APAMETA1 serializer. That would alter producer lifetime and write
structure and therefore requires an isolated experiment plus stronger reference
serialization tests before hardware use.

# IOP SysMem

The custom `hdl_stream` service has three direct SysMem ownership classes.

## Stream object

Current source:

```text
AllocSysMemory(ALLOC_FIRST, sizeof(*stream), NULL)
```

Lifetime: one open `hdl0:` stream.

Owner: `stream_open()` -> `stream_close()`.

The object stores partition geometry, source-map state, staging ownership,
prefetch handles and optional PROFILE counters. Pooling a single stream object
has no demonstrated benefit.

## Staging allocation

Preferred admission:

```text
AllocSysMemory(ALLOC_FIRST,
               2 * HDL_STREAM_IOP_STAGE_BYTES + 63,
               NULL)
```

Low-memory fallback:

```text
AllocSysMemory(ALLOC_FIRST,
               HDL_STREAM_IOP_STAGE_BYTES + 63,
               NULL)
```

The two calls are mutually exclusive outcomes. The allocation is manually
rounded to a 64-byte stage address and remains owned for the stream lifetime.

**POTWIERDZONE:** failure to obtain double buffering falls back to one stage; it
does not fail stream admission if one stage still fits.

This is an explicit resilience/ownership policy. Do not replace it with a larger
ring until real IOP memory and stall telemetry justify more buffering.

## Direct-BDM USB fragment map

Current source allocates:

```text
fragment_count * sizeof(bd_fragment_t)
```

with `AllocSysMemory`, up to the current 4096-fragment hard limit.

Lifetime: one usable direct-BDM source mapping. It is released when source
mapping is reset/disabled or when the stream closes.

The static IOP budget records the current worst-case map as 49,152 B.

This is producer representation state, not generic scratch. Any compaction must
preserve fragmented-file correctness and the sequential cursor behaviour that
avoids rescanning thousands of fragments for every 64 KiB block.

## IOP memory outside the direct SysMem inventory

ThreadMan owns backing memory for resources including:

```text
prefetch worker thread
request semaphore
done semaphore
stopped semaphore
```

The worker requests a 4096-byte stack, but ThreadMan control-object overhead is
not inferred here. `docs/HDL_IOP_RAM_BUDGET.md` correctly leaves that overhead
unmeasured until real hardware records active modules and minimum free IOP RAM.

# Phase-5 priority order

1. Keep **CI #752 bounded HDDMETA v2** as the current peak-memory candidate.
2. Hardware A/B transaction workspace v1 and source-fingerprint allocator policy.
3. Build a reference serializer test before attempting streaming canonical
   HDDMETA generation.
4. Measure ISO/game catalogue cardinality before changing growth strategy.
5. Leave payload-owning allocations intact until their consumers can use a
   different representation.
6. Do not introduce global pools/arenas or global 64-byte heap alignment merely
   to reduce allocator call counts in source.
7. Do not expand IOP staging before runtime free-memory and latency attribution
   are available.

## Acceptance record for allocator/lifetime changes

```yaml
allocation_site:
memory_domain:
producer:
consumer:
lifetime_before:
lifetime_after:
bytes_requested:
alignment_before:
alignment_after:
ownership_before:
ownership_after:
alloc_calls_before:
alloc_calls_after:
peak_bytes_before:
peak_bytes_after:
elf_text_delta:
correctness_hash:
console_scp:
hardware_revision:
active_irx:
sample_count:
p50:
p95:
p99:
max:
```

Static source inventory can prove call structure, ownership and requested-size
bounds. It cannot prove fragmentation, allocator latency or whole-system speedup.
