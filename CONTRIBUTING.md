# Contributing

Changes are welcome, but this program can write to a real PlayStation 2 HDD. Review standards are intentionally stricter around storage code than around text, diagnostics, or UI.

## Before opening a pull request

Run the portable tests on a normal host:

```sh
make test-host
```

Build the stripped PS2 ELF with the pinned toolchain used by CI:

```sh
docker run --rm -v "$PWD:/work" -w /work ps2dev/ps2dev:v2.0.0 \
  sh -lc 'make clean && make release'
```

CI performs both checks automatically.

## Storage-code rules

- Do not bypass the mandatory current-state backup.
- Do not raw-write APA header sectors 0 or 1.
- Keep payload verification before pointer activation.
- Keep full-capsule same-disk and bounds checks fail-closed.
- Do not increase raw devctl transfer sizes just because a larger buffer appears to fit; document the PS2SDK/fileXio limit and provide hardware validation.
- A performance change needs a correctness test first and measurement second.

## Source style

Use English comments and diagnostics. Prefer comments that explain a PS2-specific constraint, format contract, or safety invariant over comments that merely restate the next line of C. Keep portable code free of PS2-only headers when practical so it can be exercised by `make test-host`.

For 0.3.x, avoid opportunistic restructuring of the HDD write path. Large module boundaries are tracked in `docs/ROADMAP.md` for Michishirube so refactors can be reviewed separately from release fixes.
