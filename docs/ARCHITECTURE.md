# Architecture and safety invariants

PS2 HDD Bootstrap Manager is intentionally conservative: it is allowed to be slower than a desktop disk utility, but it is not allowed to be casual about a real APA disk. The EE application owns policy and user interaction; PS2SDK IOP modules own device access, PFS mounting, MagicGate services, and the APA driver interface.

## Repository layout

The `0.4.0-dev` **Michishirube** line is progressively converting the former monolithic EE application into explicit modules. The split is intentionally mechanical first: code moves behind headers without simultaneously changing storage semantics.

- `src/main.c` — application state machine, diagnostics presentation/timing, write authorization/confirmation, operation-specific user-facing error policy, and composition of narrow subsystem interfaces.
- `src/platform.c` — IOP reset, embedded IRX startup, pad initialization, button-edge input, and confirmation-chord input.
- `src/storage.c` — storage target definitions, encapsulated selected-target state, launch-device selection, ROMVER access, and generic fileXio helpers. Callers read/change the selection through validated accessors instead of mutating module state directly.
- `src/header_backup.c` — PS2 storage-side mandatory/legacy APA-header backup mechanics: protected slot naming, non-overwrite/reuse behavior, exact read-back verification, same-disk legacy lookup, and explicit per-slot diagnostics. It has no raw HDD write interface or UI/logging policy.
- `src/rescue_image.c` — PS2SDK-free validation of one complete rescue image: metadata decode, APA/payload SHA-256 verification, APA structure/flag checks, optional KELF-size consistency, and protected-slot state identity comparison.
- `src/rescue_storage.c` — PS2-only rescue lifecycle: protected `HDDRESCUE*.BIN` slots, USB retry/file I/O, active-payload acquisition through `hdd_read`, save/read-back verification, same-disk lookup, and preservation of the damaged/wrong-disk no-silent-fallback rule.
- `src/bootstrap_source.c` — PS2-only pre-sign installation-source preparation: writable `MBR.XLF` path for the `MBR.XIN` compatibility shim, bounded/USB-retried load, pre-sign KELF validation, sector count, and live `__mbr` reserved-capacity check. It cannot sign or write the HDD.
- `src/bootstrap_signing.c` — PS2-only security adapter: one-time `SecrInit`, `SecrDownloadFile` through the selected memory-card port, and post-sign structural KELF validation. It owns no card-selection UI, storage, or HDD transaction.
- `src/apa.c` — portable, read-only APA master-header core: little-endian parsing, checksum, normal `__mbr` validation, hybrid-GPT guard, and same-disk identity comparison.
- `src/hdd_bounds.c` — PS2SDK-free payload-pointer shape and explicit `__mbr` geometry policy. It preserves the stable `-170..-173` result domain and is shared by live PS2 reads and generated raw-HDD host fixtures.
- `src/hdd_read.c` — PS2-only read-only raw HDD transport: bounded `HDIOC_READSECTOR` access, live `hdd0:__mbr` geometry acquisition, and sector-aligned active-payload acquisition. It exposes no write or pointer-update operation.
- `src/hdd_write.c` — PS2-only write-capable transport: raw `HDIOC_WRITESECTOR` packets, write-side DMA/read-back buffers, flushes, byte comparison, `HDIOC_SETOSDMBR`, and final APA/pointer read-back verification. It owns mechanics only, not backup/confirmation/signing or transaction ordering.
- `src/bootstrap_transaction.c` — PS2SDK-free post-confirmation transaction sequencer. It preserves raw transport errors, records the failed stage, releases the caller-owned payload immediately after write/verify, and guarantees that pointer update/verification cannot occur after payload failure.
- `src/bootstrap_transaction_ps2.c` — thin PS2 binding from the portable sequencer to `hdd_write`; it adds no policy of its own.
- `src/boot_payload.c` — PS2SDK-free conversion of a sector-aligned active payload into byte counts, sector-image SHA-256, KELF structural result/size, and unpadded-KELF SHA-256.
- `src/boot_payload_ps2.c` — narrow PS2 acquisition adapter that combines `hdd_read` with portable `boot_payload` fingerprinting and fills only payload-derived boot-chain evidence.
- `src/boot_chain.c` — PS2SDK-free boot-chain evidence model, CNF parsing, ROMVER mapping, target parsing, and family-classification policy.
- `src/boot_chain_ps2.c` — PS2-only but read-only evidence collection from memory cards and PFS partitions. It owns no raw HDD write or pointer update.
- `src/boot_diagnostics_ps2.c` — PS2-only orchestration that initializes one evidence snapshot, combines ROMVER/filesystem/raw-payload sources, and invokes portable classification. It performs no rendering, persistence, UI, or disk-changing operation.
- `src/boot_report.c` — PS2SDK-free, bounded rendering of a completed evidence snapshot into the human-readable `BOOTCHAIN.TXT` image. It performs no device access or persistence.
- `src/boot_report_ps2.c` — PS2-only `BOOTCHAIN.TXT` persistence, including storage-path construction and the existing USB mount grace period; it performs no rendering or classification.
- `src/boot_report_session.c` — owns the latest rendered report buffer, length, and last save result while delegating formatting to `boot_report.c` and device I/O to `boot_report_ps2.c`.
- `src/session_log.c` — bounded ordered session logging plus `HDDMAN.LOG` append/rotation/USB-retry persistence. It owns no boot-chain classification or HDD write transaction.
- `src/kelf.c` — portable structural KELF parser and recovery of the real file length from a sector-padded HDD image. It reads the PS2SDK-defined wire fields explicitly as little-endian bytes rather than relying on a target-native struct cast.
- `src/mbr_compat.c` — narrow `MBR.XIN` / `MBR.XLF` filename compatibility interposition used by `bootstrap_source`.
- `src/sha256.c` — portable SHA-256 used for rescue integrity and payload fingerprints.
- `src/capsule_format.c` — endian-stable rescue capsule serialization.
- `tools/generate_hdd_fixtures.py` — deterministic sparse raw-HDD generator for valid APA, corrupt APA/pointer/payload, GPT-only, and hybrid APA/GPT host cases.
- `include/` — module interfaces shared by the EE build and, where practical, host tests.
- `tests/` — portable code that can run without PS2SDK, including synthetic raw-HDD images, APA/bounds, boot-chain, boot-payload, boot-report, rescue-image validation, bootstrap-transaction ordering/failure injection, and malformed KELF regression cases.
- `docs/` — format, architecture, fixture, and roadmap documentation.

