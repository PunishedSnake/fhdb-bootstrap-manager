# Storage scratch 64-byte alignment experiment rejection

CI #743 tested removal of explicit 64-byte alignment from the two highest-
confidence file-only scratch buffers identified by `ALIGNMENT_CONTRACT_AUDIT.md`:

```text
src/header_backup.c::backup_scratch[1024]
src/repair_snapshot.c::snapshot_verify[1024]
```

## Contract

**CURRENT IMPLEMENTATION:** pinned PS2SDK ordinary fileXio read paths do not
require these application buffers to start on a 64-byte boundary. Neither object
is a direct GIF/DMAC, libpad or custom hdl0: SIF/DMA buffer.

Removing the attributes was therefore correctness-compatible at the documented
API-contract level and worth testing as a layout experiment.

## CI #743

Source point:

```text
902f873dac54813c11f6056b7f0667f2283e7bbe
```

Artifact digest:

```text
sha256:e6010f1bba48dc2e96ade33652ce2e15cefa9f6b54f246d3da89404484a84e14
```

Reference was workspace-v1 + source-fingerprint-malloc from CI #739.

### PROFILE OFF

```text
                         #739          #743          delta
ELF bytes                632756        632756          0
size text                286437        286437          0
size data                345332        345332          0
size bss                3012344       3012344          0
named text               229756        229756          0
instructions              57488         57488          0
execute_transaction        6008          6008          0
```

### PROFILE ON

```text
                         #739          #743          delta
ELF bytes                638132        638132          0
size text                290381        290381          0
size data                346804        346804          0
size bss                3012984       3012984          0
named text               232552        232552          0
instructions              58189         58189          0
execute_transaction        6008          6008          0
```

The ELF SHA changed because object/symbol placement changed, but every measured
section/code metric above remained identical. The hdl_stream IRX pair also
remained byte-identical to the frozen Phase-0 pair.

## Decision

**REJECTED AS AN OPTIMIZATION / RETAIN CURRENT ALIGNMENT.**

The current source alignment is not required by fileXio, but removing it produced
no reduction in BSS, text, final ELF size or instruction count. Keeping the
existing layout avoids an otherwise pointless address-placement change in a
recovery-oriented code path.

This result is intentionally recorded so the same source-cleanup-only change is
not later rediscovered and promoted as a performance optimization merely because
there are fewer `aligned(64)` strings in the source.
