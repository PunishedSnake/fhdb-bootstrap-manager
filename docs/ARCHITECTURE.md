# Architecture and safety invariants

PS2 HDD Bootstrap Manager is intentionally conservative: it is allowed to be slower than a desktop disk utility, but it is not allowed to be casual about a real APA disk. The EE application owns policy and user interaction; PS2SDK IOP modules own device access, PFS mounting, MagicGate services, and the APA driver interface.

## Repository layout

The `0.4.0-dev` **Michishirube** line is progressively converting the former monolithic EE application into explicit modules. The split is intentionally mechanical first: code moves behind headers without simultaneously changing storage semantics.

- `src/main.c` — application state machine, raw active-payload acquisition, boot-chain report orchestration, rescue/install workflows, logging policy, raw HDD transport, and guarded pointer/write ordering that has not yet been extracted.
- `src/platform.c` — IOP reset, embedded IRX startup, pad initialization, button-edge input, and confirmation-chord input.
- `src/storage.c` — storage target definitions, launch-device selection, ROMVER access, and generic fileXio helpers. The first split temporarily exposes the selected target state so call sites remain behaviorally identical; later Michishirube work will encapsulate it after regression coverage exists.
- `src/apa.c` — portable, read-only APA master-header core: little-endian parsing, checksum, normal `__mbr` validation, hybrid-GPT detection, and same-disk identity comparison.
- `src/boot_chain.c` — PS2SDK-free boot-chain evidence model, CNF parsing, ROMVER mapping, target parsing, and family-classification policy.
- `src/boot_chain_ps2.c` — PS2-only but read-only evidence collection from memory cards and PFS partitions. It owns no raw HDD write or pointer update.
- `src/kelf.c` — portable structural KELF parser and recovery of the real file length from a sector-padded HDD image. It reads the PS2SDK-defined wire fields explicitly as little-endian bytes rather than relying on a target-native struct cast.
- `src/mbr_compat.c` — narrow `MBR.XIN` / `MBR.XLF` filename compatibility interposition.
- `src/sha256.c` — portable SHA-256 used for rescue integrity and payload fingerprints.
- `src/capsule_format.c` — endian-stable rescue capsule serialization.
- `include/` — module interfaces shared by the EE build and, where practical, host tests.
- `tests/` — portable code that can run without PS2SDK, including synthetic APA, boot-chain, and malformed KELF regression cases.
- `docs/` — format, architecture, and roadmap documentation.

### Modularization rule

Moving code between translation units is easy; proving that initialization, DMA-buffer lifetime, mount state, error propagation, and write ordering did not change is the expensive part. Each extraction therefore follows this sequence:

1. move one coherent responsibility with the smallest possible call-site diff;
2. preserve existing function names and return values during the mechanical pass when useful;
3. add or extend portable regression tests whenever the code has no PS2SDK dependency;
4. complete a warning-clean R5900 release build with the pinned PS2DEV toolchain;
5. only then consider API cleanup, state encapsulation, or additional optimization.

Platform and generic storage helpers were extracted first because they do not own the dangerous APA write transaction. The APA module is intentionally split in two: its pure header logic has moved and is host-tested, while raw sector transport and pointer updates remain in `main.c` until a later gated step. Boot-chain policy and filesystem evidence collection are now separated from raw payload acquisition for the same reason.

## Non-negotiable write invariants

Every HDD-changing path must preserve these rules:

1. Validate the APA master header, checksum, and non-hybrid layout first.
2. Save and read back a current 1024-byte header backup before the write.
3. Require an explicit multi-button confirmation chord.
4. Restrict raw writes to the reserved `__mbr` payload area; never raw-write sectors 0 or 1.
5. Flush and compare payload bytes before exposing a new payload through `osdStart`/`osdSize`.
6. Change the APA pointer through `ps2hdd`, then re-read and verify it.
7. Treat a damaged or wrong-disk full rescue capsule as an error, not as permission to silently fall back to a weaker restore.