### Modularization rule

Moving code between translation units is easy; proving that initialization, DMA-buffer lifetime, mount state, error propagation, and write ordering did not change is the expensive part. Each extraction therefore follows this sequence:

1. move one coherent responsibility with the smallest possible call-site diff;
2. preserve existing function names and return values during the mechanical pass when useful;
3. add or extend portable regression tests whenever the code has no PS2SDK dependency;
4. complete a warning-clean R5900 release build with the pinned PS2DEV toolchain;
5. only then consider API cleanup, state encapsulation, or additional optimization.

The dangerous path is now layered instead of concentrated in `main.c`. `header_backup` owns the mandatory backup storage mechanics; `rescue_image`/`rescue_storage` own rescue validation and lifecycle; `bootstrap_source` prepares an installable KELF and validates capacity; `bootstrap_signing` contains the console security operation; `hdd_write` owns raw write/flush/read-back mechanics; and portable `bootstrap_transaction` owns only the already-authorized commit order. `main.c` still decides whether an operation is allowed, asks the user for confirmation, selects the signing card when necessary, maps subsystem failures to user-facing behavior, and decides which transaction to invoke. Boot-chain policy, filesystem evidence collection, payload fingerprinting, KELF format parsing, and report formatting are separated for the same reason.

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

## APA and write-transaction boundary

