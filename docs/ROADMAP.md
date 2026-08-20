# Roadmap and release codenames

Release codenames use Japanese words connected with thresholds, passage, bridges, and wayfinding. The theme fits a tool whose entire job is deciding what the PS2 crosses into after ROM boot. Semantic versions remain the authoritative machine-readable release identifiers.

## Release line

| Version | Codename | Meaning | Milestone |
|---|---|---|---|
| `0.1.x` | **Kagi** (鍵) | key | Emergency APA pointer backup/disable/restore and the first hardware recovery. |
| `0.2.0` | **Mon** (門) | gate | General HDD bootstrap management, selectable storage, MagicGate signing, and guarded installation. |
| `0.3.0` | **Torii** (鳥居) | gateway | Stable full rescue capsules, payload restoration, boot-chain inspection, persistent diagnostics, CI, and targeted R5900 hashing optimizations. |
| `0.4.x` | **Michishirube** (道標) | signpost | Internal modularization and stronger diagnostic/test infrastructure. |
| `0.5.x` | **Kakehashi** (架け橋) | bridge | Broader interoperability and portable tooling around rescue/inspection data. |
| `1.0.0` | **Kaidō** (街道) | main road | Frozen rescue-format contract, reproducible releases, and broad real-hardware validation. |

## 0.4.x — Michishirube

Planned engineering work:

- split `src/main.c` along its existing logical boundaries (`platform`, `storage`, `apa`, `boot_chain`, `rescue`, `ui`) without changing write ordering;
- replace project-specific magic negative result numbers with documented enums/domains where PS2SDK errors are not being forwarded directly;
- expand host tests for configuration parsing, same-disk header matching, KELF length validation, and boot-chain classification using synthetic fixtures;
- add build-size/performance reporting so optimization work is measurable rather than flag-driven;
- establish a hardware validation matrix across FAT console revisions, storage adapters/HDDs, memory-card layouts, and launch devices;
- make classification evidence more data-driven so supporting another known environment does not require threading another special case through the UI.

## 0.5.x — Kakehashi

Candidate work after the internal split is proven:

- a small host-side rescue-capsule inspector/extractor that verifies `HDDRESCUE*.BIN` without a PS2;
- import/export conveniences for reports and rescue metadata without weakening same-disk restore checks;
- richer compatibility evidence for FHDB, OSDMenu, PSBBN, HDD-OSD/HOSDMenu, and custom chains;
- optional machine-readable diagnostic output alongside the human-readable report.

## 1.0.0 — Kaidō

1.0 is not a feature-count target. It requires a stable rescue-format contract, reproducible tagged builds, no known safety-critical write-path defects, documented interrupted-operation recovery behavior, and enough independent hardware validation that the project is no longer relying on one console as its entire quality-assurance department.
