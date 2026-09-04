# Allocation and lifetime audit

This document classifies the major dynamic allocations by producer, consumer,
lifetime and ownership before Phase-5 allocator work. The goal is not to replace
`malloc()` because it exists. The project corpus requires allocation changes to
remove measured churn, copies, fragmentation risk or peak working-set pressure.

## Source-of-truth routing

- `PS2_Memory_Allocators_optimization_research_corpus_v2.md`: classify by
  lifetime; alignment is a consumer contract; avoid per-item churn on hot paths;
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`: producer,
  consumer, lifetime, ownership and representation decide reuse;
- `PS2_PERFORMANCE_BIBLE.md`: remove work/copies/allocations before specialised
  kernels, but only where the workload exposes the cost;
- project CI #712 source audit plus current branch source.

## Epistemic labels

- **POTWIERDZONE**: current source/current API contract.
- **CURRENT IMPLEMENTATION**: behaviour/layout of this branch/toolchain.
- **INFERENCJA**: likely optimization consequence, not yet hardware measured.
- **HIPOTEZA DO TESTU**: change requiring real-PS2 A/B before acceptance.

## Highest-value candidate: one transaction-owned 64 KiB I/O workspace

### Current allocation pattern

The HDL transaction uses `HDL_INSTALL_IO_BYTES = 64 KiB` buffers in three
sequential helpers:

```text
hash_source_payload()
  memalign(64, 64 KiB)
  source SHA reconstruction
  free

copy_payload()
  memalign(64, 64 KiB)
  source read / IOP pump DMA destination / EE SHA consumer
  free

verify_target_digest()
  memalign(64, 64 KiB)
  HDD -> EE DMA destination / target SHA consumer
  free
```

A normal fresh install executes copy then target verification. A resumed
`PAYLOAD_VERIFIED` legacy/fallback path executes source hash then target
verification. The helpers do not own their buffers concurrently.

### Alignment contract

**POTWIERDZONE:** keep 64-byte alignment. `hdl_fast_dma_read()` explicitly rejects
an EE destination whose address is not 64-byte aligned. This is the custom SIF/
cache transport contract, unlike the ordinary fileXio scratch buffers audited in
`ALIGNMENT_CONTRACT_AUDIT.md`.

### Lifetime conclusion

**POTWIERDZONE:** these three buffers have transaction-local, mutually exclusive
lifetimes.

**INFERENCJA:** a single transaction-owned 64 KiB workspace can serve all three
helpers and remove repeated allocator calls without increasing peak payload
memory or changing SIF/HDD/source representation.

Candidate ownership:

```text
execute_transaction owns workspace
  FREE/UNUSED before allocation
  SOURCE_HASH while hash_source_payload consumes it
  COPY_IO while copy_payload consumes it
  TARGET_VERIFY while verify_target_digest consumes it
  released once at transaction exit
```

No helper may retain the pointer after return.

### Proposed post-gate A/B

Baseline: current helper-local allocations.

Experiment:

1. allocate one `memalign(64, HDL_INSTALL_IO_BYTES)` workspace only for stages
   that need source/copy/target hashing;
2. pass pointer + capacity to each helper;
3. remove helper-local alloc/free pairs;
4. preserve all fileXio/SIF/cache/journal/error semantics;
5. free exactly once on transaction exit.

Measure:

```text
allocator calls per transaction
peak EE heap delta
copy/verify p50/p95/p99/max
total transaction p50/p95/p99/max
execute_transaction/static text delta
correctness hash
```

Priority: **HIGH after the frozen resume-hash hardware gate**, because it changes
an active bulk transaction path but does not require a new representation.

## USB ISO catalogue array

Producer: `scan_mass_images()`.

Current representation:

```text
initial capacity: 32 hdl_image_entry_t
allocation: calloc
 growth: doubling below 1024, then +1024 entries
consumer: ISO selection UI + selected path/size handoff
lifetime: one begin_new_install selection session
release: before destructive confirmation / execute_transaction
```

**POTWIERDZONE:** the array is freed after the selected ISO fields have been
copied into the transaction, before the long-running HDD transaction begins.

**INFERENCJA:** this is a sensible variable-cardinality session allocation. Do
not replace it with a giant permanent table without measured directory-size or
allocation-jitter evidence.

Potential improvement only if logs show large catalogues/realloc churn:

- count/size directory entries first only if the second scan is cheaper than
  growth for the real workload;
- or use a bounded chunked/session arena if large catalogues are common.

Priority: **LOW until catalogue cardinality is measured.**

## Installed HDL catalogue array

Producer: raw APA chain walker.

Current representation:

```text
initial capacity: 64 entries on first growth
 growth: capacity * 2