`apa.c` is deliberately portable and has no fileXio or PS2SDK dependency. Host tests validate both isolated 1024-byte APA headers and complete generated raw-HDD fixtures. The raw suite covers valid disabled/enabled images, checksum/signature corruption, inconsistent and out-of-range pointers, valid/invalid KELF sectors, deterministic garbage, GPT-only input, and a checksummed hybrid APA/GPT image.

Pure pointer policy now lives in `hdd_bounds.c`. `hdd_validate_payload_shape()` preserves the historical empty/too-large/before-reserved precedence without device access, while `hdd_validate_payload_bounds_geometry()` applies an explicit `__mbr` start/size. `hdd_read.c` calls the shape check before `fileXioGetStat()` exactly as Torii did, then delegates the final live geometry decision to the portable function. This lets host fixtures exercise the same `-170..-173` policy without a fake fileXio layer.

The current `is_hybrid_gpt()` guard remains intentionally conservative: it checks the conventional `0x55AA` PC MBR signature. The fixture suite separately records the real `EFI PART` signature at LBA 1. A PC-signature-only APA case protects existing behavior, while GPT-only and APA+GPT cases make a future stricter GPT parser testable instead of silently changing the current guard.

Read-only `HDIOC_READSECTOR` transport and active-payload acquisition live in `hdd_read.c`. The module uses its own aligned two-sector read buffer and can only return bytes or validation errors; it cannot flush, write sectors, or update `osdStart`/`osdSize`.

Write-capable transport is isolated in `hdd_write.c`: it owns the raw write packet, aligned verification buffers, `HDIOC_WRITESECTOR`, `HDIOC_SETOSDMBR`, flushes, payload byte comparison, and final APA/pointer read-back. Its interface intentionally exposes those operations as separate steps rather than a single "install" call.

Pre-write authorization remains application policy. `main.c` requires successful header backup, completed source/rescue validation, the appropriate signing result, and the explicit confirmation chord before entering the commit sequencer. The mechanics behind those checks are delegated to narrow modules, so `main.c` no longer needs file layouts, raw write packets, MagicGate calls, or payload-write loops to enforce the policy.

The already-authorized commit phase lives in portable `bootstrap_transaction.c`. Host tests assert `payload -> release -> pointer set -> pointer verify`, assert immediate stop at each injected failure, and specifically prove that pointer exposure never follows a failed payload write/compare. `bootstrap_transaction_ps2.c` only binds those abstract operations to `hdd_write.c`, so the tested ordering policy is independent of PS2SDK.

Keeping read-only `hdd_read.c` separate from write-capable `hdd_write.c` also prevents diagnostics from acquiring a write interface merely because both paths need raw-sector read-back.

## Rescue and installation preparation boundary

The stable rescue wire format remains in `capsule_format.c`. `rescue_image.c` consumes a complete in-memory file and checks metadata, hashes, APA validity, and KELF-size consistency without PS2SDK. This makes corruption behavior host-testable without inventing a fake fileXio environment. `rescue_storage.c` owns device paths, slot protection, current-payload acquisition, read/write retry behavior, same-disk selection, and file lifetime. `main.c` receives a validated `rescue_storage_entry_t`, applies the existing live bounds and mandatory-backup gates, shows the confirmation UI, then passes the payload to `bootstrap_transaction`.

Manual installation follows the same separation. `bootstrap_source.c` loads the source, preserves `MBR.XIN`/`MBR.XLF` compatibility through the existing wrapper, validates the unsigned KELF, calculates its sectors, and checks current reserved capacity. `bootstrap_signing.c` performs the console-side MagicGate mutation and immediately re-validates the signed KELF. Only then does `main.c` show the final install confirmation and enter the transaction sequencer. Neither source preparation nor signing can write the HDD.

## Boot-chain reporting boundary

Boot-chain diagnostics now have explicit acquisition, policy, rendering, and persistence layers instead of one function that understood every device and output detail:

