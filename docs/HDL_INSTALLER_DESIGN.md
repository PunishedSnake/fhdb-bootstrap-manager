# HDL Tools design

HDL Tools is an isolated application workspace for installing a legal backup
image from `mass:` into a standard HDLoader game partition, inspecting
existing games and deliberately removing a selected game. It is not part of
the bootstrap or APA recovery menus and it never participates in raw forensic
repair.

The workspace renders its own top-level menu before journal, USB or HDD I/O.
Install, installed-game management and incomplete-transaction handling are
separate operations, so a slow or failed probe cannot leave an unrelated
status screen visible.

## Supported source contract

- ISO9660 images with a valid primary volume descriptor.
- A root `SYSTEM.CNF;1` containing a structurally valid PS2 `BOOT2` entry.
- 2048-byte-aligned source length.
- Plain `.iso` files first. Split FAT32 image sets are a separate front-end
  extension and must present the same random-read source contract.
- CD and single-layer DVD metadata. A DVD9-sized image stays blocked until its
  layer break has been determined and verified.

ISO9660 alone cannot reliably distinguish a small DVD from a CD. Images small
enough to be either are marked ambiguous and require an explicit media choice
before installation.

## Destination contract

The destination uses APA type `0x1337`, one main partition and at most 64 sub
partitions. Allocation sizes are limited to the standard 128 MiB, 256 MiB,
512 MiB, 1 GiB, 2 GiB and 4 GiB sizes accepted by `ps2hdd`.

The IOP transport exposes only these payload regions:

- main partition from sector `0x2000`;
- each sub-partition from sector `0x0800`.

The application loads PS2SDK's POSIX APA build (`ps2hdd-bdm.irx`) after the
internal ATA block device but before USB mass storage joins BDM. This makes the
internal disk deterministically `hdd0:` and enables PS2SDK's public HDL
partition type and physical-partition query. A USB disk cannot steal the
internal HDD unit number.

The transport delegates transfer, allocation and flushing to `ps2hdd`; it
does not issue raw ATA writes and cannot address APA headers. The 1024-byte
HDLoader metadata block is a separate final operation at byte offset
`0x100000` of the main partition attribute view. The APA file view begins after
the 4 KiB partition-information area, so a raw-disk reader must fetch the same
metadata at physical sector `main + 0x808`, not `main + 0x800`. This distinction
is regression-tested because an eight-sector error makes every otherwise valid
HDL game look structurally invalid. The metadata commit operation writes,
flushes, reads back and compares the exact bytes.

## Transaction order

1. Inspect the source and calculate its fingerprint.
2. Recheck normal `ps2hdd` admission and reject APAEXT, hybrid GPT, degraded or
   forensic-only disk states.
3. Save the planned transaction record outside the target HDD.
4. Create the main partition and every required sub-partition.
5. Stream the ISO in aligned chunks while advancing the durable journal.
6. Flush, seek to the beginning and verify the installed payload.
7. Build HDLoader metadata from the physical layout returned by the IOP
   transport. ISO payload begins after the standard 4 MiB main-partition area
   and after the standard 1 MiB subpartition area.
8. Commit and read back metadata last.
9. Mark the journal complete.

Before step 8 the allocation is intentionally not a valid installed game.
Failure or cancellation before metadata commit may remove only the exact
partition confirmed by an active journal that reached `PARTITIONS_CREATED`.
A merely planned transaction never authorizes deletion. Metadata read errors
also refuse deletion instead of treating unreadable bytes as an empty game.
Existing game partitions are never renamed, deleted or reused by the initial
installation workflow.

## Existing-game management

The installed-game list is built from main APA partitions of type `0x1337`.
The catalogue walks the validated raw APA chain once, then lazily reads only the
metadata needed by the visible page. Raw metadata access uses physical sector
`main + 0x808`, matching OPL's 4 KiB partition-information area plus the 1 MiB
extended-attribute offset. Sub-partitions contribute to the displayed allocation
size and are compared with the metadata partition count. The 1024-byte metadata
block is parsed only when it has the HDLoader magic, a bounded non-empty title
and startup name, a supported PS2 media type and a plausible partition count.
Unreadable or structurally invalid metadata remains visible for inspection,
but deletion stays locked.

Deleting a valid game is intentionally separate from cleaning up an
incomplete install. Before showing the confirmation screen, and again after
the user holds `L1+R1+SQUARE`, the manager:

1. rechecks normal `ps2hdd` disk admission;
2. refuses an exact target owned by an incomplete transaction;
3. re-reads the partition stat and requires a main APA type `0x1337` entry;
4. re-reads and parses the complete metadata block;
5. compares its SHA-256 with the snapshot selected from the freshly scanned
   list.

Only then is the named main partition passed to the normal APA remove call,
which also removes its registered sub-partitions. A corrupt transaction journal
or any identity change fails closed. There is no generic partition delete and
no raw APA deletion path.

## Resume and failure policy

The fixed 512-byte journal is SHA-256 protected and records source identity,
target identity, partition count, stage and completed ISO sectors. Stage and
progress are monotonic. A resume is allowed only when the source fingerprint,
target layout and journal all agree.

Ambiguous media type, a missing layer break, source mutation, layout mismatch,
short read, short write, failed flush, failed verification or a corrupt journal
all stop the transaction. None may be converted into a warning prompt.

HDL installer status and failure pages are rendered directly through the GS
message UI rather than the legacy incremental console. USB enumeration and ISO
probe failures therefore retain their actual diagnostic text and raw code
instead of degrading into a bare `Press X to return` screen.

## Licensing boundary

The implementation is original project code built against the documented
PS2SDK APA interfaces and the public HDLoader on-disk metadata layout. No source
from GPL HDL utilities or from projects without a clear reusable source license
is copied into this repository.
