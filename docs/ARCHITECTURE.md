# Architecture and safety invariants

PS2 HDD Bootstrap Manager is intentionally conservative: it may be slower or refuse ambiguous repairs rather than turn uncertain metadata into authoritative disk state. The `0.4.x` **Michishirube** line separates portable policy, PS2-specific device mechanics, application controllers, and UI so that dangerous behavior can be tested independently from the console services that execute it.

## Layer model

### 1. Portable policy and format core

These modules compile and run without PS2SDK and must not perform device I/O:

- `apa.c` — little-endian APA parsing, checksum, standard `__mbr` recognition, conservative hybrid guard, and same-disk identity comparison.
- `apa_repair.c` — fail-closed planning for narrowly reconstructable APA master fields. It does not write anything.
- `repair_health.c` — combines the header repair plan with already-evaluated pointer bounds and optional active-payload evidence to decide whether normal pointer clear should be recommended.
- `hdd_bounds.c` — deterministic payload pointer/geometry policy and stable error ordering.
- `kelf.c` — structural KELF parsing and recovery of unpadded size from sector images.
- `bootstrap_transaction.c` — already-authorized payload-first/pointer-last sequencing with stage reporting and failure injection.
- `rescue_image.c` and `capsule_format.c` — rescue image validation and endian-stable serialization.
- `boot_chain.c`, `boot_payload.c`, and `boot_report.c` — evidence classification, payload fingerprinting, and report rendering.
- `sha256.c` — portable hashing used by rescue and diagnostics.

Portable code may say **what is valid**, **what is repairable**, or **what operation should follow**. It cannot reach the HDD, memory cards, PFS, MagicGate, or the controller.

### 2. PS2 device/service adapters

These modules bind narrow operations to PS2SDK:

- `hdd_read.c` — read-only `HDIOC_READSECTOR`, active-payload acquisition, and live `__mbr` geometry. It exposes no write, flush, or pointer update.
- `hdd_write.c` — normal bootstrap payload write/flush/read-back plus `HDIOC_SETOSDMBR` pointer update/verification. It owns mechanics, not authorization.
- `hdd_repair_ps2.c` — exceptional raw sectors 0-1 writer used only for planner-approved master recovery; it validates the proposed finished header, flushes, and requires exact read-back.
- `header_backup.c` — mandatory normal-operation header backup mechanics.
- `repair_snapshot.c` — exact non-overwriting `HDDRAW*.BIN` snapshot used before exceptional master-header recovery.
- `rescue_storage.c` — PS2-side rescue file lifecycle and payload acquisition.
- `bootstrap_source.c` — MBR.XIN/XLF-compatible source loading and pre-sign validation.
- `bootstrap_signing.c` — MagicGate signing and immediate post-sign KELF validation.
- `bootstrap_transaction_ps2.c` — thin binding from portable transaction sequencing to `hdd_write`.
- `boot_chain_ps2.c`, `boot_payload_ps2.c`, `boot_diagnostics_ps2.c`, `boot_report_ps2.c`, and `boot_report_session.c` — read-only evidence acquisition and report persistence.
- `storage.c`, `platform.c`, and `session_log.c` — launch/storage state, IOP/pad lifecycle, and bounded session logging.
- `hdd_recovery_wrap.c` — intercepts exactly the first `hdd0:` `HDIOC_STATUS` so a raw-readable but invalid master can enter guarded recovery before the normal admission gate rejects it.

A PS2 adapter may execute an operation, but it should not silently invent policy that belongs in the portable layer or the controllers.

### 3. Application controllers and UI

Michishirube no longer concentrates high-level workflows in `main.c`:

- `bootstrap_controller_ps2.c` — backup, disable, restore, and install authorization/error flow. It composes backup/rescue/source/signing/transaction modules but does not implement their formats or raw transport.
- `diagnostics_controller_ps2.c` — refreshes and presents boot-chain diagnostics.
- `repair_controller_ps2.c` — guarded startup/health repair presentation. Canonical repair policy comes from `apa_repair`; mounted-disk pointer-health policy comes from `repair_health`.
- `app_ui_ps2.c` — shared fatal/info/power/storage/signing-card presentation and lifecycle helpers.

