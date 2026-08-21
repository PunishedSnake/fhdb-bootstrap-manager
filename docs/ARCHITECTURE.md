# Architecture and safety invariants

PS2 HDD Bootstrap Manager is intentionally conservative: it is allowed to be slower than a desktop disk utility, but it is not allowed to be casual about a real APA disk. The EE application owns policy and user interaction; PS2SDK IOP modules own device access, PFS mounting, MagicGate services, and the APA driver interface.

## Repository layout

The `0.4.0-dev` **Michishirube** line is progressively converting the former monolithic EE application into explicit modules. The split is intentionally mechanical first: code moves behind headers without simultaneously changing storage semantics.

- `src/main.c` — application state machine, KELF/payload analysis, rescue/install workflows, report rendering, logging policy, raw HDD transport, and guarded pointer/write ordering that has not yet been extracted.
- `src/platform.c` — IOP reset, embedded IRX startup, pad initialization, button-edge input, and confirmation-chord input.
- `src/storage.c` — storage target definitions, launch-device selection, ROMVER access, and generic fileXio helpers. The first split temporarily exposes the selected target state so call sites remain behaviorally identical; later Michishirube work will encapsulate it after regression coverage exists.
- `src/apa.c` — portable, read-only APA master-header core: little-endian parsing, checksum, normal `__mbr` validation, hybrid-GPT detection, and same-disk identity comparison.
- `src/boot_chain.c` — PS2SDK-free boot-chain model and policy: CNF parsing, `Skip_HDD`, ROMVER folder mapping, FHDB/OSDMenu target parsing, and evidence classification.
- `src/boot_chain_ps2.c` — PS2-only but read-only evidence collector for memory cards, `__sysconf`, `__system`, OSDMenu, PSBBN, HOSDMenu, HDD-OSD, and FMCB settings.
- `src/mbr_compat.c` — narrow `MBR.XIN` / `MBR.XLF` filename compatibility interposition.
- `src/sha256.c` — portable SHA-256 used for rescue integrity and payload fingerprints.
- `src/capsule_format.c` — endian-stable rescue capsule serialization.
- `include/` — module interfaces shared by the EE build and, where practical, host tests.
- `tests/` — portable regression code that can run without PS2SDK, including synthetic APA and boot-chain fixtures.
- `docs/` — format, architecture, and roadmap documentation.

### Modularization rule

Moving code between translation units is easy; proving that initialization, DMA-buffer lifetime, mount state, error propagation, and write ordering did not change is the expensive part. Each extraction therefore follows this sequence:

1. move one coherent responsibility with the smallest possible call-site diff;
2. preserve existing function names and return values during the mechanical pass when useful;
3. add or extend portable regression tests whenever the code has no PS2SDK dependency;
4. complete a warning-clean R5900 release build with the pinned PS2DEV toolchain;
5. only then consider API cleanup, state encapsulation, or additional optimization.

Platform and generic storage helpers were extracted first because they do not own the dangerous APA write transaction. The APA module is intentionally split in two: its pure header logic has moved and is host-tested, while raw sector transport and pointer updates remain in `main.c` until a later gated step.

Boot-chain work follows the same pattern but uses two layers. `boot_chain.c` contains deterministic policy that can be fed synthetic evidence on a PC. `boot_chain_ps2.c` only gathers real-console evidence with read-only file and PFS operations. This prevents filesystem probing from becoming inseparable from classification policy again.

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

## Boot-chain module boundary

`boot_chain_info_t` is now the shared evidence model instead of a private type embedded in `main.c`. The portable core has no PS2SDK dependency and owns:

- exact, case-insensitive CNF key parsing with bounded output;
- current `OSDSYS_Skip_HDD` plus legacy `Skip_HDD` interpretation;
- `boot_auto` and `LK_Auto_E1`/`E2`/`E3` target interpretation;
- ROMVER region-to-FMCB-folder mapping;
- classification priority and confidence labels.

The PS2 scanner owns only observation: memory-card module presence, configuration reads, read-only PFS mounts, downstream executable checks, and PSBBN partition evidence. It does not write a file, raw sector, APA field, or rescue capsule.

`analyze_boot_chain()` intentionally remains in `main.c` for now. It still reads the active MBR payload through the raw-HDD transport layer, validates/fingerprints its KELF, then combines those results with the read-only scanner. Moving it before the APA transport boundary is explicit would create a misleading dependency direction. Report rendering also remains separate from the evidence model until its output contract is tested.

The portable boot-chain suite includes deliberately contradictory fixtures. In particular, explicit OSDMenu `$PSBBN` / `$HOSDSYS` / custom targets must outrank stale partitions or executables, preserving the Torii safety fix against confident misclassification of leftover environments.

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
