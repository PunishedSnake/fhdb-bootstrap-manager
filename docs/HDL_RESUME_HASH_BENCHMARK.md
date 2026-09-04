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
  an uninterrupted hash and that altered checkpoint records are rejected;
- CI #699 builds matched PROFILE OFF and PROFILE ON experiment variants only
  after the frozen Phase-0 pair has passed exact SHA-256 validation;
- in CI #699 the checkpoint experiment changes only the EE application image:
  its PROFILE OFF `hdl_stream.irx` is byte-identical to the frozen PROFILE OFF
  IRX and its PROFILE ON IRX is byte-identical to the frozen PROFILE ON IRX.

**CURRENT IMPLEMENTATION**

- transaction journal interval: 16384 ISO sectors = 32 MiB;
- release-like experiment: `HDL_PROFILE=0 HDL_RESUME_HASH_CHECKPOINT=1`;
- instrumented experiment: `HDL_PROFILE=1 HDL_RESUME_HASH_CHECKPOINT=1`;
- release-like baseline: frozen `HDL_PROFILE=0` ELF;
- instrumented baseline: frozen `HDL_PROFILE=1` ELF;
- the experiment writes, reads back, codec-verifies and renames a 256-byte
  sidecar at existing 32 MiB transaction checkpoints and on an orderly cancel;
- checkpoint state is written before the corresponding transaction journal;
- the sidecar is deleted when a new zero-progress transaction supersedes it and
  when a transaction reaches COMPLETE;
- `tools/parse_hdl_perf.py` extracts checkpoint restore/fallback/write-failure
  events and transaction results from `HDDMAN.LOG`;
- `tools/resume_hash_ab_preflight.py` binds every hardware sample template to
  the exact baseline/experiment ELF and IRX identities from the CI artifact;
- `tools/compare_hdl_resume_hash_ab.py` separates safe fallback runs from valid
  optimized restores and reports p50/p95/p99/max for the supplied timing data.

**INFERENCJA**

- removing an N-byte prefix replay should save approximately N bytes of USB
  traffic on a matching COPY resume;
- restoring a full-payload checkpoint should remove one full source-ISO pass
  before resumed HDD verification;
- because checkpoint state is bound to the journal progress, loss of power after
  checkpoint replacement but before the newer journal becomes visible should
  make the checkpoint stale relative to the older journal and therefore reject
  it, falling back to source rehash;
- the small sidecar operations may add measurable latency or jitter to an
  uninterrupted install even though their byte volume is tiny.

**HIPOTEZA DO TESTU**

- recovery time improves materially on real PS2;
- the uninterrupted-copy regression from checkpoint maintenance is negligible;
- temp-file write/readback/rename plus the mass-storage stack provide adequate
  persistence behavior across the tested orderly-cancel and guarded fault
  windows on real hardware;
- no adapter/USB implementation exposes a correctness or persistence corner
  case not covered by host tests.

The sidecar is therefore logically fail-safe in current source, but this document
does **not** call it physically durable across arbitrary power loss until the
real-hardware crash-window matrix demonstrates that property. No performance or
power-loss durability claim is accepted before this hardware gate passes.

## Authoritative corpus rationale

This test follows:

- `PS2_PERFORMANCE_BIBLE.md`: remove work first; preserve ELF/map/toolchain,
  correctness hash and benchmark provenance; distinguish instrumented and
  release-like profiles;
