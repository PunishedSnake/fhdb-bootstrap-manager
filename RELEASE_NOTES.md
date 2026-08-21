# PS2 HDD Bootstrap Manager 0.3.1 — Torii

A small compatibility update, exactly the sort of patch release that should stay small.

## What changed

- `MBR.XIN` is now preferred when the 0.3.x installer path opens its MBR payload.
- `MBR.XLF` remains accepted as a compatibility fallback for existing community installer layouts.
- If both files exist, `MBR.XIN` wins. If that preferred file exists but cannot be opened or fails the existing KELF checks, the manager does not silently hide it behind `MBR.XLF`.
- Once `MBR.XIN` is selected, the source path used by subsequent installation diagnostics is updated to the actual filename.

The compatibility layer is deliberately narrow: it interposes the existing `fileXioOpen()` call only for a root-level `MBR.XLF` source path. There is no rescue-capsule format change, no APA write-path change, no signing change, and no change to payload verification or pointer-last activation.

## Why XIN?

`MBR.XIN` is used by established PS2 HDD tooling for the installable MBR KELF. `MBR.XLF` remains useful as a compatibility spelling because existing community installer layouts already use it. The bytes still go through the same structural KELF validation and MagicGate signing path; 0.3.1 is correcting source-file handling, not inventing another payload format.

## Credit

Thanks to **Berion** on PSX-Place for pointing out the `MBR.XIN` naming detail and the historical `MBR.XLF` convention. Tiny semantic details are exactly the sort of thing that become permanent folklore if nobody bothers to correct them.

## Release assets

The GitHub release publishes:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.3.1.ELF
SHA256SUMS.txt
```

Both are generated from the final CI/release build.
