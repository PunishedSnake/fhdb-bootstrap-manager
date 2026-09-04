# HDL dataflow and ownership contracts

This document records the current producer/consumer/lifetime contract for the
HDL installer and catalogue paths before further storage/SIF optimization.
It follows the PS2 Optimization Research Library v2 rule that representation,
transport and ownership must be explicit before buffering, caching or reuse is
introduced.

The contracts describe the current branch, not an idealized redesign. Anything
not proven by current source or the project corpus is labelled as inference or a
hardware hypothesis.

## Epistemic labels

- **POTWIERDZONE**: current source/manual/corpus contract or real-hardware fact.
- **CURRENT IMPLEMENTATION**: behavior of this branch and the pinned PS2SDK
  stack.
- **INFERENCJA**: engineering conclusion not yet measured.
- **HIPOTEZA DO TESTU**: change/result that requires real-PS2 measurement.

## Dataset: USB ISO source

```yaml
name: usb_iso_source
producer: mass:/ filesystem backed by USB mass-storage / BDM
consumers:
  - ISO9660 + SYSTEM.CNF probe
  - source fingerprint
  - payload copy
  - legacy resume-prefix/full-source SHA reconstruction
lifetime: selected install or resumed transaction while source data is required
representation: raw ISO bytes
alignment:
  file: no global alignment contract
  fast_transfer: 64-byte aligned EE destination, 512-byte transfer multiple
transport:
  fallback: fileXio read through mass: filesystem
  fast_path: IOP BDM fragment map -> IOP stage buffer -> HDD + SIF DMA to EE
batch_size:
  payload: 64 KiB
  fingerprint: first 64 KiB + last 64 KiB plus encoded source size
  journal_progress: 16384 ISO sectors = 32 MiB
ownership_states:
  - filesystem/device owns source media
  - prefetch worker owns alternate IOP stage while read is active
  - main IOP path owns completed stage while writing HDD / submitting SIF
  - EE owns destination after synchronous ioctl/SIF completion
validation:
  - expected file size
  - source fingerprint
  - ISO probe/startup identity
  - full SHA-256 accumulated during fresh copy or rebuilt on legacy resume
```

**POTWIERDZONE:** fresh copy already hashes bytes while they are transported, so
it does not perform a second full USB pass merely to produce the source digest.
The resume-hash experiment removes the remaining replay where lifetime permits.

**CURRENT IMPLEMENTATION:** a valid complete stage-4 checkpoint is being tested
as sufficient producer state for resumed HDD verification. In that case the
current source ISO is no longer a consumer dependency; invalid/missing state
falls back to opening and validating the ISO exactly as before.

## Dataset: transaction journal

```yaml
name: hdl_transaction_journal
producer: EE transaction state machine
consumers:
  - incomplete transaction UI
  - resume path
  - incomplete-target cleanup guard
lifetime: from PLANNED until COMPLETE/removal
representation: fixed 512-byte versioned/checksummed transaction record
alignment: ordinary filesystem record; no DMA alignment contract
transport: mass:/ small-file write/read/rename
batch_size: one record at semantic stage transitions and every 32 MiB of copy
ownership_states:
  - in-memory transaction is mutable only by current EE transaction
  - HDLINSTALL.NEW is replacement candidate
  - HDLINSTALL.TXN is authoritative persisted record selected by loader
validation:
  - encode/decode contract
  - read-back byte equality
  - checksum + stage/progress invariants
```

The journal is transaction authority. Performance hints must never advance its
progress or replace its recovery semantics.

## Dataset: optional SHA-state sidecar

```yaml
name: hdl_source_sha_checkpoint
producer: SHA-256 state accumulated while source bytes are already moving
consumers:
  - resumed COPYING prefix reconstruction
  - resumed PAYLOAD_VERIFIED source-digest reconstruction
lifetime: same transaction only; deleted at zero-progress replacement/COMPLETE
representation: 256-byte version-1 record + SHA-256 over record payload
alignment: ordinary filesystem record; no DMA alignment contract
transport: mass:/ temp write -> readback/codec verify -> rename
batch_size: same existing 32 MiB transaction checkpoint boundary + orderly cancel
ownership_states:
  - in-memory SHA context belongs to current EE copy/hash operation
  - HDLINSTALL.SHN is candidate replacement
  - HDLINSTALL.SHA is preferred hint
  - journal remains authority even when sidecar is newer
validation:
  - source byte count
  - exact completed byte count
  - source fingerprint
  - target ID
  - SHA context total/block state
  - record SHA-256
fallback: any failure -> legacy source rehash
```

**INFERENCJA:** because restore validates exact journal progress, a sidecar that
becomes visible ahead of its matching journal should be rejected against the
older journal rather than advance progress.

**HIPOTEZA DO TESTU:** physical persistence across reset/power loss depends on
the actual USB/FAT/device stack. The source contract proves fail-safe matching;
it does not by itself prove media-level durability.

## Dataset: APA/HDL catalogue

