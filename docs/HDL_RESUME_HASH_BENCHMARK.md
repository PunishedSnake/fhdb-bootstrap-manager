# HDL resume-hash checkpoint hardware benchmark

This document defines the real-PS2 gate for the optional
`HDL_RESUME_HASH_CHECKPOINT=1` experiment.

The experiment is deliberately not the default build. It exists to test the
first optimization rule from the PS2 Optimization Research Library v2: remove
work before trying to make the same work faster.

## Epistemic status

**POTWIERDZONE**

- the existing COPY resume path reconstructs the SHA-256 state by reading the
  entire already-copied ISO prefix again;
- a resumed `PAYLOAD_VERIFIED` transaction without a persisted source digest
  performs another complete source-ISO hash pass;
- the normal transaction journal remains authoritative;
- the optional checkpoint is a 256-byte authenticated sidecar bound to source
  size, completed byte count, source fingerprint and target ID;
- a missing, corrupt or mismatching sidecar falls back to the old rehash path;
- host tests prove that a restored SHA state produces the same final digest as
  an uninterrupted hash and that altered checkpoint records are rejected.

**CURRENT IMPLEMENTATION**

- transaction journal interval: 16384 ISO sectors = 32 MiB;
- experiment build: `HDL_PROFILE=0 HDL_RESUME_HASH_CHECKPOINT=1`;
- baseline performance build: frozen `HDL_PROFILE=0` ELF;
- the experiment writes/verifies/renames a 256-byte sidecar at durable copy
  checkpoints and on an orderly cancel;
- the sidecar is deleted when a new zero-progress transaction supersedes it and
  when a transaction reaches COMPLETE.

**INFERENCJA**

- removing an N-byte prefix replay should save approximately N bytes of USB
  traffic on a matching COPY resume;
- restoring a full-payload checkpoint should remove one full source-ISO pass
  before resumed HDD verification;
- the small sidecar operations may add measurable latency or jitter to an
  uninterrupted install even though their byte volume is tiny.

**HIPOTEZA DO TESTU**

- recovery time improves materially on real PS2;
- the uninterrupted-copy regression from checkpoint maintenance is negligible;
- no adapter/USB implementation exposes a correctness or persistence corner
  case not covered by host tests.

No performance claim is accepted before this hardware gate passes.

## Authoritative corpus rationale

This test follows:

- `PS2_PERFORMANCE_BIBLE.md`: remove work first; preserve ELF/map/toolchain,
  correctness hash and benchmark provenance; distinguish instrumented and
  release-like profiles;
- `PS2_HDD_APA_PFS_HDL_filesystem_optimization_research_corpus_v2.md`: keep HDL
  raw/storage workloads separate, use deterministic checksum validation, and do
  not infer throughput from interface headline rates;
- `PS2_Whole_System_Scheduling_research_corpus_v2.md`: treat USB, IOP, SIF and
  storage as one producer/consumer path and report distribution/tail behavior,
  not only an average;
- `PS2_USB_1_1_optimization_research_corpus.md`: USB Full-Speed scheduling has
  millisecond-scale granularity, so redundant source traffic belongs on the
  critical recovery path rather than being dismissed as free background work.

## Frozen baseline identity

Use the Phase-0 PROFILE OFF binary only after the preflight tool accepts it:

```text
PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF
bytes   632884
sha256  4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916
```

The matching embedded PROFILE OFF `hdl_stream.irx` is:

```text
bytes   8405
sha256  f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
```

Do not rebuild and silently call the result the same baseline. The frozen hash
is the identity.

The experiment must come from the same CI artifact as the baseline and must
have its own recorded SHA-256 and section sizes.

## Hardware record

Record before the first measured run:

```text
SCPH / hardware revision
ROMVER
HDD model or SSD model
network/HDD adapter model and revision
USB mass-storage device model
USB filesystem and relevant allocation state
PS2SDK commit
PS2DEV/toolchain version
active IRX set
baseline ELF SHA-256
experiment ELF SHA-256
source ISO SHA-256
source ISO byte count
target HDD free-space/allocation state
```

Keep the same console, adapter, USB device, ISO, target disk, video mode and
launch method for the whole A/B block.

## Correctness gate before timing

For both binaries:

1. start a fresh install;
2. cancel only through the normal guarded TRIANGLE path after at least one
   durable journal checkpoint;
