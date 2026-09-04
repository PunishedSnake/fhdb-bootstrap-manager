# HDL source fingerprint heap-alignment experiment

This record binds the incremental Phase-5 experiment that keeps transaction
workspace v1 from CI #724 but replaces only `source_fingerprint()`'s
`memalign(64, 65536)` with ordinary `malloc(65536)`.

## Source-of-truth routing

- `PS2_Optimization_Library_v2_MANIFEST.md`
- `PS2_PERFORMANCE_BIBLE.md`
- `PS2_Memory_Allocators_optimization_research_corpus_v2.md`
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`
- `PS2_PS2SDK_optimization_research_corpus_v2.md`
- pinned PS2SDK commit `b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b`
- `docs/ALIGNMENT_CONTRACT_AUDIT.md`

## Contract

**CURRENT IMPLEMENTATION:** pinned PS2SDK `fileXioRead()` accepts an arbitrary EE
caller buffer, performs cache writeback for the supplied range, and handles edge
bytes through its normal read/RPC residual path. The application pointer is not
required by the fileXio API to start on a 64-byte cache-line boundary.

`source_fingerprint()` is consumed by ordinary source `fileXioRead()` plus EE
SHA-256. It is not the custom `hdl0:` fast SIF/DMA destination.

Therefore:

```text
source_fingerprint heap alignment
  64 B: not a correctness/API requirement

transaction COPY/target-verify workspace
  64 B: KEEP, custom hdl_fast_dma_read() contract
```

This is deliberately not a global `memalign(64) -> malloc` policy.

## Hypothesis

**HIPOTEZA DO TESTU:** removing one unnecessary aligned allocation may reduce a
small amount of allocator work without changing source bytes, hashing or
ownership lifetime.

Counter-hypothesis: an arbitrary `malloc()` address may make the two 64 KiB
fingerprint reads use fileXio's unaligned edge handling, so real hardware can be
neutral or worse even though the API permits it.

## CI #739 identity

Source point:

```text
503ac0274b3cde18814cc7e1d170b9f656fb2615
```

Artifact digest:

```text
sha256:b958018ee0e7cc5ab184f430794d592ce430342c87faeaaaf1993e21ad6f1ff0
```

Frozen workspace-v1 reference remains CI #724.

### PROFILE OFF

Workspace-v1 #724:

```text
ELF      632756 B
sha256   23bbf6dfc28eb87bc7d484875a8940b9309eb5e3994d9c922388c9a0249415c6
named text 229764 B
instructions 57491
execute_transaction 6008 B / 1502 instructions
```

Fingerprint-malloc #739:

```text
ELF      632756 B
sha256   97e2a802952ae6f3b46c9fa0148359db8f8b69e22923f8105378f094de59c28b
named text 229756 B
instructions 57488
execute_transaction 6008 B / 1502 instructions
```

Incremental static delta:

```text
ELF                 0 B
named text          -8 B
instructions        -3
execute_transaction  0 B / 0 instructions
```

### PROFILE ON

Workspace-v1 #724:

```text
ELF      638260 B
sha256   09185cd6a21bbb9990d0b7f8cfe70fa80b4e9ba01a00e9648cb9fa70d9b3d693
named text 232560 B
instructions 58190
execute_transaction 6008 B / 1502 instructions
```

Fingerprint-malloc #739:

```text
ELF      638132 B
sha256   c8da50fe5147c3a24dc2f26d4ab910660bac615bf8e26f48e0bff3a2483f578b
named text 232552 B
instructions 58189
execute_transaction 6008 B / 1502 instructions
```

Incremental static delta:

```text
ELF               -128 B
named text          -8 B
instructions        -1
execute_transaction  0 B / 0 instructions
```

Both experiment IRX files remain byte-identical to the frozen Phase-0 pair:

```text
PROFILE OFF f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
PROFILE ON  8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db
```

## Decision

**KEEP AS A HARDWARE HYPOTHESIS, NOT A MEASURED SPEEDUP.**

Unlike workspace v2, this change does not lengthen a 64 KiB lifetime or grow the
hot transaction controller. Static code is neutral-to-smaller. That is enough to
keep the experiment available for real-console A/B, but not enough to promote it
to the default runtime.

Acceptance requires the same source ISO and interleaved baseline/experiment
runs, with source-fingerprint and total transaction p50/p95/p99/max plus
correctness hash. If unaligned fileXio edge handling causes a repeatable tail or
wall-time regression, retain `memalign(64)` despite the absence of an API
requirement.
