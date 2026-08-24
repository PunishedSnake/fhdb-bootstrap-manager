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
static uint64_t hdl_fast_source_total;
static int hdl_fast_source_position_valid;
static int hdl_fast_source_seek_pending;
static int hdl_fast_pump_mode;
static int hdl_fast_pump_ack;
static unsigned int hdl_fast_pump_ack_bytes;
static int hdl_fast_path_logged;
static int hdl_fast_stats_logged;

static void hdl_fast_io_reset(void)
{
    hdl_fast_source_fd = -1;
    hdl_fast_source_position = 0;
    hdl_fast_source_seek_value = 0;
    hdl_fast_source_total = 0;
    hdl_fast_source_position_valid = 0;
    hdl_fast_source_seek_pending = 0;
    hdl_fast_pump_mode = 0;
    hdl_fast_pump_ack = 0;
    hdl_fast_pump_ack_bytes = 0;
    hdl_fast_path_logged = 0;
    hdl_fast_stats_logged = 0;
}

static int hdl_fast_resolve_source_total(int source_fd)
{
    s64 end;
    s64 restore;

    if (hdl_fast_source_total != 0)
        return 0;
    if (source_fd < 0 || !hdl_fast_source_position_valid)
        return HDL_INSTALL_COPY_FAILED;

    /* Resolve EOF once through the ordinary fileXio descriptor. Direct-BDM
     * reads use explicit physical offsets afterwards, while the fallback path
     * seeks explicitly before every read, so restoring the descriptor here is
     * sufficient and costs only two RPCs per transaction rather than per block. */
    end = fileXioLseek64(source_fd, 0, FIO_SEEK_END);
    if (end <= 0)
        return end < 0 ? (int)end : HDL_INSTALL_COPY_FAILED;
    restore = fileXioLseek64(source_fd, (s64)hdl_fast_source_position,
                             FIO_SEEK_SET);
    if (restore != (s64)hdl_fast_source_position)
        return restore < 0 ? (int)restore : HDL_INSTALL_COPY_FAILED;
    hdl_fast_source_total = (uint64_t)end;
    return 0;
}

static void hdl_fast_io_log_stats(int target_fd)
{
    hdl_stream_fast_stats_t stats;
    int result;

    if (target_fd < 0 || hdl_fast_stats_logged)
        return;
    memset(&stats, 0, sizeof(stats));
    result = fileXioIoctl2(target_fd, HDL_STREAM_IOCTL2_GET_FAST_STATS,
                           NULL, 0, &stats, sizeof(stats));
    if (result < 0) {
        session_log_line("HDL fast I/O stats unavailable fd=%d result=%d",
                         target_fd, result);
        return;
    }
    session_log_line(
        "HDL fast I/O source stats flags=0x%08x fragments=%u direct=%u fallback=%u prefetch-hit=%u miss=%u pump=%u sectors=%u src-dma=%u",
        stats.flags, stats.fragment_count, stats.direct_reads,
        stats.fallback_reads, stats.prefetch_hits, stats.prefetch_misses,
        stats.pumped_chunks, stats.pumped_sectors, stats.source_dma_chunks);
    hdl_fast_stats_logged = 1;
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

        result = hdl_fast_resolve_source_total(source_fd);
        if (result < 0)
            return result;
        request.source_fd = source_fd;
        request.ee_address = (uint32_t)(uintptr_t)buffer;
        request.bytes = bytes;
        request.source_offset_low = (uint32_t)hdl_fast_source_position;
        request.source_offset_high = (uint32_t)(hdl_fast_source_position >> 32);
        request.source_total_low = (uint32_t)hdl_fast_source_total;
        request.source_total_high = (uint32_t)(hdl_fast_source_total >> 32);
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
                        "HDL IOP fast pump active: double-buffered USB source -> ps2hdd + one IOP->EE SHA DMA, chunk=%u",
                        (unsigned int)size);
                    hdl_fast_path_logged = 1;
                }
            } else {
                hdl_fast_pump_ack = 0;
                hdl_fast_pump_ack_bytes = 0;
            }
            if (result == size && hdl_fast_source_total != 0 &&
                hdl_fast_source_position == hdl_fast_source_total)
                hdl_fast_io_log_stats(hdl_active_target_fd);
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
