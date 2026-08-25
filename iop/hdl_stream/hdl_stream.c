/*
 * Sequential HDL payload transport for the IOP.
 *
 * This driver deliberately delegates disk writes to ps2hdd. Besides the
 * ordinary hdl0: stream it owns a high-throughput staging path for USB installs:
 * source bytes stay on the IOP, are written directly to the HDL target and are
 * DMAed to EE only once for R5900-side SHA-256. The fast path also pipelines the
 * next 64 KiB USB read on a worker thread while the current block is written to
 * DEV9/SIF, removing the old mass: -> IOP -> EE -> IOP -> HDD bounce and most
 * of the idle gap between consecutive USB requests.
 */

#include <bdm.h>
#include <errno.h>
#include <hdd-ioctl.h>
#include <iomanX.h>
#include <irx.h>
#include <loadcore.h>
#include <sifman.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/fcntl.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>
#include <thsemap.h>
#include <usbhdfsd-common.h>

#include "hdl_stream_rpc.h"

#define HDL_STREAM_MAIN_SKIP 0x2000u
#define HDL_STREAM_SUB_SKIP 0x0800u
#define HDL_STREAM_METADATA_OFFSET 0x100000
#define HDL_STREAM_DEVICE "hdl"
#define HDL_STREAM_BDM_MAX_DEVICES 20u
#define HDL_STREAM_MAX_SOURCE_FRAGMENTS 4096u
#define HDL_STREAM_USB_SECTOR_SIZE 512u
#define HDL_STREAM_USB_SECTOR_SHIFT 9u
#define HDL_STREAM_PREFETCH_STACK 0x1000u
#define HDL_STREAM_PREFETCH_PRIORITY 0x30u

IRX_ID("hdl_stream", 1, 4);

typedef struct {
    int source_fd;
    int disabled;
    struct block_device *bd;
    bd_fragment_t *fragments;
    uint32_t fragment_count;
    uint32_t cursor_fragment;
    uint64_t cursor_base;
} hdl_source_map_t;

typedef struct {
    int hdd_fd;
    int writable;
    uint32_t count;
    uint32_t lengths[HDL_STREAM_MAX_PARTITIONS];
    uint32_t starts[HDL_STREAM_MAX_PARTITIONS];
    uint64_t capacity_bytes;
    uint64_t position;

    void *stage_allocation;
    unsigned char *stage[2];
    uint32_t stage_count;

    int prefetch_thread;
    int prefetch_request_sema;
    int prefetch_done_sema;
    int prefetch_stopped_sema;
    volatile int prefetch_stop;
    volatile int prefetch_active;
    int prefetch_result;
    int prefetch_source_fd;
    uint64_t prefetch_offset;
    uint32_t prefetch_bytes;
    uint32_t prefetch_stage_index;

    hdl_source_map_t source;
    hdl_stream_fast_stats_t stats;
} hdl_stream_file_t;

static int stream_init(iomanX_iop_device_t *device)
{
    (void)device;
    return 0;
}

static int stream_deinit(iomanX_iop_device_t *device)
{
    (void)device;
    return 0;
}

static int make_hdd_path(const char *name, char destination[38])
{
    static const char prefix[] = "hdd0:";
    size_t length;

    if (name == NULL)
        return -EINVAL;
    while (*name == '/' || *name == '\\')
        name++;
    length = strlen(name);
    if (length == 0 || length > 32)
        return -ENAMETOOLONG;
    if (memchr(name, ':', length) != NULL || memchr(name, ',', length) != NULL)
        return -EINVAL;
    memcpy(destination, prefix, sizeof(prefix) - 1);
    memcpy(destination + sizeof(prefix) - 1, name, length);
    destination[sizeof(prefix) - 1 + length] = '\0';
    return 0;
}

static int ioctl_u32_result(int raw, uint32_t *value)
{
    if (value == NULL)
        return -EINVAL;
    if (raw < 0 && raw > -4096)
        return raw;
    *value = (uint32_t)raw;
    return 0;
}

static void source_map_free_fragments(hdl_source_map_t *source)
{
    if (source->fragments != NULL)
        FreeSysMemory(source->fragments);
    source->fragments = NULL;
    source->fragment_count = 0;
    source->cursor_fragment = 0;
    source->cursor_base = 0;
    source->bd = NULL;
}

static void source_map_reset(hdl_stream_file_t *stream)
{
    source_map_free_fragments(&stream->source);
    stream->source.source_fd = -1;
    stream->source.disabled = 0;
}