`main.c` is the composition root: initialization, the normal APA admission gate, top-level menu state, and dispatch. New disk formats, repair algorithms, MagicGate mechanics, backup formats, or write loops do not belong there.

## Normal write-path invariants

The Torii-compatible backup/disable/restore/install paths preserve these rules:

1. `ps2hdd` must accept the device and the current master must pass standard APA/checksum/non-hybrid validation.
2. A current 1024-byte header backup must be saved and read back before an HDD-changing operation.
3. The user must authorize the specific operation with its multi-button confirmation chord.
4. Raw writes are confined to the reserved `__mbr` bootstrap program area beginning at sector `0x2000`; normal workflows do **not** raw-write sectors 0-1.
5. A new/restored payload is written, flushed, and compared before it can be exposed through the OSD pointer.
6. `osdStart`/`osdSize` are changed through `HDIOC_SETOSDMBR`, then the resulting header/pointer is read back and verified.
7. A damaged or wrong-disk rescue capsule is an error, not permission to silently fall back to a weaker restore.

These are behavior invariants, not implementation suggestions.

## Exceptional APA master recovery invariants

Master recovery is deliberately a separate contract because its purpose is to help when rule 1 above cannot be satisfied.

The only raw sectors 0-1 write path is `hdd_repair_ps2`, reached through `hdd_recovery_wrap` / `repair_controller_ps2`. It is allowed only when all applicable gates succeed:

1. Raw sectors 0-1 must be readable even if normal `HDIOC_STATUS`/APA admission fails.
2. `apa_repair` must identify a narrowly reconstructable canonical defect. Current automatic repair is restricted to one known master identity/anchor field (`APA\0`, `__mbr`, Sony MBR marker, master `start`, MBR type, or MBR version) under sufficient independent identity evidence.
3. The original checksum must be mismatched and correcting only that known field must restore the **old stored checksum**. A checksum-valid noncanonical header is not accepted automatically because additive checksum errors can cancel each other.
4. GPT/protective-MBR or insufficient-identity states are hard blockers.
5. The exact original 1024 bytes must be saved and read back as a non-overwriting `HDDRAW*.BIN` snapshot before any raw write.
6. The user must explicitly confirm the proposed repair.
7. The proposed 1024-byte result must itself pass complete standard APA/master/non-hybrid validation before `HDIOC_WRITESECTOR` is issued.
8. Exactly sectors 0-1 are written, the drive is flushed, both sectors are read back, and all 1024 bytes must compare exactly.
9. A successful master repair requires a restart so `ps2hdd` reinitializes from repaired on-disk state instead of continuing with pre-repair cached metadata.

Pointer-only anomalies, out-of-bounds OSD pointers, and active invalid KELFs do **not** use raw master repair. `repair_health` routes them to the established normal backup + `HDIOC_SETOSDMBR(0,0)` disable path.

## Why checksum-valid corruption is not enough evidence

APA uses an additive 32-bit checksum. It detects many accidental changes but is not collision-resistant. Two independent changes can have equal and opposite deltas, leaving the stored checksum apparently valid.

Therefore Michishirube distinguishes:

- **single known-field bit flip with stale checksum** — potentially automatically repairable when the candidate correction exactly restores the stored checksum;
- **checksum-only mismatch** — diagnostic only, because the corrupt word might be anywhere;
- **checksum-valid but semantically wrong canonical field** — ambiguous and blocked from automatic raw repair;
- **multiple/unknown corruption** — blocked from automatic raw repair and a candidate for forensic reconstruction instead.

The synthetic regression suite contains an explicit checksum-collision case so future cleanup cannot accidentally weaken this rule.

## Degraded / forensic read-only recovery (planned boundary)

A disk that cannot pass normal APA admission does not automatically become unreadable at the block level. `HDIOC_READSECTOR` can still provide raw sectors when the drive itself is accessible. This makes a future **forensic read-only** mode technically useful and substantially safer than speculative repair.

That mode should not pretend the disk is healthy. It should expose a reconstructed **candidate partition map** with confidence/evidence for each entry. A proposed scanner can combine several independent signals:

