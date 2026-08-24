/*
 * EE-side glue for the dev20 IOP payload pump.
 *
 * The transaction code intentionally remains conservative and still thinks in
 * ordinary read/hash/write chunks. These wrappers recognize the exact source
 * seek -> target seek -> source read -> target write pattern and collapse the
 * bulk movement into one hdl0: ioctl2 executed entirely on the IOP.
 *
 * Source bytes are DMAed back to this 64-byte-aligned EE buffer once so the
 * R5900 can keep the existing SHA-256 verification policy. The following
 * fileXioWrite becomes an acknowledgement only because ps2hdd already received
 * the same staging buffer on the IOP. Target verification similarly reads HDD
 * into IOP staging and performs one IOP->EE DMA, avoiding fileXio's extra
 * staging/copy layer.
 */
static int hdl_fast_source_fd = -1;
static uint64_t hdl_fast_source_position;
static uint64_t hdl_fast_source_seek_value;
static int hdl_fast_source_position_valid;
static int hdl_fast_source_seek_pending;
static int hdl_fast_pump_mode;
static int hdl_fast_pump_ack;
static unsigned int hdl_fast_pump_ack_bytes;
static int hdl_fast_path_logged;

static void hdl_fast_io_reset(void)
{
    hdl_fast_source_fd = -1;
    hdl_fast_source_position = 0;
    hdl_fast_source_seek_value = 0;
    hdl_fast_source_position_valid = 0;
    hdl_fast_source_seek_pending = 0;
    hdl_fast_pump_mode = 0;
    hdl_fast_pump_ack = 0;
    hdl_fast_pump_ack_bytes = 0;
}

static s64 hdl_fast_fileXioLseek64(int fd, s64 offset, int whence)
{
    s64 result = fileXioLseek64(fd, offset, whence);

    if (result < 0)
        return result;
    hdl_fast_pump_ack = 0;
    hdl_fast_pump_ack_bytes = 0;
    if (fd == hdl_active_target_fd) {
        hdl_fast_pump_mode =
            hdl_fast_source_seek_pending && hdl_fast_source_position_valid &&
            hdl_fast_source_seek_value == (uint64_t)result;
        hdl_fast_source_seek_pending = 0;
    } else {
        hdl_fast_source_fd = fd;
        hdl_fast_source_position = (uint64_t)result;
        hdl_fast_source_position_valid = 1;
        hdl_fast_source_seek_pending = 1;
        hdl_fast_source_seek_value = (uint64_t)result;
        hdl_fast_pump_mode = 0;
    }
    return result;
}

static int hdl_fast_dma_read(int command, int source_fd, void *buffer,
                             unsigned int bytes)
{
    int result;

    if (hdl_active_target_fd < 0 || buffer == NULL || bytes == 0 ||
        bytes > HDL_STREAM_IOP_STAGE_BYTES || (bytes & 0x1ffu) != 0 ||
        ((uintptr_t)buffer & 0x3fu) != 0)
        return INT_MIN;

    /* The IOP is about to DMA into cached EE memory. Write back any dirty line
     * first, then invalidate after the synchronous ioctl returns so the R5900
     * sees the new payload instead of a stale D-cache copy. */
    SyncDCache(buffer, (unsigned char *)buffer + bytes);
    if (command == HDL_STREAM_IOCTL2_TARGET_TO_EE) {
        hdl_stream_target_io_t request;

        request.ee_address = (uint32_t)(uintptr_t)buffer;
        request.bytes = bytes;
        result = fileXioIoctl2(hdl_active_target_fd, command,
                               &request, sizeof(request), NULL, 0);
    } else {
        hdl_stream_source_io_t request;

        request.source_fd = source_fd;
        request.ee_address = (uint32_t)(uintptr_t)buffer;
        request.bytes = bytes;
        request.source_offset_low = (uint32_t)hdl_fast_source_position;
        request.source_offset_high = (uint32_t)(hdl_fast_source_position >> 32);
        result = fileXioIoctl2(hdl_active_target_fd, command,
                               &request, sizeof(request), NULL, 0);
    }
    if (result >= 0)
        InvalidDCache(buffer, (unsigned char *)buffer + bytes);
    return result;
}

