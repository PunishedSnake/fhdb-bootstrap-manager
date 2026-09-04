# HDL IOP RAM budget

This document records the memory budget of the custom `hdl_stream` service
before any further buffering or SIF overlap experiment.

The goal is not to pretend that the IOP has a free 2 MiB heap. The PS2 IOP RAM
also contains the kernel/runtime, every active IRX, stacks, heaps, filesystem and
device state. This document therefore separates the custom service's known
incremental footprint from system-wide memory that still requires real runtime
inventory.

## Source-of-truth routing

- `PS2_IOP_SIF_optimization_research_corpus_v2.md`: IOP RAM/service/thread/SIF
  architecture and the requirement to budget text/data/BSS, stacks, heap
  allocations and persistent buffers;
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`: buffering as
  explicit producer/consumer ownership rather than an arbitrary count of slots;
- `PS2_Whole_System_Scheduling_research_corpus_v2.md`: new buffering is accepted
  only if it hides measured exposed latency without creating another resource
  bottleneck;
- pinned PS2SDK `b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b`: current API/data contracts;
- project CI #706: final `hdl_stream.irx` loaded section sizes and compiler output
  for the matched PROFILE pair.

## Epistemic labels

**POTWIERDZONE**

- retail-class IOP RAM budget is about 2 MiB as routed by the project corpus;
- one `hdl_stream` open attempts a two-stage allocation first and falls back to
  one stage if that allocation fails;
- each stage is 64 KiB;
- the two-stage allocation requests `2 * 64 KiB + 63` bytes and manually aligns
  the first stage to 64 bytes;
- direct BDM source mapping permits at most 4096 fragments;
- pinned PS2SDK defines packed `bd_fragment_t` as one `u64 sector` plus one
  `u32 count`, therefore 12 bytes per fragment;
- the dedicated prefetch worker stack is `0x1000` = 4096 bytes;
- the prefetch worker is created only when two stages are available;
- CI #706 reports final loaded `hdl_stream.irx` sections of 7139 B text + 144 B
  data for PROFILE OFF and 8595 B text + 144 B data for PROFILE ON, with zero
  reported BSS in those final IRX images;
- CI #706 disassembly shows the compiler-generated `AllocSysMemory` size for
  `hdl_stream_file_t` is `0x288` = 648 B in PROFILE OFF and `0x538` = 1336 B in
  PROFILE ON.

**CURRENT IMPLEMENTATION**

- application-side fast-I/O state tracks one active HDL target descriptor;
- the custom IOP driver itself is not a hard single-open API, so the numbers
  below describe the current installer workload, not an enforcement guarantee
  against another future caller opening multiple streams;
- the direct fragment map is allocated lazily and freed when source mapping is
  reset or the stream closes;
- PROFILE ON stores IOP latency/traffic statistics inside the stream object;
- semaphores/thread-control objects are allocated by ThreadMan and are not
  counted as exact bytes here because the project has not measured their runtime
  allocator cost on the pinned IOP image.

**INFERENCJA**

- adding a third 64 KiB stage before telemetry demonstrates a producer/consumer
  gap would consume a material fraction of the service's existing incremental
  budget for an unproven benefit;
- the relevant safety margin is system-wide free IOP memory after all active IRX,
  stacks and driver buffers, not simply `2 MiB - hdl_stream`.

**HIPOTEZA DO TESTU**

- two stages are sufficient for the current USB/HDD/SIF pipeline on real
  hardware;
- a third stage is useful only if PROFILE ON shows repeatable prefetch misses or
  exposed producer jitter that the extra ownership slot can hide.

## Known allocation budget

### Final IRX image

CI #706:

```text
PROFILE OFF loaded sections
  .text   7139 B
  .data    144 B
  .bss       0 B
  total    7283 B

PROFILE ON loaded sections
  .text   8595 B
  .data    144 B
  .bss       0 B
  total    8739 B
```

The on-disk IRX file sizes are larger because they also contain module/ELF
structure. For RAM budgeting use loaded sections, not the archive/file size.

### Streaming stages

```text
one-stage allocation
  65536 + 63 = 65599 B