- valid or near-valid APA headers discovered at candidate LBAs;
- each header's `start`, `length`, `type`, flags, `main`, sub-partition table, `next`, and `prev` fields;
- forward and backward link agreement;
- reciprocal `next`/`prev` relationships between neighboring headers;
- main-partition references to sub-partition LBAs and matching sub headers;
- partition alignment, power-of-two sizing rules, disk bounds, and absence of impossible overlaps;
- checksum validity or a bounded set of plausible one/two-bit corrections;
- known system partition IDs/types where they provide supporting rather than decisive evidence;
- optional filesystem-level evidence such as readable PFS metadata after a candidate APA extent has been established.

The result should be a graph/constraint problem rather than a single guessed linked list. Multiple plausible maps may coexist. The UI can present candidates such as `A`, `B`, and `C`, explain which headers/links disagree, and allow export of the raw evidence without writing the HDD.

A degraded read-only map may then be used to:

- list likely partitions even when `ps2hdd` refuses the master;
- dump candidate partition extents to external storage;
- inspect PFS or known system content through a deliberately read-only adapter where feasible;
- generate a reconstruction report that can later inform a separately authorized repair.

It must **not** silently feed speculative geometry into normal write-capable `pfs`/APA paths. Read-only reconstruction and on-disk repair are separate trust levels.

## Multi-header / heavy-corruption repair direction

Repair beyond the current single-field master case is possible in principle because APA metadata is redundant across the disk. PS2SDK's own `hdck` logic already walks the partition list forward and backward, compares reciprocal links, repairs some `prev`/`next` inconsistencies, and can reconstruct certain sub-partition headers when their main partition still records the sub extent. That demonstrates that recovery can use neighboring headers rather than treating sector zero as the sole source of truth.

For Michishirube, broader repair should be staged:

1. **scan only** — raw-read candidate headers and construct all plausible maps;
2. **score/compare** — rank candidates using independent constraints, never checksum alone;
3. **preview** — show exactly which headers/fields would change for each reconstruction variant;
4. **snapshot** — export the original master plus every header that would be touched, ideally with a manifest/hash;
5. **repair one metadata unit at a time** — write/flush/read-back each header while preserving enough information to roll back;
6. **re-scan from raw disk** — require the repaired graph to become internally consistent before normal writable mounting is allowed.

A user-selectable reconstruction variant is reasonable when evidence cannot prove a unique map, but such a choice must be labelled as a hypothesis. An interactive choice is not evidence by itself.

## Read-only versus writable trust levels

The project should treat recovery states as an explicit ladder:

1. **Raw readable** — sectors can be read; no structural claims.
2. **Forensic candidate map** — one or more plausible APA graphs reconstructed; still no writes.
3. **Read-only validated map** — sufficient independent consistency to expose partitions read-only through recovery tooling.
4. **Repair candidate** — exact proposed metadata changes and backups exist.
5. **Repaired + raw revalidated** — on-disk graph passes the recovery validator.
6. **Normal ps2hdd admitted** — standard manager write paths may be enabled again.

Calling levels 2-3 "healthy" would be misleading. The useful feature is that data can remain inspectable even while the standard driver correctly refuses to trust the metadata.

## Regression and hardware gates

`make test-host` covers portable format/policy modules, transaction failure injection, rescue validation, generated raw HDD states, byte-level payload/pointer mutation, and the repair matrix. Repair tests verify postconditions, not only classification.

The R5900 release build is warning-clean with the pinned PS2DEV toolchain. Host tests cannot emulate DEV9/ATA timing, fileXio RPC/DMA behavior, cache durability, APA journaling, or an actual power failure. Physical-HDD validation remains mandatory before a newly introduced write path is considered hardware-proven.

## Commenting rule

Comments should explain ownership, safety evidence, hardware constraints, failure ordering, and why an operation is allowed or refused. They should not merely narrate C syntax. In particular, comments that say "never write sector zero" must qualify the statement as applying to **normal** Torii-compatible workflows; the guarded Michishirube master-recovery exception is part of the architecture and must never be hidden by stale documentation.
