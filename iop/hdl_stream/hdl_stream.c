/*
 * Sequential HDL payload transport for the IOP.
 *
 * This driver deliberately delegates all disk access to ps2hdd. It exposes
 * only the data areas of an already-created APA main/sub set and keeps the
 * attribute-area metadata commit behind a separate verified operation.
 */

#include <errno.h>
#include <hdd-ioctl.h>
#include <iomanX.h>
#include <irx.h>
#include <loadcore.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/fcntl.h>
#include <sysclib.h>
#include <sysmem.h>

#include "hdl_stream_rpc.h"

#define HDL_STREAM_MAIN_SKIP 0x2000u
#define HDL_STREAM_SUB_SKIP 0x0800u
#define HDL_STREAM_METADATA_OFFSET 0x100000
#define HDL_STREAM_DEVICE "hdl"

IRX_ID("hdl_stream", 1, 1);

typedef struct {
    int hdd_fd;
    int writable;
    uint32_t count;
    uint32_t lengths[HDL_STREAM_MAX_PARTITIONS];
    uint32_t starts[HDL_STREAM_MAX_PARTITIONS];
    uint64_t capacity_bytes;
    uint64_t position;
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

/*
 * Several ps2hdd ioctl2 commands return a raw u32 through the signed int
 * return channel. On disks whose APA partition starts have bit 31 set, a
 * perfectly valid LBA therefore looks like a huge negative number to C code.
 * Small negative values are real errno results; everything else is the
 * driver's u32 payload and must be preserved bit-for-bit.
 */
static int ioctl_u32_result(int raw, uint32_t *value)
{
    if (value == NULL)
        return -EINVAL;
    if (raw < 0 && raw > -4096)
        return raw;
    *value = (uint32_t)raw;
    return 0;
}

static int stream_open(iomanX_iop_file_t *file, const char *name,
                       int flags, int mode)
{
    hdl_stream_file_t *stream;
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
    stream->hdd_fd = iomanX_open(path, flags, 0);
    if (stream->hdd_fd < 0) {
        result = stream->hdd_fd;
        FreeSysMemory(stream);
        return result;
    }
    stream->writable = (flags & FIO_O_WRONLY) != 0;
    sub_count = iomanX_ioctl2(stream->hdd_fd, HIOCNSUB,
                              NULL, 0, NULL, 0);
    if (sub_count < 0 || sub_count >= (int)HDL_STREAM_MAX_PARTITIONS) {
        result = sub_count < 0 ? sub_count : -EFBIG;
        iomanX_close(stream->hdd_fd);
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
            FreeSysMemory(stream);
            return result;
        }
        result = ioctl_u32_result(raw_start, &start);
        if (result < 0 || length <= skip) {
            if (result >= 0)
                result = -EINVAL;
            iomanX_close(stream->hdd_fd);
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
    result = iomanX_close(stream->hdd_fd);
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
        result = iomanX_lseek(stream->hdd_fd, HDL_STREAM_METADATA_OFFSET,
                              FIO_SEEK_SET);
    if (result == HDL_STREAM_METADATA_OFFSET)
        result = iomanX_read(stream->hdd_fd, verify,
                             HDL_STREAM_METADATA_SIZE);
    if (result == HDL_STREAM_METADATA_SIZE)
        result = memcmp(metadata, verify, HDL_STREAM_METADATA_SIZE) == 0 ?
                 0 : -EIO;
    else if (result >= 0)
        result = -EIO;
    FreeSysMemory(verify);
    return result;
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
    return -EINVAL;
}

IOMANX_RETURN_VALUE_IMPL(EPERM);

static iomanX_iop_device_ops_t stream_ops = {
    &stream_init,
    &stream_deinit,
    IOMANX_RETURN_VALUE(EPERM),
    &stream_open,
    &stream_close,
    &stream_read,
    &stream_write,
    &stream_seek,
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    &stream_seek64,
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM),
    &stream_ioctl2
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