An optimization or refactor that changes any of these semantics is a behavior change, not a cleanup.

## APA module boundary

`apa.c` is deliberately portable and has no fileXio or PS2SDK dependency. Synthetic host tests construct a valid 1024-byte `__mbr` header, verify the checksum and pointer decoding, exercise hybrid-GPT detection, reject corrupted identity data, and confirm that same-disk matching ignores only the checksum and mutable `osdStart`/`osdSize` fields.

The following operations still belong to the not-yet-extracted transport half in `main.c`:

- `HDIOC_READSECTOR` / `HDIOC_WRITESECTOR` RPC packets and aligned transfer buffers;
- active-payload bounds checks against the actual `hdd0:__mbr` partition;
- `HDIOC_SETOSDMBR` pointer changes and flushes;
- post-write sector comparison and final header read-back.

Keeping that boundary explicit prevents a testable parser extraction from accidentally becoming an unreviewed rewrite of the write path.

## KELF module boundary

`kelf.c` contains format policy only. The PS2SDK public layout describes a 32-byte fixed KELF header followed by up to 63 16-byte BIT entries, optional header areas selected by flags, and the remaining key/check material. Michishirube decodes the little-endian size, flag, and BIT-count fields by byte offset so the same malformed input produces the same result on the EE and on a desktop host.

The parser deliberately does **not** decrypt, identify, sign, or execute a KELF. It only verifies the structural conditions already enforced by Torii and recovers the unpadded KELF length from a sector-aligned payload image. Stable named result enums preserve Torii's numeric validation codes so existing diagnostics do not silently change meaning during the extraction.

Host fixtures cover normal low/high flag layouts, the length-prefixed variable area, the maximum 63-entry BIT table, plain-ELF rejection, truncated or impossible headers, BIT-table overflow, missing variable/key areas, and sector-padding recovery. MagicGate signing itself remains in the PS2-specific installation workflow because that operation depends on `secrman`, a real memory card, and console security hardware.

## EE / IOP boundary

Raw HDD operations use `fileXioDevctl()` and DMA-safe aligned buffers. The two-sector transfer size is deliberately conservative: fileXio's devctl RPC packets use fixed-size buffers, and the current write packet plus its command header stays comfortably below that boundary. Larger batches should be benchmarked and hardware-tested rather than inferred from the theoretical maximum.

PFS partitions are mounted read-only for boot-chain evidence. Configuration scanning and family classification are advisory diagnostics; encrypted KELFs are structurally validated and fingerprinted, not identified from imaginary plaintext signatures.

`platform.c` now owns the lifetime-sensitive pad DMA buffer and embedded IRX startup order. The IRX dependency order is intentionally unchanged from Torii and should be treated as behavior until independently tested otherwise.

## R5900 optimization policy

The release build uses `-O2 -G0` plus section garbage collection. Torii added two targeted SHA-256 improvements that are portable and directly testable:

- a 16-word rolling message schedule instead of a 64-word stack array;
- direct transformation of complete 64-byte caller blocks instead of copying every block into the context buffer first.

This reduces transform-local schedule storage from 256 bytes to 64 bytes and removes a 64-byte copy for each complete block of a multi-megabyte rescue payload while preserving byte-for-byte SHA-256 output.

`-O3`, speculative MIPS flags, LTO, or larger HDD devctl batches should only be adopted after size/speed measurement and real-console regression testing. The tool spends much of its important time waiting on IOP/HDD I/O; making risky code harder to reason about for a tiny EE-side win is not an optimization.

## Commenting rule

Comments should explain hardware constraints, safety ordering, format contracts, module ownership, and non-obvious PS2 behavior. They should not narrate trivial C syntax. Error codes and unusual constants need either a named macro or a nearby explanation that lets a future maintainer understand why changing them could be dangerous.