static int hdl_fast_fileXioRead(int fd, void *buffer, int size)
{
    int result;

    if (size <= 0)
        return fileXioRead(fd, buffer, size);

    if (fd == hdl_active_target_fd) {
        hdl_fast_pump_mode = 0;
        hdl_fast_pump_ack = 0;
        hdl_fast_pump_ack_bytes = 0;
        result = hdl_fast_dma_read(HDL_STREAM_IOCTL2_TARGET_TO_EE,
                                   -1, buffer, (unsigned int)size);
        if (result != INT_MIN)
            return result;
    } else if (hdl_active_target_fd >= 0 &&
               hdl_fast_source_position_valid && fd == hdl_fast_source_fd) {
        int command = hdl_fast_pump_mode
                          ? HDL_STREAM_IOCTL2_PUMP_TO_EE
                          : HDL_STREAM_IOCTL2_SOURCE_TO_EE;

        result = hdl_fast_dma_read(command, fd, buffer, (unsigned int)size);
        if (result != INT_MIN) {
            hdl_fast_source_seek_pending = 0;
            if (result > 0)
                hdl_fast_source_position += (unsigned int)result;
            if (result == size && command == HDL_STREAM_IOCTL2_PUMP_TO_EE) {
                hdl_fast_pump_ack = 1;
                hdl_fast_pump_ack_bytes = (unsigned int)size;
                if (!hdl_fast_path_logged) {
                    session_log_line(
                        "HDL IOP fast pump active: USB source -> ps2hdd + one IOP->EE SHA DMA, chunk=%u",
                        (unsigned int)size);
                    hdl_fast_path_logged = 1;
                }
            } else {
                hdl_fast_pump_ack = 0;
                hdl_fast_pump_ack_bytes = 0;
            }
            return result;
        }
    }

    /* Keep the old 64 KiB fileXio tuning as a fallback. If fast reads already
     * advanced only our logical source position, synchronize the real FatFs
     * descriptor before dropping back to stock fileXio. */
    hdl_stream_tune_filexio_once();
    if (fd != hdl_active_target_fd && hdl_fast_source_position_valid &&
        fd == hdl_fast_source_fd) {
        s64 seek = fileXioLseek64(fd, (s64)hdl_fast_source_position,
                                  FIO_SEEK_SET);

        if (seek != (s64)hdl_fast_source_position)
            return seek < 0 ? (int)seek : HDL_INSTALL_COPY_FAILED;
    }
    result = fileXioRead(fd, buffer, size);
    if (result > 0 && fd != hdl_active_target_fd &&
        hdl_fast_source_position_valid && fd == hdl_fast_source_fd)
        hdl_fast_source_position += (unsigned int)result;
    hdl_fast_source_seek_pending = 0;
    hdl_fast_pump_ack = 0;
    hdl_fast_pump_ack_bytes = 0;
    return result;
}

static int hdl_fast_fileXioWrite(int fd, const void *buffer, int size)
{
    if (hdl_fast_pump_ack) {
        if (fd == hdl_active_target_fd && size > 0 &&
            (unsigned int)size == hdl_fast_pump_ack_bytes) {
            hdl_fast_pump_ack = 0;
            hdl_fast_pump_ack_bytes = 0;
            return size;
        }
        /* Never fall back after PUMP_TO_EE already advanced the target. A
         * mismatched acknowledgement would otherwise write the block twice. */
        hdl_fast_pump_ack = 0;
        hdl_fast_pump_ack_bytes = 0;
        return HDL_INSTALL_COPY_FAILED;
    }

    hdl_stream_tune_filexio_once();
    return fileXioWrite(fd, buffer, size);
}