static void source_map_disable(hdl_stream_file_t *stream, int source_fd)
{
    source_map_free_fragments(&stream->source);
    stream->source.source_fd = source_fd;
    stream->source.disabled = 1;
    stream->stats.flags &= ~HDL_STREAM_FAST_FLAG_DIRECT_BDM;
}

static int source_map_prepare(hdl_stream_file_t *stream, int source_fd)
{
    struct block_device *devices[HDL_STREAM_BDM_MAX_DEVICES];
    bd_fragment_t *fragments;
    char driver_name[16];
    uint32_t device_number;
    int fragment_count;
    int result;
    unsigned int i;

    if (stream->source.source_fd == source_fd) {
        if (stream->source.disabled)
            return -ENOTSUP;
        if (stream->source.bd != NULL && stream->source.fragments != NULL)
            return 0;
    }

    source_map_reset(stream);
    fragment_count = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_FRAGLIST,
                                   NULL, 0, NULL, 0);
    if (fragment_count <= 0 ||
        (uint32_t)fragment_count > HDL_STREAM_MAX_SOURCE_FRAGMENTS) {
        source_map_disable(stream, source_fd);
        return -ENOTSUP;
    }

    fragments = AllocSysMemory(ALLOC_FIRST,
                               (unsigned int)fragment_count * sizeof(*fragments),
                               NULL);
    if (fragments == NULL)
        return -ENOMEM;
    result = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_FRAGLIST,
                           NULL, 0, fragments,
                           (unsigned int)fragment_count * sizeof(*fragments));
    if (result != fragment_count) {
        FreeSysMemory(fragments);
        source_map_disable(stream, source_fd);
        return -ENOTSUP;
    }

    memset(driver_name, 0, sizeof(driver_name));
    result = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_DEVICE_NUMBER,
                           NULL, 0, &device_number, sizeof(device_number));
    if (result < 0) {
        FreeSysMemory(fragments);
        source_map_disable(stream, source_fd);
        return -ENOTSUP;
    }
    result = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_DRIVERNAME,
                           NULL, 0, driver_name, sizeof(driver_name));
    if (result < 0 || driver_name[0] == '\0') {
        FreeSysMemory(fragments);
        source_map_disable(stream, source_fd);
        return -ENOTSUP;
    }

    memset(devices, 0, sizeof(devices));
    bdm_get_bd(devices, HDL_STREAM_BDM_MAX_DEVICES);
    for (i = 0; i < HDL_STREAM_BDM_MAX_DEVICES; i++) {
        struct block_device *bd = devices[i];

        if (bd != NULL && bd->parNr == 0 && bd->devNr == device_number &&
            bd->name != NULL && strcmp(bd->name, driver_name) == 0 &&
            bd->read != NULL && bd->sectorSize == HDL_STREAM_USB_SECTOR_SIZE) {
            stream->source.source_fd = source_fd;
            stream->source.disabled = 0;
            stream->source.bd = bd;
            stream->source.fragments = fragments;
            stream->source.fragment_count = (uint32_t)fragment_count;
            stream->source.cursor_fragment = 0;
            stream->source.cursor_base = 0;
            stream->stats.flags |= HDL_STREAM_FAST_FLAG_DIRECT_BDM;
            stream->stats.fragment_count = (uint32_t)fragment_count;
            return 0;
        }
    }

    FreeSysMemory(fragments);
    source_map_disable(stream, source_fd);
    return -ENOTSUP;
}

/*
 * Locate the fragment that contains a logical source sector. ISO reads are
 * overwhelmingly sequential, so keep the last fragment and its logical base
 * instead of re-scanning a potentially thousands-entry FAT fragment list for
 * every 64 KiB block. A backwards seek resets the cursor and still behaves
 * correctly for resume/hash passes.
 */
static int source_map_locate(hdl_source_map_t *source, uint64_t logical,
                             uint32_t *index, uint64_t *base)
{
    uint32_t i = source->cursor_fragment;
    uint64_t current = source->cursor_base;

    if (i >= source->fragment_count || logical < current) {
        i = 0;
        current = 0;
    }

    while (i < source->fragment_count) {
        uint64_t end = current + source->fragments[i].count;

        if (logical < end) {
            source->cursor_fragment = i;
            source->cursor_base = current;
            *index = i;
            *base = current;
            return 0;
        }
        current = end;
        i++;
    }
    return -EIO;
}