- `PS2_HDD_APA_PFS_HDL_filesystem_optimization_research_corpus_v2.md`: keep HDL
  raw/storage workloads separate, eliminate repeated reads before attempting
  lower-level acceleration, use deterministic checksum validation, and do not
  infer throughput from interface headline rates;
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`: reuse producer
  state only when representation, lifetime and ownership remain valid for the
  consumer;
- `PS2_Whole_System_Scheduling_research_corpus_v2.md`: treat USB, IOP, SIF and
  storage as one producer/consumer path and report distribution/tail behavior,
  not only an average;
- `PS2_USB_1_1_optimization_research_corpus.md`: USB Full-Speed scheduling has
  millisecond-scale granularity, so redundant source traffic belongs on the
  critical recovery path rather than being dismissed as free background work.

## Frozen and experiment identities

The authoritative matched A/B identity was emitted by CI #699 for project head
`9d17f6805927bbe6fba8417237ff2e72869e0452` in
`RESUME_HASH_AB_IDENTITY.json`.

### PROFILE OFF: release-like pair

Baseline:

```text
PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF
bytes   632884
sha256  4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916
```

Experiment:

```text
PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_OFF.ELF
bytes   636084
sha256  d463064036b2f253462cf3fbc90b75db89da36979acfd88334b50e6eecd7ef11
```

Both use the exact same PROFILE OFF `hdl_stream.irx`:

```text
bytes   8405
sha256  f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751
```

Static checkpoint delta against frozen PROFILE OFF:

```text
stripped ELF                 +3200 B
EE named text                +2512 B  (229956 -> 232468)
EE named functions              +4    (609 -> 613)
EE instructions                +629    (57539 -> 58168)
execute_transaction()          +288 B  (6156 -> 6444)
execute_transaction insn        +72    (1540 -> 1612)
```

### PROFILE ON: instrumented pair

Baseline:

```text
PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_ON.ELF
bytes   638388
sha256  964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7
```

Experiment:

```text
PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_ON.ELF
bytes   641588
sha256  849cb5fec6cce31b214210a09d8ffe81ce181a6790f0ef5fa28bb8f62ce89d62
```

Both use the exact same PROFILE ON `hdl_stream.irx`:

```text
bytes   9861
sha256  8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db
```

Static checkpoint delta against frozen PROFILE ON:

```text
stripped ELF                 +3200 B
EE named text                +2512 B  (232780 -> 235292)
EE named functions              +4    (618 -> 622)
EE instructions                +631    (58246 -> 58877)
execute_transaction()          +288 B  (6156 -> 6444)
execute_transaction insn        +72    (1540 -> 1612)
```

The 2-instruction difference between OFF/ON checkpoint deltas is profiler-side
code generation outside `execute_transaction()`; the transaction body itself
has the same measured static delta in both pairs.

Do not rebuild and silently call a result one of these binaries. Exact hash is
the identity. If experiment source changes, CI emits a new experiment identity
while the frozen baseline hashes remain fixed.

## Why there are two A/B pairs

Use PROFILE OFF for the acceptance timing result. It is the release-like pair
and does not pay the Phase-0 HDL profiler overhead.

Use PROFILE ON for attribution. Both binaries in that pair contain the same IOP
latency/traffic profiler and the same PROFILE ON IRX, allowing the existing
`usb-direct-read`, `source-fallback-read`, `prefetch-consumer-wait`, `hdd-write`,
`hdd-read`, `sif-dma-completion`, `pump-ioctl`, `source-ioctl`, `target-ioctl`,
`copy-ee-consumer` and `verify-ee-consumer` categories to explain where time and
traffic changed.

Never compare frozen PROFILE OFF directly against checkpoint PROFILE ON and call
that the checkpoint effect. That changes two variables at once.

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
PROFILE mode
baseline ELF SHA-256
experiment ELF SHA-256
hdl_stream.irx SHA-256
source ISO SHA-256
source ISO byte count
target HDD free-space/allocation state
video mode / launch method
```

Keep the same console, adapter, USB device, ISO, target disk, video mode and
launch method for the whole A/B block.

The CI artifact contains:

```text
BENCHMARK_PROVENANCE_RESUME_HASH_PROFILE_OFF.yml
BENCHMARK_PROVENANCE_RESUME_HASH_PROFILE_ON.yml
RESUME_HASH_AB_IDENTITY.json
RESUME_HASH_AB_PROFILE_OFF_TEMPLATE.json
RESUME_HASH_AB_PROFILE_ON_TEMPLATE.json
```

Fill the hardware/runtime fields rather than inferring them from CI.

## Correctness gate before timing

For both binaries:

1. start a fresh install;
2. cancel only through the normal guarded TRIANGLE path after at least one
   transaction checkpoint;
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

Recommended order, already emitted in the CI templates:

```text
BASE, EXP, EXP, BASE, EXP, BASE, BASE, EXP
```

Measure separately where the existing logs permit:

```text
copy phase elapsed time
payload verification elapsed time
total transaction elapsed time
copy throughput
checkpoint write failures
correctness result
```

Report p50, p95, p99 and max. With only eight initial samples the tail
percentiles are coarse; treat them as a smoke gate and collect a larger sample
set if the result is near the acceptance boundary.

Do not accept an improvement in recovery if uninterrupted installs acquire a
large or erratic regression.

