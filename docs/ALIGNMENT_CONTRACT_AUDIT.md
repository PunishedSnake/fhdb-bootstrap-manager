# Explicit alignment contract audit

This document classifies every current `__attribute__((aligned(64)))` in the
project before Phase-5 alignment cleanup. It deliberately does not equate
"64-byte aligned" with "faster". Each site must identify the actual consumer
that requires an alignment domain.

Heap `memalign(64, ...)` calls are audited separately because their ownership and
DMA/cache contracts differ from static/stack object placement.

## Source-of-truth routing

- `PS2_Memory_Allocators_optimization_research_corpus_v2.md`: allocator ABI,
  cache-line and device/DMA alignment are separate contracts; avoid blanket
  over-alignment;
- `PS2_Data_Oriented_Design_optimization_research_corpus_v2.md`: representation
  and final-consumer contract decide layout;
- `PS2_IOP_SIF_optimization_research_corpus_v2.md`: SIF/DMA buffers need explicit
  ownership/cache/alignment contracts rather than generic alignment folklore;
- pinned PS2SDK `b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b`: current fileXio/libpad/draw
  implementation used to decide API requirements;
- project CI #712 source audit: 25 explicit `aligned(64)` sites.

## Current PS2SDK findings

### fileXio devctl/ioctl2 does not require a 64-byte-aligned caller buffer

Pinned EE `fileXioDevctl()`/`fileXioIoctl2()`:

- copies input `arg` bytes into the library-owned RPC packet with `memcpy`;
- stores the caller output pointer only as the final destination;
- uses a library-owned return packet for the IOP->EE DMA;
- its EE callback then copies the returned bytes into the caller buffer with
  ordinary `memcpy`.

Pinned IOP fileXio server:

- passes its own `rwbuf` payload to `iomanX_devctl()`/`iomanX_ioctl2()`;
- DMA-transfers the complete library-owned `fxio_ctl_return_pkt` to EE internal
  callback storage, not directly into the application's destination.

The server `rwbuf` itself is allocated with ordinary `AllocSysMemory()`.

**CURRENT IMPLEMENTATION:** 64-byte alignment of application input/output
objects is not a fileXio devctl/ioctl2 API requirement.

### fileXio read/write explicitly supports unaligned caller buffers

Pinned `fileXioRead()` performs cache maintenance and uses an internal callback
packet for unaligned edge bytes. Pinned `fileXioWrite()` explicitly calculates a
leading non-64-byte fragment and copies up to 64 bytes into
`fxio_write_packet.unalignedData[]` before the bulk transfer.

**CURRENT IMPLEMENTATION:** ordinary fileXio read/write callers are not required
to provide a 64-byte-aligned pointer.

This does not mean every application buffer may be arbitrarily aligned. A buffer
can still have another consumer such as GIF DMAC or libpad.

## Sites with demonstrated alignment contracts

### `src/platform.c`: `pad_buffer[256]`

```text
current alignment: 64 B
consumer: padPortOpen()
status: KEEP 64 B
```

Pinned `libpad.h` explicitly states that the new-libpad pad area must be a
256-byte region at a 64-byte-aligned address. This is a real API/device contract,
not an optimization preference.

Classification: **POTWIERDZONE / API-DMA alignment**.

### `src/gs_ui_ps2.c`: `font_atlas[]`

```text
current alignment: 64 B
consumer: draw_texture_transfer() -> DMATAG_REF -> GIF DMAC
status: KEEP aligned, 64 B itself not demonstrated
minimum candidate: 16 B qword/DMAC alignment
```

Pinned `draw_texture_transfer()` places the source address directly into
`DMATAG_REF` blocks. The atlas is therefore a direct DMA source and must obey the
DMA packet/source contract. The project also calls `FlushCache(0)` after building
the atlas, so the current code does not depend on 64-byte start alignment for a
selective cache-line flush.

Classification: **POTWIERDZONE direct DMA source; INFERENCJA that 16 B is the
sufficient project alignment pending a small A/B/correctness build.**

Do not remove alignment entirely. A future cleanup may evaluate 64 -> 16, not
64 -> arbitrary.

## Sites where 64 B is not supported by the current consumer contract

The following objects are either:

1. fileXio devctl/ioctl2 input/output;
2. ordinary fileXio read/write buffers; or
3. CPU-only scratch after such a transfer.

Pinned fileXio already supports non-64-byte caller addresses for those paths.
No other direct DMA consumer was found for these objects.

### Raw HDD transport / repair