static int source_map_read(hdl_source_map_t *source, uint64_t sector,
                           void *buffer, uint16_t count)
{
    uint64_t logical = sector;
    uint16_t left = count;
    unsigned char *cursor = buffer;

    while (left != 0) {
        uint64_t base;
        uint64_t end;
        uint32_t index;
        bd_fragment_t *fragment;
        uint16_t chunk;

        if (source_map_locate(source, logical, &index, &base) < 0)
            return -EIO;
        fragment = &source->fragments[index];
        end = base + fragment->count;

        chunk = left;
        if ((uint64_t)chunk > end - logical)
            chunk = (uint16_t)(end - logical);
        if (source->bd->read(source->bd,
                             fragment->sector + (logical - base),
                             cursor, chunk) != chunk)
            return -EIO;
        logical += chunk;
        left -= chunk;
        cursor += (unsigned int)chunk * HDL_STREAM_USB_SECTOR_SIZE;

        if (logical == end && index + 1u < source->fragment_count) {
            source->cursor_fragment = index + 1u;
            source->cursor_base = end;
        }
    }
    return count;
}

static int source_read_fallback(int source_fd, uint64_t offset,
                                void *buffer, unsigned int bytes)
{
    unsigned int complete = 0;
    s64 position = iomanX_lseek64(source_fd, (s64)offset, FIO_SEEK_SET);

    if (position != (s64)offset)
        return position < 0 ? (int)position : -EIO;
    while (complete < bytes) {
        int result = iomanX_read(source_fd,
                                 (unsigned char *)buffer + complete,
                                 bytes - complete);

        if (result <= 0)
            return result < 0 ? result : -EIO;
        complete += (unsigned int)result;
    }
    return (int)bytes;
}

static int source_read_at(hdl_stream_file_t *stream, int source_fd,
                          uint64_t offset, void *buffer, unsigned int bytes)
{
    /*
     * The stock usbmass BDM intentionally caps one SCSI request at 128 512-byte
     * sectors (64 KiB), and HDL_STREAM_IOP_STAGE_BYTES matches that exactly.
     * Requiring 512-byte BDM sectors lets the IOP use shifts/masks here instead
     * of libgcc 64-bit divide/mod helpers on the 37.5 MHz R3000A. Devices with
     * another logical sector size simply use the safe iomanX fallback.
     */
    if (source_map_prepare(stream, source_fd) == 0 &&
        (offset & (HDL_STREAM_USB_SECTOR_SIZE - 1u)) == 0 &&
        (bytes & (HDL_STREAM_USB_SECTOR_SIZE - 1u)) == 0 &&
        (bytes >> HDL_STREAM_USB_SECTOR_SHIFT) <= UINT16_MAX) {
        int result = source_map_read(
            &stream->source, offset >> HDL_STREAM_USB_SECTOR_SHIFT, buffer,
            (uint16_t)(bytes >> HDL_STREAM_USB_SECTOR_SHIFT));

        if (result >= 0) {
            stream->stats.direct_reads++;
            return (int)bytes;
        }
        source_map_disable(stream, source_fd);
    }

    stream->stats.fallback_reads++;
    return source_read_fallback(source_fd, offset, buffer, bytes);
}

static void prefetch_worker(void *arg)
{
    hdl_stream_file_t *stream = arg;

    for (;;) {
        WaitSema(stream->prefetch_request_sema);
        if (stream->prefetch_stop)
            break;
        stream->prefetch_result = source_read_at(
            stream, stream->prefetch_source_fd, stream->prefetch_offset,
            stream->stage[stream->prefetch_stage_index],
            stream->prefetch_bytes);
        SignalSema(stream->prefetch_done_sema);
    }

    SignalSema(stream->prefetch_stopped_sema);
    ExitThread();
}

static void prefetch_delete_semas(hdl_stream_file_t *stream)
{
    if (stream->prefetch_request_sema >= 0)
        DeleteSema(stream->prefetch_request_sema);
    if (stream->prefetch_done_sema >= 0)
        DeleteSema(stream->prefetch_done_sema);
    if (stream->prefetch_stopped_sema >= 0)
        DeleteSema(stream->prefetch_stopped_sema);
    stream->prefetch_request_sema = -1;
    stream->prefetch_done_sema = -1;
    stream->prefetch_stopped_sema = -1;
}

