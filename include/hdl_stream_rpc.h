#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_STREAM_RPC_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_STREAM_RPC_H

#include <stdint.h>

#define HDL_STREAM_MAX_PARTITIONS 65u
#define HDL_STREAM_METADATA_SIZE 1024u

#define HDL_STREAM_IOCTL2_GET_LAYOUT 0x6D01
#define HDL_STREAM_IOCTL2_FLUSH 0x6D02
#define HDL_STREAM_IOCTL2_COMMIT_METADATA 0x6D03
#define HDL_STREAM_IOCTL2_READ_METADATA 0x6D04

typedef struct {
    uint32_t count;
    uint32_t starts[HDL_STREAM_MAX_PARTITIONS];
    uint32_t lengths[HDL_STREAM_MAX_PARTITIONS];
} hdl_stream_layout_t;

/*
 * fileXio's IOP server defaults to a 16 KiB staging buffer. HDL Tools moves
 * payloads in 64 KiB EE chunks, so the stock setting silently splits every
 * fileXioRead/fileXioWrite into four IOP reads/writes plus four SIF DMA/get-data
 * operations. On real hardware that showed up as roughly 0.6 MiB/s USB->HDD.
 *
 * This header is shared with the IOP driver, therefore keep the tuning strictly
 * EE-side. The first sizeable HDL transfer grows fileXio's global staging
 * buffer to 64 KiB so one application chunk maps to one server-side chunk.
 * If the larger IOP allocation ever fails, immediately restore the stock
 * 16 KiB buffer instead of leaving fileXio with a NULL rwbuf.
 */
#ifdef _EE
#define HDL_STREAM_FILEXIO_RWBUF_SIZE (64 * 1024)
#define HDL_STREAM_FILEXIO_RWBUF_FALLBACK (16 * 1024)

extern int fileXioRead(int fd, void *buf, int size);
extern int fileXioWrite(int fd, const void *buf, int size);
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

static inline int hdl_stream_fileXioRead(int fd, void *buf, int size)
{
    if (size >= HDL_STREAM_FILEXIO_RWBUF_FALLBACK)
        hdl_stream_tune_filexio_once();
    return (fileXioRead)(fd, buf, size);
}

static inline int hdl_stream_fileXioWrite(int fd, const void *buf, int size)
{
    if (size >= HDL_STREAM_FILEXIO_RWBUF_FALLBACK)
        hdl_stream_tune_filexio_once();
    return (fileXioWrite)(fd, buf, size);
}

#define fileXioRead(fd, buf, size) hdl_stream_fileXioRead((fd), (buf), (size))
#define fileXioWrite(fd, buf, size) hdl_stream_fileXioWrite((fd), (buf), (size))
#endif

#endif