3. restart and choose resume;
4. complete payload verification and metadata commit;
5. confirm the final installed game is catalogued with the expected startup and
   title;
6. preserve the transaction/session log;
7. verify the same final source/payload correctness hash in both variants.

Any checksum mismatch, invalid target metadata, unexpected cleanup behavior or
resume refusal invalidates the performance sample.

## Workload A: uninterrupted-install regression

Purpose: measure the cost paid when recovery is never needed.

Use one deterministic ISO large enough to cross many 32 MiB journal intervals.
Do not change the ISO between A and B.

For each binary, perform at least eight interleaved fresh-install runs. Restore
an equivalent target state before each run.

Recommended order:

```text
OFF, EXP, EXP, OFF, EXP, OFF, OFF, EXP
```

Measure separately where the existing logs permit:

```text
copy phase elapsed time
payload verification elapsed time
total transaction elapsed time
copy throughput
journal/checkpoint failures
correctness result
```

Report p50, p95, p99 and max. With only eight initial samples the tail
percentiles are coarse; treat them as a smoke gate and collect a larger sample
set if the result is near the acceptance boundary.

Do not accept an improvement in recovery if uninterrupted installs acquire a
large or erratic regression.

## Workload B: resumed COPY

Purpose: measure removal of the already-copied-prefix replay.

Choose three durable resume depths separated across the ISO. Prefer journal
boundaries near approximately 25%, 50% and 75% of source progress. Record the
exact `completed_sectors` from the journal rather than assuming the requested
percentage was hit.

For each depth:

1. perform a fresh copy to the selected durable checkpoint;
2. cancel normally;
3. preserve the journal and, for EXP, its checkpoint sidecar;
4. reboot/relaunch in the same way for each run;
5. start timing immediately before accepting resume;
6. stop the recovery-start metric when the first payload progress beyond the
   stored `completed_sectors` is observed;
7. continue the install to completion for correctness.

Derived redundant work for the legacy baseline is:

```text
prefix_rehash_bytes = completed_sectors * 2048
```

For a valid EXP checkpoint, expected skipped prefix bytes are the same number.
A session-log line must confirm checkpoint restore. If EXP falls back to safe
rehash, classify that run separately rather than pretending it was an optimized
sample.

Report for each depth:

```text
resume-to-first-new-progress p50/p95/p99/max
full resumed-transaction p50/p95/p99/max
completed_sectors
prefix_rehash_bytes baseline
checkpoint_restored_bytes experiment
fallback count
correctness failures
```

## Workload C: resumed PAYLOAD_VERIFIED

Purpose: measure removal of the complete extra USB source-hash pass.

Create a valid `PAYLOAD_VERIFIED` recovery point with the source ISO still
available. Preserve identical transaction state for baseline and experiment
runs as far as the format permits.

Expected work difference:

```text
baseline: one full source ISO SHA-256 pass, then HDD payload verification
EXP:      restore final source SHA state, then HDD payload verification
```

The HDD verification remains required in both variants. Do not count its time as
work removed by this experiment.

Report:

```text
resume-to-HDD-verification-start p50/p95/p99/max
full recovery p50/p95/p99/max
source ISO bytes
checkpoint restore/fallback status
correctness failures
```

## Crash-window matrix

The sidecar is not journal authority, so deliberately test these states before
promotion:

```text
sidecar absent
sidecar checksum corrupt
sidecar from an earlier completed_sectors value
sidecar from another source fingerprint
sidecar from another target ID
valid .SHA primary
valid temporary .SHN with primary absent
```

Every invalid/mismatched case must fall back to the legacy rehash path and still
complete correctly. It must never advance transaction progress by itself.

Use the existing guarded hardware fault-injection procedure only where its
safety preconditions are satisfied. Do not pull power during arbitrary APA
metadata writes merely to make the benchmark more exciting. The console has
suffered enough.

## Acceptance rule

Promote the checkpoint path only if all are true:

1. zero correctness failures across the test matrix;
2. frozen baseline identity remains unchanged;
3. matching checkpoint resumes actually skip the expected redundant source
   bytes;
4. COPY-resume and PAYLOAD_VERIFIED recovery show a clear real-hardware benefit;
5. uninterrupted-install p50/p95/p99/max do not show an unacceptable regression
   or new long-tail spikes;
6. sidecar failure always degrades to the old safe rehash behavior.

If the result is ambiguous, keep the flag off and collect more samples. A neat
architecture diagram is not a benchmark result.