two-stage allocation
  131072 + 63 = 131135 B
```

The 63-byte slop exists solely to obtain the documented 64-byte-aligned stage
address. It is allocator/alignment overhead, not a third payload buffer.

### Direct-BDM fragment map

Pinned PS2SDK contract:

```text
sizeof(bd_fragment_t) = 8 + 4 = 12 B (packed)
maximum fragments     = 4096
maximum fragment map  = 49152 B
```

This is a worst-case project cap. Ordinary contiguous or lightly fragmented ISO
files consume less.

### Prefetch worker stack

```text
HDL_STREAM_PREFETCH_STACK = 0x1000 = 4096 B
```

This stack exists only on the two-stage path because one-stage fallback does not
create the prefetch worker.

### Stream object and ThreadMan bookkeeping

The final CI #706 machine code exposes the exact size passed to IOP
`AllocSysMemory` for the stream object:

```text
PROFILE OFF sizeof(hdl_stream_file_t) = 0x288 = 648 B
PROFILE ON  sizeof(hdl_stream_file_t) = 0x538 = 1336 B
```

The PROFILE ON increase is expected because latency histograms and traffic
counters live inside the stream object.

Thread and semaphore kernel-control allocations are still **UNMEASURED** and
must be added to the runtime inventory before claiming exact system free RAM.

## Current incremental worst-case service envelope

For one current installer stream, direct BDM available, two stages allocated:

```text
                                  PROFILE OFF    PROFILE ON
IRX loaded sections                    7283 B         8739 B
two-stage allocation                 131135 B       131135 B
max fragment map                      49152 B        49152 B
prefetch stack                         4096 B         4096 B
stream object                           648 B         1336 B
                                  ----------     ----------
known subtotal                       192314 B       194458 B
ThreadMan control objects            UNMEASURED     UNMEASURED
other active IRX/runtime             NOT INCLUDED   NOT INCLUDED
```

The known subtotal is roughly 188-190 KiB. It is **not** a claim that only this
amount of IOP RAM is consumed by the whole application stack.

For one-stage low-memory fallback, remove one 64 KiB stage and the dedicated
prefetch stack. Correctness remains available while overlap is reduced.

## Buffer expansion gate

Do not add triple buffering merely because 64 KiB appears small next to 2 MiB.
A third stage costs another 65536 B before any ownership metadata and can also
increase live working-set pressure in a memory already shared by USB, FATFS,
ps2hdd, DEV9 and other services.

A third stage may be prototyped only if all of the following are true:

1. PROFILE ON real-hardware data shows a repeatable exposed producer/consumer
   stall, such as prefetch misses or wait tail latency that two slots cannot
   hide;
2. the active IOP module/stack/heap inventory has been recorded for that test;
3. the experiment preserves a documented free-memory safety margin after the
   extra stage is allocated;
4. the new slot has explicit ownership states and cannot be overwritten while
   USB, HDD, SIF or EE consumption still owns it;
5. p50/p95/p99/max improve without device/audio/service regressions;
6. one-stage and two-stage fallback semantics remain correct.

The numeric safety margin is intentionally not invented here. It must be based
on measured free IOP memory and peak transient demand on the actual active module
set. Until that measurement exists, the current two-stage design remains the
maximum accepted service allocation.

## Required runtime inventory before Phase-4 buffer growth

Record at least:

```yaml
console_scp:
hardware_revision:
active_iop_modules:
  - name:
    text_bytes:
    data_bytes:
    bss_bytes:
owned_thread_stacks:
hdl_stream_profile_mode:
hdl_stream_stage_count:
hdl_stream_fragment_count:
free_iop_memory_before_stream:
free_iop_memory_after_stream_open:
free_iop_memory_at_peak_copy:
minimum_free_iop_memory_observed:
workload:
sample_count:
correctness_hash:
```

The project corpus specifically requires active module text/data/BSS and stack
sizes to be treated as performance data. This runtime inventory is therefore a
gate, not optional documentation polish.