static int prefetch_init(hdl_stream_file_t *stream)
{
    iop_sema_t sema;
    iop_thread_t thread;
    int result;

    if (stream->stage_count < 2)
        return -ENOMEM;

    stream->prefetch_thread = -1;
    stream->prefetch_request_sema = -1;
    stream->prefetch_done_sema = -1;
    stream->prefetch_stopped_sema = -1;
    stream->prefetch_stop = 0;
    stream->prefetch_active = 0;

    memset(&sema, 0, sizeof(sema));
    sema.initial = 0;
    sema.max = 1;
    stream->prefetch_request_sema = CreateSema(&sema);
    if (stream->prefetch_request_sema < 0)
        goto fail;
    stream->prefetch_done_sema = CreateSema(&sema);
    if (stream->prefetch_done_sema < 0)
        goto fail;
    stream->prefetch_stopped_sema = CreateSema(&sema);
    if (stream->prefetch_stopped_sema < 0)
        goto fail;

    memset(&thread, 0, sizeof(thread));
    thread.attr = TH_C;
    thread.thread = prefetch_worker;
    thread.priority = HDL_STREAM_PREFETCH_PRIORITY;
    thread.stacksize = HDL_STREAM_PREFETCH_STACK;
    result = CreateThread(&thread);
    if (result < 0)
        goto fail;
    stream->prefetch_thread = result;
    result = StartThread(stream->prefetch_thread, stream);
    if (result < 0) {
        DeleteThread(stream->prefetch_thread);
        stream->prefetch_thread = -1;
        goto fail;
    }

    stream->stats.flags |= HDL_STREAM_FAST_FLAG_DOUBLE_BUFFER;
    return 0;

fail:
    prefetch_delete_semas(stream);
    return -ENOMEM;
}

static int prefetch_wait(hdl_stream_file_t *stream)
{
    int result;

    if (!stream->prefetch_active)
        return 0;
    result = WaitSema(stream->prefetch_done_sema);
    if (result < 0)
        return result;
    stream->prefetch_active = 0;
    return stream->prefetch_result;
}

static int prefetch_take(hdl_stream_file_t *stream, int source_fd,
                         uint64_t offset, uint32_t bytes,
                         unsigned char **stage, uint32_t *stage_index)
{
    int matches;
    int result;

    if (!stream->prefetch_active)
        return 0;
    matches = stream->prefetch_source_fd == source_fd &&
              stream->prefetch_offset == offset &&
              stream->prefetch_bytes == bytes;
    result = prefetch_wait(stream);
    if (!matches) {
        stream->stats.prefetch_misses++;
        return 0;
    }
    if (result != (int)bytes)
        return result < 0 ? result : -EIO;
    stream->stats.prefetch_hits++;
    *stage_index = stream->prefetch_stage_index;
    *stage = stream->stage[*stage_index];
    return 1;
}

static void prefetch_schedule(hdl_stream_file_t *stream, int source_fd,
                              uint64_t offset, uint32_t bytes,
                              uint32_t stage_index)
{
    if (stream->prefetch_thread < 0 || stream->prefetch_active || bytes == 0 ||
        stage_index >= stream->stage_count)
        return;

    stream->prefetch_source_fd = source_fd;
    stream->prefetch_offset = offset;
    stream->prefetch_bytes = bytes;
    stream->prefetch_stage_index = stage_index;
    stream->prefetch_result = -EIO;
    stream->prefetch_active = 1;
    if (SignalSema(stream->prefetch_request_sema) < 0)
        stream->prefetch_active = 0;
}

static void prefetch_shutdown(hdl_stream_file_t *stream)
{
    if (stream->prefetch_thread < 0) {
        prefetch_delete_semas(stream);
        return;
    }

    if (stream->prefetch_active)
        (void)prefetch_wait(stream);
    stream->prefetch_stop = 1;
    if (SignalSema(stream->prefetch_request_sema) >= 0) {
        WaitSema(stream->prefetch_stopped_sema);
        DeleteThread(stream->prefetch_thread);
    }
    stream->prefetch_thread = -1;
    prefetch_delete_semas(stream);
}

static int dma_to_ee(uint32_t ee_address, const void *source,
                     unsigned int bytes)
{
    SifDmaTransfer_t transfer;
    int id;

    if (ee_address == 0 || bytes == 0 || (ee_address & 0x3fu) != 0 ||
        (bytes & 0x3fu) != 0)
        return -EINVAL;
    transfer.src = (void *)source;
    transfer.dest = (void *)(uintptr_t)ee_address;
    transfer.size = (int)bytes;
    transfer.attr = 0;
    id = sceSifSetDma(&transfer, 1);
    if (id <= 0)
        return -EIO;
    while (sceSifDmaStat(id) >= 0) {}
    return 0;
}

