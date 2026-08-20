# Architecture and safety invariants

PS2 HDD Bootstrap Manager is intentionally conservative: it is allowed to be slower than a desktop disk utility, but it is not allowed to be casual about a real APA disk. The EE application owns policy and user interaction; PS2SDK IOP modules own device access, PFS mounting, MagicGate services, and the APA driver interface.

## Repository layout

- `src/main.c` — EE application state machine, storage helpers, APA validation, boot-chain inspection, and guarded write operations.
- `src/sha256.c` — portable SHA-256 used for rescue integrity and payload fingerprints.
- `src/capsule_format.c` — endian-stable rescue capsule serialization.
- `include/` — headers shared by the EE build and host tests, including release identity.
- `tests/` — portable code that can run without PS2SDK.
- `docs/` — format, architecture, and roadmap documentation.

`src/main.c` remains a single translation unit in 0.3.0 on purpose. Moving functions between files is easy; proving that no initialization, DMA-buffer, mount, or write-order assumption changed is the expensive part. Its logical sections are already isolated and are the boundaries planned for physical modules after Torii has a broader regression baseline.

## Non-negotiable write invariants

Every HDD-changing path must preserve these rules:

1. Validate the APA master header, checksum, and non-hybrid layout first.
2. Save and read back a current 1024-byte header backup before the write.
3. Require an explicit multi-button confirmation chord.
4. Restrict raw writes to the reserved `__mbr` payload area; never raw-write sectors 0 or 1.
5. Flush and compare payload bytes before exposing a new payload through `osdStart`/`osdSize`.
6. Change the APA pointer through `ps2hdd`, then re-read and verify it.
7. Treat a damaged or wrong-disk full rescue capsule as an error, not as permission to silently fall back to a weaker restore.

An optimization that changes any of these semantics is a behavior change, not a cleanup.

## EE / IOP boundary

Raw HDD operations use `fileXioDevctl()` and DMA-safe aligned buffers. The two-sector transfer size is deliberately conservative: fileXio's devctl RPC packets use fixed-size buffers, and the current write packet plus its command header stays comfortably below that boundary. Larger batches should be benchmarked and hardware-tested rather than inferred from the theoretical maximum.

PFS partitions are mounted read-only for boot-chain evidence. Configuration scanning and family classification are advisory diagnostics; encrypted KELFs are structurally validated and fingerprinted, not identified from imaginary plaintext signatures.

## R5900 optimization policy

The release build uses `-O2 -G0` plus section garbage collection. Torii adds two targeted SHA-256 improvements that are portable and directly testable:

- a 16-word rolling message schedule instead of a 64-word stack array;
- direct transformation of complete 64-byte caller blocks instead of copying every block into the context buffer first.

This reduces transform-local schedule storage from 256 bytes to 64 bytes and removes a 64-byte copy for each complete block of a multi-megabyte rescue payload while preserving byte-for-byte SHA-256 output.

`-O3`, speculative MIPS flags, LTO, or larger HDD devctl batches should only be adopted after size/speed measurement and real-console regression testing. The tool spends much of its important time waiting on IOP/HDD I/O; making risky code harder to reason about for a tiny EE-side win is not an optimization.

## Commenting rule

Comments should explain hardware constraints, safety ordering, format contracts, and non-obvious PS2 behavior. They should not narrate trivial C syntax. Error codes and unusual constants need either a named macro or a nearby explanation that lets a future maintainer understand why changing them could be dangerous.