| File | Object/site | Current consumer | 64-B status |
| --- | --- | --- | --- |
| `src/hdd_read.c` | `read_transfer_buffer` | `fileXioDevctl(HDIOC_READSECTOR)` output | not required by fileXio |
| `src/hdd_write.c` | `write_packet` | `fileXioDevctl(HDIOC_WRITESECTOR)` input arg | not required by fileXio |
| `src/hdd_write.c` | `sector_verify_buffer` | raw-read devctl output + CPU compare | not required by fileXio |
| `src/hdd_write.c` | `header_verify_buffer` | raw-read devctl output + CPU parse | not required by fileXio |
| `src/hdd_repair_ps2.c` | `repair_packet` | write devctl input arg | not required by fileXio |
| `src/hdd_repair_ps2.c` | `repair_verify` | raw-read devctl output + compare | not required by fileXio |
| `src/hdd_recovery_wrap.c` | `recovery_header` | raw-read devctl output + CPU recovery | not required by fileXio |
| `src/hdd_forensic_repair_ps2.c` | `write_packet` | write devctl input arg | not required by fileXio |
| `src/hdd_forensic_repair_ps2.c` | `source_verify` | raw-read devctl output + compare | not required by fileXio |
| `src/hdd_forensic_repair_ps2.c` | `write_verify` | raw-read devctl output + compare | not required by fileXio |
| `src/hdd_forensic_repair_ps2.c` | `repaired_header` | CPU-built header, then copied into devctl arg | no DMA consumer |
| `src/main.c` | `header_buffer` | raw-read devctl output + CPU parse | not required by fileXio |

Classification: **CURRENT IMPLEMENTATION supports ordinary alignment; candidate
cleanup after frozen hardware experiment.**

### HDL installer/catalogue

| File | Object/site | Current consumer | 64-B status |
| --- | --- | --- | --- |
| `src/hdl_installer_ps2.c` | `hdl_zero_metadata` | ordinary `fileXioWrite` | not required by fileXio |
| `src/hdl_installer_ps2.c` | local `verify[4]` | ordinary `fileXioRead` | not required by fileXio |
| `src/hdl_installer_ps2.c` | final `actual[1024]` | `fileXioIoctl2(READ_METADATA)` output | not required by fileXio |
| `src/hdl_tools/source_ui.inc` | `admission_header` | raw-read devctl output + APA parse | not required by fileXio |
| `src/hdl_tools/source_ui_resume_hash.inc` | `admission_header` | experiment equivalent of above | not required by fileXio |
| `src/hdl_tools/catalog.inc` | local APA `header[1024]` | raw-read devctl output + parse | not required by fileXio |
| `src/hdl_tools/catalog.inc` | local `metadata[1024]` | raw-read devctl output + parse/hash | not required by fileXio |
| `src/hdl_tools/transaction.inc` | snapshot `metadata[1024]` | raw-read devctl output + parse/hash | not required by fileXio |
| `src/hdl_tools/transaction_resume_hash.inc` | snapshot `metadata[1024]` | experiment equivalent of above | not required by fileXio |

The normal and resume-hash `.inc` files are alternate build fragments, not two
simultaneously linked copies. Any cleanup must update both variants so an
experiment does not silently regain a stale layout assumption.

Classification: **CURRENT IMPLEMENTATION supports ordinary alignment; candidate
cleanup after frozen hardware experiment.**

### Storage snapshot/backup scratch

| File | Object/site | Current consumer | 64-B status |
| --- | --- | --- | --- |
| `src/header_backup.c` | `backup_scratch[1024]` | `read_exact_file` + CPU validation | not required by fileXio |
| `src/repair_snapshot.c` | `snapshot_verify[1024]` | `read_exact_file` + `memcmp` | not required by fileXio |

These are the cleanest first A/B candidates because they do not cross raw-HDD
or custom-stream APIs at all.

Classification: **CURRENT IMPLEMENTATION supports ordinary alignment; highest
confidence cleanup candidates.**

## Count summary

Current explicit attributes: 25.

```text
KEEP exact 64 B API contract
  1  pad_buffer

KEEP aligned but evaluate 64 -> 16
  1  font_atlas direct GIF-DMA source

64 B not demonstrated by current consumer
  23 fileXio/raw-HDD/CPU scratch sites
```

This summary is not permission for one global search/replace. Object placement,
stack frames and LTO layout can change even when the API contract permits lower
alignment. Cleanup is therefore staged and measured.

## Cleanup sequence

1. Keep the CI #706 runtime experiment binary identity frozen for its hardware
   gate. Do not mutate that binary merely to make this table prettier.
2. First post-gate A/B: remove 64-B alignment only from
   `backup_scratch`/`snapshot_verify` and compare final ELF/map/BSS/symbol layout.
3. If useful or neutral with correctness preserved, clean the raw-HDD/fileXio
   scratch group as one reviewed transport-contract change.
4. Separately test `font_atlas` at 16-B alignment with GS texture-upload and all
   supported video-mode regressions.
5. Never change `pad_buffer` below 64 B while using the current libpad contract.
6. Audit `memalign(64)` calls independently before touching heap alignment.

## Acceptance record for any alignment cleanup

Record:

```yaml
object:
producer:
consumer:
old_alignment:
new_alignment:
alignment_domain:
api_or_hardware_contract:
elf_delta_bytes:
text_delta_bytes:
bss_delta_bytes:
stack_frame_delta_if_applicable:
correctness_test:
real_hardware_required:
```

Removing an unnecessary alignment is an optimization only if it reduces memory,
stack/layout pressure or another measured cost. A source diff containing fewer
`aligned(64)` strings is not itself a performance result.
