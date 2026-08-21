# Contributing

Changes are welcome, but this program can write to a real PlayStation 2 HDD. Review standards are intentionally stricter around storage/recovery code than around text, diagnostics, or UI.

## Before opening a pull request

Run the portable tests on a normal host:

```sh
make test-host
```

Build the stripped PS2 ELF with the pinned toolchain used by CI:

```sh
docker run --rm -v "$PWD:/work" -w /work ps2dev/ps2dev:v2.0.0 \
  sh -c 'apk add --no-cache make >/dev/null && make clean && make release'
```

The released PS2DEV container intentionally keeps its runtime layer small, so the command installs `make` into the disposable container before building. Do not use a login shell (`sh -l`) here: it can replace the image-provided `PATH` that points at the R5900 toolchain.

CI performs both checks automatically.

## Storage-code rules

Normal bootstrap workflows and exceptional recovery are intentionally separate.

### Normal bootstrap writes

- Do not bypass the mandatory current-state backup.
- Do not raw-write APA header sectors 0 or 1 from normal install/restore/disable code.
- Keep payload verification before pointer activation.
- Keep pointer changes on the normal `HDIOC_SETOSDMBR` path.
- Keep full-capsule same-disk, bounds and KELF checks fail-closed.
- Do not increase raw devctl transfer sizes merely because a larger buffer appears to fit; document the PS2SDK/fileXio contract and provide hardware validation.

### Exceptional recovery writes

`hdd_repair_ps2` and `hdd_forensic_repair_ps2` are explicit recovery exceptions, not generic raw-sector services.

- Do not call them from normal workflows.
- Do not bypass the portable planner or controller authorization.
- Do not bypass verified `HDDRAW` / `HDDMETA` evidence creation.
- A truncated forensic scan must remain strictly read-only at every layer.
- Source-stability rereads immediately before write are mandatory.
- Forensic plans may not become an arbitrary-LBA/data write language.
- Preserve non-master-first / master-LBA-0-last ordering.
- Preserve flush + immediate read-back + final touched-set verification.
- Preserve mandatory restart after successful or partially failed exceptional recovery.
- Any relaxation of a write invariant requires independent evidence, a deterministic regression and explicit review.

The exceptional raw metadata paths are still marked experimental in 0.4.0. Hardware reports on sacrificial or fully imaged media are particularly valuable.

## Reporting recovery tests

For a useful hardware report include as much of the following as possible:

- console model and ROMVER;
- HDD/SSD model and capacity;
- Sony/third-party network adapter or SATA/IDE bridge;
- exact corruption introduced or failure being recovered;
- application version and build hash if known;
- `HDDMAN.LOG` and `FORENSIC.TXT`;
- relevant `HDDRAW` / `HDDMETA` / rescue artifacts;
- proposed patch diff;
- raw before/after header comparison;
- whether a restart was performed before any follow-up HDD operation.

Every reproducible hardware discrepancy should become a portable fixture/test before its fix is considered complete.

## Source style

Use English comments and diagnostics. Prefer comments that explain a PS2-specific constraint, format contract, trust boundary, or safety invariant over comments that merely restate the next line of C.

Keep portable code free of PS2-only headers when practical so it can be exercised by `make test-host`. Keep presentation, policy and device mechanics separated: UI should not invent repair policy, and raw writers should not infer what a repair should be.

0.4.x is feature-frozen except for defects and narrowly scoped validation hardening. New interoperability work belongs to the 0.5.x Kakehashi milestone described in `docs/ROADMAP.md`.
