# HDL game installer design

The HDL installer is an isolated application feature for installing a legal
backup image from `mass:` into a standard HDLoader game partition. It is not
part of the bootstrap or APA recovery menus and it never participates in raw
forensic repair.

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
- each sub-partition from sector `4`.

It delegates transfer, allocation and flushing to `ps2hdd`; it does not issue
raw ATA writes and cannot address APA headers. The 1024-byte HDLoader metadata
block is a separate final operation at byte offset `0x100000` of the main
partition attribute area. That operation writes, flushes, reads back and
compares the exact bytes.

## Transaction order

1. Inspect the source and calculate its fingerprint.
2. Recheck normal `ps2hdd` admission and reject APAEXT, hybrid GPT, degraded or
   forensic-only disk states.
3. Save the planned transaction record outside the target HDD.
4. Create the main partition and every required sub-partition.
5. Stream the ISO in aligned chunks while advancing the durable journal.
6. Flush, seek to the beginning and verify the installed payload.
7. Build HDLoader metadata from the physical layout returned by the IOP
   transport.
8. Commit and read back metadata last.
9. Mark the journal complete.

Before step 8 the allocation is intentionally not a valid installed game.
Failure or cancellation before metadata commit may remove only the exact
partition created by the active journal. Existing game partitions are never
renamed, deleted or reused by the initial implementation.

## Resume and failure policy

The fixed 512-byte journal is SHA-256 protected and records source identity,
target identity, partition count, stage and completed ISO sectors. Stage and
progress are monotonic. A resume is allowed only when the source fingerprint,
target layout and journal all agree.

Ambiguous media type, a missing layer break, source mutation, layout mismatch,
short read, short write, failed flush, failed verification or a corrupt journal
all stop the transaction. None may be converted into a warning prompt.

## Licensing boundary

The implementation is original project code built against the documented
PS2SDK APA interfaces and the public HDLoader on-disk metadata layout. No source
from GPL HDL utilities or from projects without a clear reusable source license
is copied into this repository.
