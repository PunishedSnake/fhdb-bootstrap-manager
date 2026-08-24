/*
 * Sequential HDL payload transport for the IOP.
 *
 * This driver deliberately delegates disk writes to ps2hdd. Besides the
 * ordinary hdl0: stream it now owns a small high-throughput staging path for
 * USB installs: source bytes can stay on the IOP, be written directly to the
 * HDL target and be DMAed to EE only once for R5900-side SHA-256. This avoids
 * the old mass: -> IOP -> EE -> IOP -> HDD bounce for every payload block.
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
#include <usbhdfsd-common.h>

#include "hdl_stream_rpc.h"

#define HDL_STREAM_MAIN_SKIP 0x2000u
#define HDL_STREAM_SUB_SKIP 0x0800u
#define HDL_STREAM_METADATA_OFFSET 0x100000
#define HDL_STREAM_DEVICE "hdl"
#define HDL_STREAM_BDM_MAX_DEVICES 20u
#define HDL_STREAM_MAX_SOURCE_FRAGMENTS 4096u

IRX_ID("hdl_stream", 1, 2);

typedef struct {
    int source_fd;
    struct block_device *bd;
    bd_fragment_t *fragments;
    uint32_t fragment_count;
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
    unsigned char *stage;
    hdl_source_map_t source;
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

static void source_map_reset(hdl_stream_file_t *stream)
{
    if (stream->source.fragments != NULL)
        FreeSysMemory(stream->source.fragments);
    stream->source.fragments = NULL;
    stream->source.fragment_count = 0;
    stream->source.bd = NULL;
    stream->source.source_fd = -1;
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

    if (stream->source.source_fd == source_fd && stream->source.bd != NULL &&
        stream->source.fragments != NULL)
        return 0;

    source_map_reset(stream);
    fragment_count = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_FRAGLIST,
                                   NULL, 0, NULL, 0);
    if (fragment_count <= 0 ||
        (uint32_t)fragment_count > HDL_STREAM_MAX_SOURCE_FRAGMENTS)
        return -ENOTSUP;

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
        return -ENOTSUP;
    }

    memset(driver_name, 0, sizeof(driver_name));
    result = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_DEVICE_NUMBER,
                           NULL, 0, &device_number, sizeof(device_number));
    if (result < 0) {
        FreeSysMemory(fragments);
        return -ENOTSUP;
    }
    result = iomanX_ioctl2(source_fd, USBMASS_IOCTL_GET_DRIVERNAME,
                           NULL, 0, driver_name, sizeof(driver_name));
    if (result < 0 || driver_name[0] == '\0') {
        FreeSysMemory(fragments);
        return -ENOTSUP;
    }

    memset(devices, 0, sizeof(devices));
    bdm_get_bd(devices, HDL_STREAM_BDM_MAX_DEVICES);
    for (i = 0; i < HDL_STREAM_BDM_MAX_DEVICES; i++) {
        struct block_device *bd = devices[i];

        if (bd != NULL && bd->parNr == 0 && bd->devNr == device_number &&
            bd->name != NULL && strcmp(bd->name, driver_name) == 0 &&
            bd->read != NULL && bd->sectorSize != 0) {
            stream->source.source_fd = source_fd;
            stream->source.bd = bd;
            stream->source.fragments = fragments;
            stream->source.fragment_count = (uint32_t)fragment_count;
            return 0;
        }
    }

    FreeSysMemory(fragments);
    return -ENOTSUP;
}

static int source_map_read(const hdl_source_map_t *source, uint64_t sector,
                           void *buffer, uint16_t count)
{
    uint64_t logical = sector;
    uint16_t left = count;
    unsigned char *cursor = buffer;

    while (left != 0) {
        uint64_t base = 0;
        bd_fragment_t *fragment = NULL;
        uint16_t chunk;
        unsigned int i;

        for (i = 0; i < source->fragment_count; i++) {
            bd_fragment_t *candidate = &source->fragments[i];

            if (logical >= base && logical < base + candidate->count) {
                fragment = candidate;
                break;
            }
            base += candidate->count;
        }
        if (fragment == NULL)
            return -EIO;

        chunk = left;
        if ((uint64_t)chunk > base + fragment->count - logical)
            chunk = (uint16_t)(base + fragment->count - logical);
        if (source->bd->read(source->bd,
                             fragment->sector + (logical - base),
                             cursor, chunk) != chunk)
            return -EIO;
        logical += chunk;
        left -= chunk;
        cursor += (unsigned int)chunk * source->bd->sectorSize;
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
    if (source_map_prepare(stream, source_fd) == 0) {
        uint32_t sector_size = stream->source.bd->sectorSize;

        if (sector_size != 0 && offset % sector_size == 0 &&
            bytes % sector_size == 0 &&
            bytes / sector_size <= UINT16_MAX) {
            int result = source_map_read(&stream->source,
                                         offset / sector_size, buffer,
                                         (uint16_t)(bytes / sector_size));
            if (result >= 0)
                return (int)bytes;
            source_map_reset(stream);
        }
    }
    return source_read_fallback(source_fd, offset, buffer, bytes);
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
    stream->stage_allocation = AllocSysMemory(
        ALLOC_FIRST, HDL_STREAM_IOP_STAGE_BYTES + 63u, NULL);
    if (stream->stage_allocation == NULL) {
        FreeSysMemory(stream);
        return -ENOMEM;
    }
    aligned = ((uintptr_t)stream->stage_allocation + 63u) & ~(uintptr_t)63u;
    stream->stage = (unsigned char *)aligned;

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
    file->privdata = stream;
    return 0;
}

static int stream_close(iomanX_iop_file_t *file)
{
    hdl_stream_file_t *stream = file->privdata;
    int result;

    if (stream == NULL)
        return -EBADF;
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
            *sector = skip + (uint32_t)(position / 512u);
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
        transfer.size = (uint32_t)chunk / 512u;
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
    uint64_t offset;
    int result;

    if (stream == NULL || request == NULL ||
        request_size != sizeof(*request) || request->source_fd < 0 ||
        request->bytes == 0 || request->bytes > HDL_STREAM_IOP_STAGE_BYTES ||
        (request->bytes & 0x1ffu) != 0)
        return -EINVAL;
    offset = (uint64_t)request->source_offset_low |
             ((uint64_t)request->source_offset_high << 32);
    result = source_read_at(stream, request->source_fd, offset,
                            stream->stage, request->bytes);
    if (result != (int)request->bytes)
        return result < 0 ? result : -EIO;
    if (pump) {
        result = stream_transfer(file, stream->stage, request->bytes,
                                 APA_IO_MODE_WRITE);
        if (result != (int)request->bytes)
            return result < 0 ? result : -EIO;
    }
    result = dma_to_ee(request->ee_address, stream->stage, request->bytes);
    return result < 0 ? result : (int)request->bytes;
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
    result = stream_transfer(file, stream->stage, request->bytes,
                             APA_IO_MODE_READ);
    if (result != (int)request->bytes)
        return result < 0 ? result : -EIO;
    result = dma_to_ee(request->ee_address, stream->stage, request->bytes);
    return result < 0 ? result : (int)request->bytes;
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