static int stream_open(iomanX_iop_file_t *file, const char *name,
                       int flags, int mode)
{
    hdl_stream_file_t *stream;
    uintptr_t aligned;
    char path[38];
    int sub_count;
    unsigned int i;
    int result;

    (void)mode;
    result = make_hdd_path(name, path);
    if (result < 0)
        return result;
    stream = AllocSysMemory(ALLOC_FIRST, sizeof(*stream), NULL);
    if (stream == NULL)
        return -ENOMEM;
    memset(stream, 0, sizeof(*stream));
    stream->source.source_fd = -1;
    stream->prefetch_thread = -1;
    stream->prefetch_request_sema = -1;
    stream->prefetch_done_sema = -1;
    stream->prefetch_stopped_sema = -1;

    stream->stage_allocation = AllocSysMemory(
        ALLOC_FIRST, (HDL_STREAM_IOP_STAGE_BYTES * 2u) + 63u, NULL);
    if (stream->stage_allocation != NULL) {
        stream->stage_count = 2;
    } else {
        stream->stage_allocation = AllocSysMemory(
            ALLOC_FIRST, HDL_STREAM_IOP_STAGE_BYTES + 63u, NULL);
        stream->stage_count = stream->stage_allocation != NULL ? 1u : 0u;
    }
    if (stream->stage_allocation == NULL) {
        FreeSysMemory(stream);
        return -ENOMEM;
    }
    aligned = ((uintptr_t)stream->stage_allocation + 63u) & ~(uintptr_t)63u;
    stream->stage[0] = (unsigned char *)aligned;
    if (stream->stage_count == 2)
        stream->stage[1] = stream->stage[0] + HDL_STREAM_IOP_STAGE_BYTES;

    stream->hdd_fd = iomanX_open(path, flags, 0);
    if (stream->hdd_fd < 0) {
        result = stream->hdd_fd;
        FreeSysMemory(stream->stage_allocation);
        FreeSysMemory(stream);
        return result;
    }
    stream->writable = (flags & FIO_O_WRONLY) != 0;
    sub_count = iomanX_ioctl2(stream->hdd_fd, HIOCNSUB,
                              NULL, 0, NULL, 0);
    if (sub_count < 0 || sub_count >= (int)HDL_STREAM_MAX_PARTITIONS) {
        result = sub_count < 0 ? sub_count : -EFBIG;
        iomanX_close(stream->hdd_fd);
        FreeSysMemory(stream->stage_allocation);
        FreeSysMemory(stream);
        return result;
    }
    stream->count = (uint32_t)sub_count + 1;
    for (i = 0; i < stream->count; i++) {
        uint32_t index = i;
        uint32_t skip = i == 0 ? HDL_STREAM_MAIN_SKIP : HDL_STREAM_SUB_SKIP;
        uint32_t length;
        uint32_t start;
        int raw_length = iomanX_ioctl2(stream->hdd_fd, HIOCGETSIZE,
                                       &index, sizeof(index), NULL, 0);
        int raw_start = iomanX_ioctl2(stream->hdd_fd, HIOCGETPARTSTART,
                                      &index, sizeof(index), NULL, 0);

        result = ioctl_u32_result(raw_length, &length);
        if (result < 0) {
            iomanX_close(stream->hdd_fd);
            FreeSysMemory(stream->stage_allocation);
            FreeSysMemory(stream);
            return result;
        }
        result = ioctl_u32_result(raw_start, &start);
        if (result < 0 || length <= skip) {
            if (result >= 0)
                result = -EINVAL;
            iomanX_close(stream->hdd_fd);
            FreeSysMemory(stream->stage_allocation);
            FreeSysMemory(stream);
            return result;
        }
        stream->lengths[i] = length;
        stream->starts[i] = start;
        stream->capacity_bytes += ((uint64_t)length - skip) * 512u;
    }

    /* Prefetch is an optimization, never an admission requirement. A low-memory
     * IOP still gets the synchronous direct-BDM fast path with one staging block. */
    if (stream->stage_count == 2)
        (void)prefetch_init(stream);
    file->privdata = stream;
    return 0;
}

static int stream_close(iomanX_iop_file_t *file)
{
    hdl_stream_file_t *stream = file->privdata;
    int result;

    if (stream == NULL)
        return -EBADF;
    prefetch_shutdown(stream);
    source_map_reset(stream);
    result = iomanX_close(stream->hdd_fd);
    FreeSysMemory(stream->stage_allocation);
    FreeSysMemory(stream);
    file->privdata = NULL;
    return result;
}