1. `boot_chain.c` and `boot_chain_ps2.c` define the evidence model, portable classification policy, and filesystem/configuration evidence that does not require raw HDD writes.
2. `boot_payload_ps2.c` acquires the active sector image through read-only `hdd_read.c`; portable `boot_payload.c` performs hashing and KELF-size/structure conversion.
3. `boot_diagnostics_ps2.c` initializes and combines those evidence sources, then invokes portable `classify_boot_chain()`; `main.c` no longer contains evidence-collection orchestration.
4. `boot_report.c` receives the completed `boot_chain_info_t` plus explicit `osdStart`/`osdSize` values and renders the bounded text image.
5. `boot_report_session.c` owns the latest rendered text image and last save result, delegating actual file persistence to `boot_report_ps2.c`; `session_log.c` owns ordered `HDDMAN.LOG` buffering, append/rotation, and storage retry policy. `main.c` retains scan timing/presentation and the short diagnostics screen.

The renderer has no fileXio, PFS, memory-card, or raw-HDD dependency. Its complete section order, assessment precedence, SHA-256 text, and truncation behavior can therefore be checked on a host. A full disabled-state golden fixture protects the human-readable contract users paste into bug reports, while targeted fixtures cover active evidence, warning/critical branches, the external-HDD-module note, and NUL termination under a deliberately tiny output capacity.

The detailed text contract and non-goals are documented in [`BOOT_REPORT.md`](BOOT_REPORT.md).

## KELF module boundary

`kelf.c` contains format policy only. The PS2SDK public layout describes a 32-byte fixed KELF header followed by up to 63 16-byte BIT entries, optional header areas selected by flags, and the remaining key/check material. Michishirube decodes the little-endian size, flag, and BIT-count fields by byte offset so the same malformed input produces the same result on the EE and on a desktop host.

The parser deliberately does **not** decrypt, identify, sign, or execute a KELF. It only verifies the structural conditions already enforced by Torii and recovers the unpadded KELF length from a sector-aligned payload image. Stable named result enums preserve Torii's numeric validation codes so existing diagnostics do not silently change meaning during the extraction.

Host fixtures cover normal low/high flag layouts, the length-prefixed variable area, the maximum 63-entry BIT table, plain-ELF rejection, truncated or impossible headers, BIT-table overflow, missing variable/key areas, and sector-padding recovery. The actual console-side signing operation lives in PS2-specific `bootstrap_signing.c`, which calls the portable KELF parser again after `SecrDownloadFile()` mutates the image.

## EE / IOP boundary

Raw HDD operations use `fileXioDevctl()` and DMA-safe aligned buffers. The two-sector transfer size is deliberately conservative: fileXio's devctl RPC packets use fixed-size buffers, and the current write packet plus its command header stays comfortably below that boundary. Larger batches should be benchmarked and hardware-tested rather than inferred from the theoretical maximum.

PFS partitions are mounted read-only for boot-chain evidence. Configuration scanning and family classification are advisory diagnostics; encrypted KELFs are structurally validated and fingerprinted, not identified from imaginary plaintext signatures.

`platform.c` owns the lifetime-sensitive pad DMA buffer and embedded IRX startup order. The IRX dependency order is intentionally unchanged from Torii and should be treated as behavior until independently tested otherwise.

## R5900 optimization policy

The release build uses `-O2 -G0` plus section garbage collection. Torii added two targeted SHA-256 improvements that are portable and directly testable:

- a 16-word rolling message schedule instead of a 64-word stack array;
- direct transformation of complete 64-byte caller blocks instead of copying every block into the context buffer first.

This reduces transform-local schedule storage from 256 bytes to 64 bytes and removes a 64-byte copy for each complete block of a multi-megabyte rescue payload while preserving byte-for-byte SHA-256 output.

`-O3`, speculative MIPS flags, LTO, or larger HDD devctl batches should only be adopted after size/speed measurement and real-console regression testing. The tool spends much of its important time waiting on IOP/HDD I/O; making risky code harder to reason about for a tiny EE-side win is not an optimization.

## Commenting rule

Comments should explain hardware constraints, safety ordering, format contracts, module ownership, and non-obvious PS2 behavior. They should not narrate trivial C syntax. Error codes and unusual constants need either a named macro or a nearby explanation that lets a future maintainer understand why changing them could be dangerous.
