# PS2 HDD Bootstrap Manager 0.5.0 — Kakehashi

**Kakehashi (架け橋, "bridge")** brings the first console-side HDL management
features into PS2 HDD Bootstrap Manager while keeping the recovery and safety
model established by Michishirube.

The point of 0.5.0 is not to turn the manager into another general-purpose HDD
utility. It adds a guarded bridge between removable ISO sources and the PS2's
existing internal APA/HDL storage, while preserving the diagnostic, backup and
recovery tools already present in the application.

## New: HDL Tools

0.5.0 adds a dedicated **HDL Tools** workspace.

### Installed games

- Builds the installed-game catalogue from one validated raw APA chain walk.
- Removes the old fixed 128-game limit.
- Reads HDLoader metadata lazily for the visible page instead of opening every
  game partition up front.
- Shows startup ID, allocated size and APA part count.
- Keeps malformed or unreadable metadata visible but locks destructive actions.
- Supports paged navigation on large game collections.
- Guarded deletion rechecks the live disk, journal state, APA identity,
  partition count and metadata SHA-256 before the normal APA remove call.

The catalogue path has been exercised on a real large PS2 HDD with **354 HDL
games**. Guarded deletion has also been exercised on physical hardware.

### ISO browser

- Browses ISO sources from `mass:/`.
- Removes the former fixed 64-image source limit.
- Uses paged navigation for large source sets.
- Validates ISO9660 structure and extracts PS2 disc identity from
  `SYSTEM.CNF` before any HDD mutation.
- Rejects unsupported or malformed sources instead of treating every `.iso`
  filename as trustworthy.

Real-console ISO discovery has been verified with a normal PS2 DVD image.

## Trial ISO-to-HDL installer

0.5.0 includes ISO-to-HDL installation as an **experimental / trial feature**.

The transaction is deliberately conservative:

1. validate the source image and game identity;
2. re-read and validate the live APA layout;
3. prove the generated target name is not already allocated;
4. allocate the HDL layout through the normal PS2 HDD stack;
5. stream the image in bounded blocks;
6. accumulate source SHA-256 while copying;
7. flush the payload;
8. read the HDD payload back and compare the final SHA-256;
9. commit HDLoader metadata only after payload verification;
10. flush and read the metadata back before declaring success.

Interrupted installs use an external transaction journal so incomplete work is
not silently presented as a finished game.

The current fast path keeps source bytes on the IOP while they are written to
the internal HDD, sends one copy to the EE for hashing, and can prefetch the
next 64 KiB USB block in parallel. If the second staging buffer or worker
cannot be created, it falls back to a synchronous one-buffer path.

**This release does not claim a complete install compatibility matrix yet.**
A produced installation should still be verified on the target HDD and booted
through OPL before it is treated as proven. DVD9 layer-break handling, split
FAT32 ISO sets and recursive source directories are not implemented.

## Storage target for 0.5.0

Kakehashi ships **one normal release storage variant** rather than a collection
of benchmark builds.

The authoritative target is the classic PS2 internal HDD path through the
expansion bay / official Sony Network Adapter using the established DEV9/ATA,
APA and HDL stack. The release does not include the later experimental
HDD-write/checkpoint/materialized benchmark variants developed during
performance research.

Third-party adapters, SATA conversions and bridge devices may work, but their
write behavior is not claimed as the reference configuration for 0.5.0.

## UI and usability

- New two-by-three main dashboard.
- Dedicated HDL workspace.
- Cleaner controller navigation and paged game/ISO lists.
- High-rate storage status messages are coalesced so the GS UI does not add a
  VBlank wait to every 64 KiB transfer.
- Existing native, 480p, 576p, 720p and 1080i guarded video modes remain.
- Existing MSX and Spleen bitmap-font choices remain.

## Code and performance work

Kakehashi keeps the accepted code-side optimization work without shipping the
profiling experiments themselves.

- Normal runtime code remains built with `-O2` and LTO.
- Selected cold, human-paced controllers use `-Os` to reduce R5900 I-cache
  footprint.
- Unused generic file/libc paths were removed where the application contract
  did not require them.
- Formatting paths remain integer-only where floating conversion was unused.
- Large control flows were split around actual responsibility/lifetime
  boundaries instead of mechanically inlining or unrolling them.
- The Phase-2 build reduced named EE text by roughly **4.5 KiB** and about
  **1100 instructions** versus its earlier Phase-1 baseline.

No runtime speed percentage is claimed from those static results. The release
simply keeps the smaller, cleaner code layout.

## Existing recovery features retained

Kakehashi retains the Michishirube recovery toolkit, including:

- full APA master-header validation;
- header backups and full rescue capsules;
- signed HDD bootstrap install/restore;
- boot-chain diagnostics and reports;
- degraded raw APA forensic scanning;
- forward/reverse/geometry reconstruction;
- guarded deterministic master recovery;
- guarded multi-header topology repair;
- HDDRAW/HDDMETA/FORENSIC evidence artifacts;
- source-stability checks, master-last commit ordering, flush and read-back
  verification;
- fail-closed behavior when evidence is ambiguous or a scan is truncated.

Exceptional raw metadata repair remains experimental and should still be tested
on sacrificial or fully imaged media.

## Validation status

**Validated:**

- full portable host regression suite;
- 30 deterministic mounted-HDD fixtures;
- 9 sparse forensic HDD fixtures;
- pinned PS2DEV v2.0.0 EE/IOP build;
- physical large-HDD catalogue with 354 installed games;
- physical installed-game metadata browsing;
- guarded deletion on physical hardware;
- physical USB ISO discovery;
- the existing Michishirube GS/video-mode validation.

**Still experimental / pending broader physical validation:**

- end-to-end ISO allocation + copy + metadata commit across a representative
  game set;
- produced-game OPL boot matrix;
- sustained installer throughput characterization;
- interruption / resume across real power-loss cases;
- DVD9 and split-image support;
- broad third-party adapter / SATA / bridge write matrix.

## Upgrade notes

0.5.0 does not require reformatting the HDD and does not replace the existing
APA recovery safety policy. Existing `HDDMAN.CFG` installations remain usable.

The recommended download is:

```text
PS2_HDD_BOOTSTRAP_MANAGER-0.5.0.zip
```

It contains the versioned ELF, default configuration, checksums, project
license, PS2SDK license and bundled third-party notices.