static int locate_position(const hdl_stream_file_t *stream, uint64_t position,
                           uint32_t *part, uint32_t *sector,
                           uint64_t *bytes_available)
{
    unsigned int i;

    for (i = 0; i < stream->count; i++) {
        uint32_t skip = i == 0 ? HDL_STREAM_MAIN_SKIP : HDL_STREAM_SUB_SKIP;
        uint64_t capacity = ((uint64_t)stream->lengths[i] - skip) * 512u;

        if (position < capacity) {
            *part = i;
            *sector = skip + (uint32_t)(position >> 9);
            *bytes_available = capacity - position;
            return 0;
        }
        position -= capacity;
    }
    return -ENXIO;
}

static int stream_transfer(iomanX_iop_file_t *file, void *buffer,
                           int size, uint32_t direction)
{
    hdl_stream_file_t *stream = file->privdata;
    unsigned char *cursor = buffer;
    int remaining = size;

    if (stream == NULL)
        return -EBADF;
    if (size < 0 || (size & 0x1ff) != 0 ||
        (stream->position & 0x1ff) != 0)
        return -EINVAL;
    if (direction == APA_IO_MODE_WRITE && !stream->writable)
        return -EACCES;
    if ((uint64_t)size > stream->capacity_bytes - stream->position)
        return -ENXIO;

    while (remaining > 0) {
        hddIoctl2Transfer_t transfer;
        uint64_t available;
        uint32_t part;
        uint32_t sector;
        int chunk;
        int result;

        result = locate_position(stream, stream->position,
                                 &part, &sector, &available);
        if (result < 0)
            return result;
        chunk = remaining;
        if ((uint64_t)chunk > available)
            chunk = (int)available;
        transfer.sub = part;
        transfer.sector = sector;
        transfer.size = (uint32_t)chunk >> 9;
        transfer.mode = direction;
        transfer.buffer = cursor;
        result = iomanX_ioctl2(stream->hdd_fd, HIOCTRANSFER,
                               &transfer, sizeof(transfer), NULL, 0);
        if (result < 0)
            return result;
        stream->position += (uint32_t)chunk;
        cursor += chunk;
        remaining -= chunk;
    }
    return size;
}

static int stream_read(iomanX_iop_file_t *file, void *buffer, int size)
{
    return stream_transfer(file, buffer, size, APA_IO_MODE_READ);
}

static int stream_write(iomanX_iop_file_t *file, void *buffer, int size)
{
    return stream_transfer(file, buffer, size, APA_IO_MODE_WRITE);
}

static s64 stream_seek64(iomanX_iop_file_t *file, s64 offset, int whence)
{
    hdl_stream_file_t *stream = file->privdata;
    uint64_t position;
    uint64_t magnitude;

    if (stream == NULL)
        return -EBADF;
    if (whence == FIO_SEEK_SET) {
        if (offset < 0)
            return -EINVAL;
        position = (uint64_t)offset;
    } else if (whence == FIO_SEEK_CUR) {
        if (offset < 0) {
            magnitude = (uint64_t)(-(offset + 1)) + 1;
            if (magnitude > stream->position)
                return -EINVAL;
            position = stream->position - magnitude;
        } else {
            if ((uint64_t)offset > stream->capacity_bytes - stream->position)
                return -EINVAL;
            position = stream->position + (uint64_t)offset;
        }
    } else if (whence == FIO_SEEK_END) {
        if (offset > 0)
            return -EINVAL;
        magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : 0;
        if (magnitude > stream->capacity_bytes)
            return -EINVAL;
        position = stream->capacity_bytes - magnitude;
    } else {
        return -EINVAL;
    }
    if (position > stream->capacity_bytes || (position & 0x1ff) != 0)
        return -EINVAL;
    stream->position = position;
    return (s64)position;
}

static int stream_seek(iomanX_iop_file_t *file, int offset, int whence)
{
    s64 result = stream_seek64(file, offset, whence);

    if (result > INT32_MAX)
        return -EOVERFLOW;
    return (int)result;
}

static int read_metadata(hdl_stream_file_t *stream, void *metadata,
                         unsigned int size)
{
    int result;

    if (metadata == NULL || size != HDL_STREAM_METADATA_SIZE)
        return -EINVAL;
    result = iomanX_lseek(stream->hdd_fd, HDL_STREAM_METADATA_OFFSET,
                          FIO_SEEK_SET);
    if (result != HDL_STREAM_METADATA_OFFSET)
        return result < 0 ? result : -EIO;
    result = iomanX_read(stream->hdd_fd, metadata, HDL_STREAM_METADATA_SIZE);
    if (result != HDL_STREAM_METADATA_SIZE)
        return result < 0 ? result : -EIO;
    return 0;
}

