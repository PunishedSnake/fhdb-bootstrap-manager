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

The `mass:/` browser no longer has a fixed ISO-count ceiling. It grows its
catalogue dynamically and displays eight entries at a time. `UP/DOWN` changes
the selected image and `LEFT/RIGHT` changes page, so page controls never consume
menu rows or collapse the GS list below a readable text height.

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
`0x100000` of the main partition attribute area. That operation writes,
flushes, reads back and compares the exact bytes.

Normal write admission now validates the live APA linked list directly. The
raw read-only pass verifies header magic, checksum, self LBA, bounds, `prev`
links and a bounded sub-partition count. It also calculates free sectors from
APA free nodes instead of depending on a signed `HDIOC_FREESECTOR` return. The
32-bit `HDIOC_TOTALSECTOR` value is treated as an unsigned sector-count bit
pattern after ordinary errno-range failures are rejected, which keeps healthy
2 TB disks representable without weakening the APA checks.

## Transaction order

1. Inspect the source and calculate its quick identity fingerprint.
2. Recheck normal `ps2hdd` admission plus the live raw APA chain and reject
   APAEXT, hybrid GPT, degraded or structurally inconsistent disk states.
3. Save the planned transaction record outside the target HDD.
4. Create the main partition and every required sub-partition.
5. Stream the ISO in aligned chunks while advancing the durable journal.
6. Flush, seek to the beginning and verify the complete installed payload
   against the complete current source image, including independent SHA-256
   digests.
7. Build HDLoader metadata from the physical layout returned by the IOP
   transport. ISO payload begins after the standard 4 MiB main-partition area
   and after the standard 1 MiB subpartition area.
8. Commit and read back metadata last.
9. Mark the journal complete.

Before an initial run or resume starts writing, the source is opened at the
recorded size, the quick fingerprint is rechecked, and the ISO is probed again.
Its PS2 startup identity must still match the journal. The final full
source/target verification remains authoritative, so a partial fingerprint is
never accepted as proof that copied bytes are correct.

Before step 8 the allocation is intentionally not a valid installed game.
Failure or cancellation before metadata commit may remove only the exact
partition confirmed by an active journal that reached `PARTITIONS_CREATED`.
A merely planned transaction never authorizes deletion. Metadata read errors
also refuse deletion instead of treating unreadable bytes as an empty game.
Existing game partitions are never renamed, deleted or reused by the initial
installation workflow.

## Existing-game management

The installed-game catalogue no longer enumerates `hdd0:` through hundreds of
`fileXioDread` entries and then opens every game partition individually. It
walks the normal APA linked list once using two-sector raw reads and records
only main type-`0x1337` partitions. The catalogue is dynamically allocated, so
the former 128-game truncation does not exist.

Each game snapshot stores its main LBA, APA identifier, allocation derived from
the main header and its sub table, and the expected APA partition count.
HDLoader metadata is read directly from `main_lba + 0x800` only for the eight
games on the currently visible page. This keeps initial listing cost close to
the number of APA headers rather than the number of headers plus one
open/seek/read/close transaction for every installed game.

The list uses eight visible rows with `UP/DOWN` selection and `LEFT/RIGHT` page
navigation. Valid metadata supplies the game title and startup ID; malformed or
unreadable metadata falls back to the APA partition identifier and remains
read-only. A global main/sub count mismatch is surfaced in the status line
instead of silently presenting the catalogue as healthy.

Deleting a valid game is intentionally separate from cleaning up an
incomplete install. Before showing the confirmation screen, and again after
the user holds `L1+R1+SQUARE`, the manager:

1. rechecks normal disk admission and the live APA chain;
2. refuses an exact target owned by an incomplete transaction;
3. re-reads the partition stat and requires a main APA type `0x1337` entry;
4. re-reads and parses the complete 1024-byte metadata block from its physical
   main-partition location;
5. requires the metadata partition count to agree with the APA snapshot;
6. compares the complete metadata SHA-256 with the page snapshot selected by
   the user.

Only then is the named main partition passed to the normal APA remove call,
which also removes its registered sub-partitions. A corrupt transaction journal,
APA-chain change, metadata change or partition-count disagreement fails closed.
There is no generic partition delete and no raw APA deletion path.

## Resume and failure policy

The fixed 512-byte journal is SHA-256 protected and records source identity,
target identity, partition count, stage and completed ISO sectors. Stage and
progress are monotonic. A resume is allowed only when the source fingerprint,
PS2 startup identity, target layout and journal agree.

Ambiguous media type, a missing layer break, source mutation detected by the
identity checks or full verification, layout mismatch, short read, short write,
failed flush, failed verification, an invalid APA chain or a corrupt journal
all stop the transaction. None may be converted into a warning prompt.

## Remaining source limitations

The current front end still accepts only ordinary root-level `.iso` files.
That means FAT32's single-file size limit remains a practical blocker for large
DVD images until split-image support is implemented. DVD9 remains intentionally
blocked as well. These are explicit next features rather than cases the current
installer should attempt to guess around.

The complete verification pass intentionally rereads the USB source after the
copy. It is conservative but expensive on PS2 USB. A future optimization may
carry a source SHA-256 through the copy and verify only the HDD read-back, but
that change must preserve crash/resume semantics before replacing the current
byte-for-byte path.

## Licensing boundary

The implementation is original project code built against the documented
PS2SDK APA interfaces and the public HDLoader on-disk metadata layout. No source
from GPL HDL utilities or from projects without a clear reusable source license
is copied into this repository.
