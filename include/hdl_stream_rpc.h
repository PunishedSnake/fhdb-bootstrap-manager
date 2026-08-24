#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_STREAM_RPC_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_STREAM_RPC_H

#include <stdint.h>

#define HDL_STREAM_MAX_PARTITIONS 65u
#define HDL_STREAM_METADATA_SIZE 1024u
#define HDL_STREAM_IOP_STAGE_BYTES (64u * 1024u)

#define HDL_STREAM_IOCTL2_GET_LAYOUT 0x6D01
#define HDL_STREAM_IOCTL2_FLUSH 0x6D02
#define HDL_STREAM_IOCTL2_COMMIT_METADATA 0x6D03
#define HDL_STREAM_IOCTL2_READ_METADATA 0x6D04

/*
 * High-throughput payload path used by HDL Tools.
 *
 * The ordinary fileXio copy path reads mass: into EE RAM and then sends the
 * same bytes back over SIF to hdl0:. The commands below keep the payload in a
 * 64-byte-aligned IOP staging buffer, optionally read the USB file directly
 * through its BDM fragment map, write that staging buffer straight to ps2hdd,
 * and DMA one copy to EE only when the R5900 needs the bytes for SHA-256.
 */
#define HDL_STREAM_IOCTL2_SOURCE_TO_EE 0x6D05
#define HDL_STREAM_IOCTL2_PUMP_TO_EE 0x6D06
#define HDL_STREAM_IOCTL2_TARGET_TO_EE 0x6D07

typedef struct {
    uint32_t count;
    uint32_t starts[HDL_STREAM_MAX_PARTITIONS];
    uint32_t lengths[HDL_STREAM_MAX_PARTITIONS];
} hdl_stream_layout_t;

typedef struct {
    int32_t source_fd;
    uint32_t ee_address;
    uint32_t bytes;
    uint32_t source_offset_low;
    uint32_t source_offset_high;
} hdl_stream_source_io_t;

typedef struct {
    uint32_t ee_address;
    uint32_t bytes;
} hdl_stream_target_io_t;

/*
 * Keep a tuned stock fileXio fallback for small probes and for hardware that
 * rejects the fast path. The optimized copy itself no longer sends payload
 * back from EE to IOP, so this is deliberately only a fallback helper and not
 * a global macro around every fileXioRead/fileXioWrite call.
 */
#ifdef _EE
#define HDL_STREAM_FILEXIO_RWBUF_SIZE (64 * 1024)
#define HDL_STREAM_FILEXIO_RWBUF_FALLBACK (16 * 1024)

extern int fileXioSetRWBufferSize(int size);

static inline void hdl_stream_tune_filexio_once(void)
{
    static int attempted;

    if (attempted)
        return;
    attempted = 1;
    if (fileXioSetRWBufferSize(HDL_STREAM_FILEXIO_RWBUF_SIZE) < 0)
        (void)fileXioSetRWBufferSize(HDL_STREAM_FILEXIO_RWBUF_FALLBACK);
}
#endif

#endif