static int commit_metadata(hdl_stream_file_t *stream, const void *metadata,
                           unsigned int size)
{
    unsigned char *verify;
    int result;

    if (!stream->writable)
        return -EACCES;
    if (size != HDL_STREAM_METADATA_SIZE)
        return -EINVAL;
    verify = AllocSysMemory(ALLOC_FIRST, HDL_STREAM_METADATA_SIZE, NULL);
    if (verify == NULL)
        return -ENOMEM;
    result = iomanX_lseek(stream->hdd_fd, HDL_STREAM_METADATA_OFFSET,
                          FIO_SEEK_SET);
    if (result == HDL_STREAM_METADATA_OFFSET)
        result = iomanX_write(stream->hdd_fd, (void *)metadata,
                              HDL_STREAM_METADATA_SIZE);
    if (result == HDL_STREAM_METADATA_SIZE)
        result = iomanX_ioctl2(stream->hdd_fd, HIOCFLUSH,
                               NULL, 0, NULL, 0);
    if (result >= 0)
        result = read_metadata(stream, verify, HDL_STREAM_METADATA_SIZE);
    if (result == 0)
        result = memcmp(metadata, verify, HDL_STREAM_METADATA_SIZE) == 0 ?
                 0 : -EIO;
    FreeSysMemory(verify);
    return result;
}

static int fast_source_to_ee(iomanX_iop_file_t *file,
                             const hdl_stream_source_io_t *request,
                             unsigned int request_size, int pump)
{
    hdl_stream_file_t *stream = file->privdata;
    unsigned char *stage;
    uint64_t offset;
    uint64_t total;
    uint64_t next_offset;
    uint32_t stage_index = 0;
    int prefetched;
    int result;

    if (stream == NULL || request == NULL ||
        request_size != sizeof(*request) || request->source_fd < 0 ||
        request->bytes == 0 || request->bytes > HDL_STREAM_IOP_STAGE_BYTES ||
        (request->bytes & 0x1ffu) != 0)
        return -EINVAL;
    offset = (uint64_t)request->source_offset_low |
             ((uint64_t)request->source_offset_high << 32);
    total = (uint64_t)request->source_total_low |
            ((uint64_t)request->source_total_high << 32);
    if (total == 0 || offset > total || request->bytes > total - offset)
        return -EINVAL;

    stage = stream->stage[0];
    prefetched = prefetch_take(stream, request->source_fd, offset,
                               request->bytes, &stage, &stage_index);
    if (prefetched < 0)
        return prefetched;
    if (prefetched == 0) {
        result = source_read_at(stream, request->source_fd, offset,
                                stage, request->bytes);
        if (result != (int)request->bytes)
            return result < 0 ? result : -EIO;
    }

    /* Start the next USB request before touching DEV9 or SIF with this block.
     * While this thread waits for HDD DMA and the EE hashes the current block,
     * the worker can keep the USB bulk endpoint occupied using the other IOP
     * staging buffer. This is the key throughput optimization, not larger
     * individual USB requests. */
    next_offset = offset + request->bytes;
    if (stream->prefetch_thread >= 0 && next_offset < total) {
        uint64_t remaining = total - next_offset;
        uint32_t next_bytes = remaining > HDL_STREAM_IOP_STAGE_BYTES ?
                              HDL_STREAM_IOP_STAGE_BYTES : (uint32_t)remaining;
        uint32_t next_stage = stage_index ^ 1u;

        if ((next_bytes & 0x1ffu) == 0)
            prefetch_schedule(stream, request->source_fd, next_offset,
                              next_bytes, next_stage);
    }

    if (pump) {
        result = stream_transfer(file, stage, request->bytes,
                                 APA_IO_MODE_WRITE);
        if (result != (int)request->bytes)
            return result < 0 ? result : -EIO;
        stream->stats.pumped_chunks++;
        stream->stats.pumped_sectors += request->bytes >> 9;
    }
    result = dma_to_ee(request->ee_address, stage, request->bytes);
    if (result < 0)
        return result;
    stream->stats.source_dma_chunks++;
    return (int)request->bytes;
}

static int fast_target_to_ee(iomanX_iop_file_t *file,
                             const hdl_stream_target_io_t *request,
                             unsigned int request_size)
{
    hdl_stream_file_t *stream = file->privdata;
    int result;

    if (stream == NULL || request == NULL ||
        request_size != sizeof(*request) || request->bytes == 0 ||
        request->bytes > HDL_STREAM_IOP_STAGE_BYTES ||
        (request->bytes & 0x1ffu) != 0)
        return -EINVAL;
    result = stream_transfer(file, stream->stage[0], request->bytes,
                             APA_IO_MODE_READ);
    if (result != (int)request->bytes)
        return result < 0 ? result : -EIO;
    result = dma_to_ee(request->ee_address, stream->stage[0], request->bytes);
    if (result < 0)
        return result;
    stream->stats.target_dma_chunks++;
    return (int)request->bytes;
}