consumer: installed-games menu/details/delete selection
lifetime: one menu session
metadata: loaded lazily by visible page and cached in each entry
release: leaving menu; rebuilt after successful deletion
```

**POTWIERDZONE:** `realloc` occurs only while discovering main HDL partitions;
metadata itself is not separately heap-allocated per game.

**INFERENCJA:** the growable session array is appropriate unless very large HDL
catalogues demonstrate allocator/copy cost. A persistent index does not solve
this automatically because invalidation must still be cheaper than the APA walk.

Priority: **LOW/MEDIUM depending measured game count and catalogue latency.**

## Forensic HDDMETA snapshot

Producer: `build_snapshot_image()`.

Current peak state during save:

```text
image  = malloc(image_size)
verify = malloc(image_size)
write image
read entire file into verify
memcmp(verify, image, image_size)
free both
```

`image_size` scales with `patch_count` because every touched original 1024-byte
APA header is embedded in the safety record.

**POTWIERDZONE:** two equal-size buffers are simultaneously live solely for
read-back verification.

**INFERENCJA:** the second full-size allocation can be removed without weakening
verification by reading the saved file back in a bounded scratch window and
comparing each window to the still-owned canonical `image`. This retains exact
byte-for-byte verification rather than replacing it with an unchecked write.

Alternative: compare a streamed read-back hash against a canonical image hash,
but exact chunk comparison is simpler and preserves the current error contract.

Priority: **MEDIUM for peak-memory robustness, LOW for normal performance**
because forensic repair is an exceptional cold path.

## Bootstrap payload (`MBR.XLF`)

Producer: `load_payload_file()`.

```text
allocation: malloc(file size), bounded by HDD_MAX_MBR_PAYLOAD_SIZE
consumer: KELF/layout validation and subsequent bootstrap write workflow
ownership: bootstrap_source_t.payload
lifetime: prepare -> caller operations -> bootstrap_source_release
```

**POTWIERDZONE:** this is not transient read scratch. The loaded representation
is itself consumed across multiple stages.

**INFERENCJA:** retaining one owned payload buffer is correct. Replacing it with
chunked streaming would complicate KELF/layout consumers and should not be done
without evidence that payload peak memory is a problem.

Priority: **KEEP unless memory measurements disagree.**

## Raw active bootstrap payload read

Producer: `hdd_read_payload_image()`.

```text
allocation: malloc(total selected payload bytes)
producer scratch: fixed HDD_TRANSFER_BYTES temporary
consumer: caller receives payload_out
ownership transfer: function -> caller
```

**POTWIERDZONE:** the heap allocation is the returned dataset, not helper-local
scratch. It cannot be removed without changing the API/consumer representation.

Priority: **KEEP; redesign only with an explicit streaming consumer.**

## Boot-chain text/config files

Current source allocates bounded text buffers for complete small configuration
files and frees them at the end of the corresponding probe/parse operation.

**INFERENCJA:** these are cold startup/configuration allocations. Replacing them
with custom pools is lower value than transaction/storage work unless startup
profiling identifies allocator cost or fragmentation.

Priority: **LOW.**

## Rescue/forensic/general storage allocations

The source audit identifies additional allocations in `rescue_storage.c`,
`forensic_snapshot.c`, `bootstrap_source.c`, `storage.c` and boot tooling. They
are mostly operation/session-owned bounded records rather than per-64-KiB hot
loop allocations.

Rule for subsequent review:

```text
if allocation happens once per user operation:
  measure peak bytes and failure behaviour first
if allocation happens at each bulk phase boundary:
  consider lifetime reuse
if allocation happens inside a chunk/item loop:
  treat as immediate review trigger
```

The current HDL fast 64-KiB loop does **not** allocate per chunk. Its allocation
churn is per phase, which is why one transaction workspace is the appropriate
first allocator experiment rather than an arena rewrite.

## Phase-5 priority order

1. **After hardware gate:** A/B one transaction-owned 64 KiB aligned workspace.
2. Record EE heap before/after transaction and at major phase boundaries if a
   current safe heap query is available without materially perturbing the path.
3. If exceptional recovery memory matters, replace forensic full-size read-back
   duplicate with bounded exact chunk comparison.
4. Measure ISO/game catalogue cardinality before changing their growth strategy.
5. Leave payload-owning allocations intact until a consumer can operate on a
   different representation.
6. Do not introduce custom pools/arenas merely to reduce the count of `malloc`
   strings in source.

## Acceptance record for allocator changes

```yaml
allocation_site:
producer:
consumer:
lifetime:
bytes:
alignment:
ownership_before:
ownership_after:
alloc_calls_before:
alloc_calls_after:
peak_heap_before:
peak_heap_after:
p50:
p95:
p99:
max:
correctness_hash:
error_path_test:
```

Allocator optimization is accepted only if it removes a real cost or reduces a
meaningful peak-memory risk while preserving ownership/error semantics.