For release-like acceptance data, use
`RESUME_HASH_AB_PROFILE_OFF_TEMPLATE.json`. For profiler attribution, repeat the
selected workload with the PROFILE ON template rather than mixing modes in one
comparator input.

## Workload B: resumed COPY

Purpose: measure removal of the already-copied-prefix replay.

Choose three persisted resume depths separated across the ISO. Prefer journal
boundaries near approximately 25%, 50% and 75% of source progress. Record the
exact `completed_sectors` from the journal rather than assuming the requested
percentage was hit.

For each depth:

1. perform a fresh copy to the selected transaction checkpoint;
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
`tools/parse_hdl_perf.py` must report a `prefix_restores` record with that byte
count. If EXP reports a prefix fallback, classify that run separately.
`tools/compare_hdl_resume_hash_ab.py` deliberately excludes fallback runs from
the optimized timing distribution while preserving their count.

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

For a valid optimized run, `parse_hdl_perf.py` must report a `full_restores`
record whose byte count equals `source_bytes`. Full-source fallback is kept as a
correctness-safe fallback sample, not as evidence for optimized latency.

Report:

```text
resume-to-HDD-verification-start p50/p95/p99/max
full recovery p50/p95/p99/max
source ISO bytes
checkpoint restore/fallback status
correctness failures
```

## Crash-window analysis and hardware matrix

Current save order at a copy checkpoint is:

```text
update transaction.completed_sectors in RAM
encode checkpoint for that exact progress
write HDLINSTALL.SHN
read back and verify exact bytes + checkpoint codec
remove previous HDLINSTALL.SHA
rename HDLINSTALL.SHN -> HDLINSTALL.SHA
encode/write/readback/verify/replace transaction journal
```

A checkpoint can never advance journal progress. Restore validates the sidecar
against the transaction loaded from the authoritative journal, including its
completed byte count, source fingerprint and target ID.

**INFERENCJA:** if the new sidecar becomes visible but the newer journal does
not, the older journal progress makes the sidecar stale and restore is rejected.
If checkpoint replacement itself is lost/corrupt, the optimization is lost and
legacy rehash is the intended fallback.

The actual mass-storage persistence semantics across reset/power loss are still
a hardware property. Test these states before promotion:

```text
sidecar absent
sidecar checksum corrupt
sidecar from an earlier completed_sectors value
sidecar from a later completed_sectors value with older journal
sidecar from another source fingerprint
sidecar from another target ID
valid .SHA primary
valid temporary .SHN with primary absent
loss/reset around sidecar replacement where the guarded fault procedure permits
```

Every invalid/mismatched case must fall back to the legacy rehash path and still
complete correctly. It must never advance transaction progress by itself.

Use the existing guarded hardware fault-injection procedure only where its
safety preconditions are satisfied. Do not pull power during arbitrary APA
metadata writes merely to make the benchmark more exciting. The console has
suffered enough.

## Host-side workflow

After a hardware run, turn each preserved log into structured telemetry:

```sh
python3 tools/parse_hdl_perf.py HDDMAN.LOG --output run.json
```

Fill one workload/depth per copy of the CI sample template. Do not combine COPY
resume at 25%, 50% and 75% in one comparator input.

Then compare against the identity emitted with the tested artifact:

```sh
python3 tools/compare_hdl_resume_hash_ab.py \
  RESUME_HASH_AB_IDENTITY.json samples.json \
  --output result.json
```

The comparator rejects wrong ELF/IRX hashes, mixed PROFILE modes, mixed workload
IDs, mismatching correctness hashes, invalid restored-byte counts and too few
accepted EXP samples after fallback separation.

## Acceptance rule

Promote the checkpoint path only if all are true:

1. zero correctness failures across the test matrix;
2. frozen baseline identity remains unchanged;
3. matching checkpoint resumes actually skip the expected redundant source
   bytes;
4. COPY-resume and PAYLOAD_VERIFIED recovery show a clear real-hardware benefit;
5. uninterrupted-install p50/p95/p99/max do not show an unacceptable regression
   or new long-tail spikes;
6. sidecar failure always degrades to the old safe rehash behavior;
7. PROFILE ON attribution agrees with the release-like PROFILE OFF result rather
   than revealing an unrelated IOP/device change;
8. power/reset testing does not demonstrate a persistence behavior that can
   violate the transaction correctness contract.

If the result is ambiguous, keep the flag off and collect more samples. A neat
architecture diagram is not a benchmark result.