static int stream_ioctl2(iomanX_iop_file_t *file, int command,
                         void *argument, unsigned int argument_length,
                         void *buffer, unsigned int buffer_length)
{
    hdl_stream_file_t *stream = file->privdata;

    if (stream == NULL)
        return -EBADF;
    if (command == HDL_STREAM_IOCTL2_GET_LAYOUT) {
        hdl_stream_layout_t *layout = buffer;

        if (layout == NULL || buffer_length < sizeof(*layout))
            return -EINVAL;
        memset(layout, 0, sizeof(*layout));
        layout->count = stream->count;
        memcpy(layout->starts, stream->starts,
               stream->count * sizeof(stream->starts[0]));
        memcpy(layout->lengths, stream->lengths,
               stream->count * sizeof(stream->lengths[0]));
        return 0;
    }
    if (command == HDL_STREAM_IOCTL2_FLUSH)
        return iomanX_ioctl2(stream->hdd_fd, HIOCFLUSH,
                             NULL, 0, NULL, 0);
    if (command == HDL_STREAM_IOCTL2_COMMIT_METADATA)
        return commit_metadata(stream, argument, argument_length);
    if (command == HDL_STREAM_IOCTL2_READ_METADATA) {
        if (buffer == NULL || buffer_length < HDL_STREAM_METADATA_SIZE)
            return -EINVAL;
        return read_metadata(stream, buffer, HDL_STREAM_METADATA_SIZE);
    }
    if (command == HDL_STREAM_IOCTL2_SOURCE_TO_EE)
        return fast_source_to_ee(file, argument, argument_length, 0);
    if (command == HDL_STREAM_IOCTL2_PUMP_TO_EE)
        return fast_source_to_ee(file, argument, argument_length, 1);
    if (command == HDL_STREAM_IOCTL2_TARGET_TO_EE)
        return fast_target_to_ee(file, argument, argument_length);
    if (command == HDL_STREAM_IOCTL2_GET_FAST_STATS) {
        if (buffer == NULL || buffer_length < sizeof(stream->stats))
            return -EINVAL;
        memcpy(buffer, &stream->stats, sizeof(stream->stats));
        return 0;
    }
    return -EINVAL;
}

IOMANX_RETURN_VALUE_IMPL(EPERM);

static iomanX_iop_device_ops_t stream_ops = {
    .init = &stream_init,
    .deinit = &stream_deinit,
    .format = IOMANX_RETURN_VALUE(EPERM),
    .open = &stream_open,
    .close = &stream_close,
    .read = &stream_read,
    .write = &stream_write,
    .lseek = &stream_seek,
    .ioctl = IOMANX_RETURN_VALUE(EPERM),
    .remove = IOMANX_RETURN_VALUE(EPERM),
    .mkdir = IOMANX_RETURN_VALUE(EPERM),
    .rmdir = IOMANX_RETURN_VALUE(EPERM),
    .dopen = IOMANX_RETURN_VALUE(EPERM),
    .dclose = IOMANX_RETURN_VALUE(EPERM),
    .dread = IOMANX_RETURN_VALUE(EPERM),
    .getstat = IOMANX_RETURN_VALUE(EPERM),
    .chstat = IOMANX_RETURN_VALUE(EPERM),
    .rename = IOMANX_RETURN_VALUE(EPERM),
    .chdir = IOMANX_RETURN_VALUE(EPERM),
    .sync = IOMANX_RETURN_VALUE(EPERM),
    .mount = IOMANX_RETURN_VALUE(EPERM),
    .umount = IOMANX_RETURN_VALUE(EPERM),
    .lseek64 = &stream_seek64,
    .devctl = IOMANX_RETURN_VALUE(EPERM),
    .symlink = IOMANX_RETURN_VALUE(EPERM),
    .readlink = IOMANX_RETURN_VALUE(EPERM),
    .ioctl2 = &stream_ioctl2
};

static iomanX_iop_device_t stream_device = {
    HDL_STREAM_DEVICE,
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "Guarded HDLoader payload stream",
    &stream_ops
};

int _start(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    iomanX_DelDrv(HDL_STREAM_DEVICE);
    return iomanX_AddDrv(&stream_device) == 0 ?
           MODULE_RESIDENT_END : MODULE_NO_RESIDENT_END;
}