```yaml
name: hdl_catalogue_snapshot
producer: raw APA chain walker
consumers:
  - installed-games browser
  - game details
  - guarded delete selection
  - planning free-space/target-collision checks in scan-only mode
lifetime: one installed-games menu session; rebuilt after successful deletion
representation:
  catalogue: growable EE array of hdl_game_entry_t
  metadata: lazy 1024-byte HDL metadata read, parsed into entry + SHA-256
alignment:
  raw APA header: 64-byte aligned EE buffer for raw HDD transfer/cache contract
  metadata: 64-byte aligned local buffer on raw read paths
transport: raw HDD sector reads + selected fileXio control queries
batch_size:
  APA: one 1024-byte header (2 HDD sectors) per chain node
  game_metadata: one 1024-byte block per game, lazy by visible browser page
ownership_states:
  - catalogue array belongs to installed-games menu invocation
  - each metadata_state transitions from NOT_LOADED to one cached result
  - delete path treats snapshot only as selection evidence and revalidates live HDD
validation:
  - APA magic/checksum/start/prev/next/bounds/type/sub-count
  - total/used/free sector accounting
  - HDL metadata parse + metadata SHA-256
  - destructive delete performs live target/journal/snapshot checks again
```

**POTWIERDZONE:** metadata is already loaded lazily by visible page and cached in
the session entry. A persistent catalogue index therefore cannot be justified by
"avoiding all metadata reads"; the current path does not read every game metadata
block up front.

**INFERENCJA:** a persistent index is useful only if its validity can be checked
substantially cheaper than the raw APA work it replaces. If proving that no
external APA mutation occurred requires walking the whole chain anyway, the
index may simply move work around. Any persistent-index implementation must
first identify a trustworthy drive/APA generation signal or define a clearly
bounded weaker cache contract with full-scan fallback.

## Dataset: HDL payload stream

```yaml
name: hdl_payload_stream
producer: USB source or HDD target depending operation
consumers:
  copy:
    - ps2hdd target write on IOP
    - EE SHA-256 consumer
  verify:
    - EE SHA-256 consumer of HDD read-back
lifetime: one open hdl0: stream descriptor / transaction phase
representation: sequential 64 KiB payload chunks
alignment:
  IOP_stage_allocator: AllocSysMemory allocation plus manual 64-byte alignment
  EE_DMA_destination: required 64-byte aligned
  transfer_size: required multiple of 512 B; DMA helper requires 64 B multiple
transport:
  source_fast: BDM read into IOP stage
  target_write: IOP stage -> ps2hdd
  EE_copy: SIF DMA once per chunk
  fallback: stock fileXio path
batch_size: 64 KiB
ownership_states:
  - stage[0]/stage[1] are IOP-owned buffers
  - prefetch worker owns requested alternate stage until done semaphore
  - main IOP path owns current completed stage during HDD/SIF consumption
  - SIF DMA must complete before current ioctl returns
  - EE destination becomes usable after ioctl return + D-cache invalidation
validation:
  - layout query matches partition plan
  - transfer bounds and sector multiples
  - full target SHA-256 after required flush
```

The IOP tries to allocate two 64 KiB staging buffers (+ alignment slop). If that
fails it falls back to one stage, preserving correctness while losing prefetch.
The second stage is therefore an optimization state, not an admission contract.

The producer schedule for COPY is already:

```text
obtain current source stage
submit/schedule next source read into alternate stage
write current stage to HDD
DMA current stage to EE
return to EE
EE consumes/hash current destination
request next chunk
```

This is already `submit early` for USB prefetch.

## Current bottleneck candidates after ownership audit

### 1. Redundant source replay on recovery

Status: **implemented as isolated experiment; hardware gate pending.**

This is the highest-priority work-removal candidate because it can eliminate a
prefix or complete ISO pass rather than merely shorten a control operation.

### 2. Source reopening at PAYLOAD_VERIFIED with a complete checkpoint

Status: **implemented in the isolated resume-hash experiment; CI/hardware gate
pending.**

A matching full checkpoint already provides the original producer digest. HDD
read-back remains the correctness consumer; current USB media is unnecessary
unless checkpoint restore fails.

### 3. Persistent HDL catalogue index

Status: **design not yet justified.**

Do not implement merely because persistent caches sound fast. The current
catalogue already caches metadata per menu session and loads it lazily. First
prove a cheap invalidation/generation contract that detects external APA
mutation without replaying the same full chain walk.

### 4. SIF DMA completion wait / second EE destination

Status: **defer until real-hardware attribution.**

Current IOP `dma_to_ee()` submits `sceSifSetDma()` and waits for completion before
returning the ioctl. Simply removing that wait is invalid because EE currently
uses a single destination buffer and hashes it immediately after the synchronous
ioctl returns.

A real overlap experiment therefore requires a new ownership protocol, for
example two EE consumer buffers or a ring with explicit FREE/FILLING/READY/
CONSUMING states. It is only justified if PROFILE ON hardware telemetry shows
`sif-dma-completion` is materially exposed on the critical path after USB/HDD
work is accounted for.

### 5. Journal/sidecar small-file metadata churn

Status: **measure before redesign.**

The optional sidecar adds a 256-byte write/readback/replace transaction on the
same 32 MiB boundary as the existing 512-byte journal. Combining records or
changing journal ABI to save these operations is a larger correctness change
than the current evidence justifies. Release-like A/B must first show a
meaningful uninterrupted-install regression attributable to checkpoint
maintenance.

## Next measurement decisions

1. Finish the resume-hash matched A/B and correctness matrix on real PS2.
2. Use PROFILE ON only to attribute USB/HDD/SIF/EE time; use PROFILE OFF for the
   release-like acceptance result.
3. If SIF completion is materially exposed, design explicit EE/IOP ownership
   before adding another buffer.
4. If catalogue entry latency is materially user-visible, identify a cheap and
   correct mutation-generation contract before implementing persistent cache.
5. Re-profile whole-system behavior after any accepted change because the
   bottleneck may move.
