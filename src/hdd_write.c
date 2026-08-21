/*
 * PS2-specific write-capable HDD transport.
 *
 * This module owns raw write packets, flushes, and immediate read-back
 * verification. It does not decide whether a transaction is allowed,
 * create backups, request confirmation, sign KELFs, or choose ordering
 * between payload and pointer operations.
 */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include <string.h>

#include "apa.h"
#include "app_error.h"
#include "disk_status_ps2.h"
#include "hdd_limits.h"
#include "hdd_read.h"
#include "hdd_write.h"

/* Local names keep the source compatible with older PS2SDK headers. */
#define HDIOC_SETOSDMBR_LOCAL 0x6833
#define HDIOC_WRITESECTOR_LOCAL 0x6837
#define HDIOC_FLUSH_LOCAL 0x4804

/* Preserve Torii's historical project-defined verification codes. */
enum {
    HDD_WRITE_HEADER_READ_FAILED = -1,
    HDD_WRITE_HEADER_INVALID = -2,
    HDD_WRITE_START_MISMATCH = -3,
    HDD_WRITE_SIZE_MISMATCH = -4,
    HDD_WRITE_PAYLOAD_FLUSH_FAILED = -130,
    HDD_WRITE_PAYLOAD_COMPARE_FAILED = -131
};

typedef struct {
    u32 lba;
    u32 size;
    unsigned char data[HDD_TRANSFER_BYTES];
} raw_write_packet_t;

static raw_write_packet_t write_packet __attribute__((aligned(64)));
static unsigned char sector_verify_buffer[HDD_TRANSFER_BYTES]
    __attribute__((aligned(64)));
static unsigned char header_verify_buffer[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

int hdd_write_set_osd_mbr(u32 start, u32 size)
{
    hddSetOsdMBR_t info;
    int result;

    disk_status_begin("Bootstrap pointer update",
                      "Updating osdStart/osdSize through ps2hdd");
    disk_status_io(DISK_STATUS_POINTER, 0, 2, 0, 0);
    info.start = start;
    info.size = size;
    result = fileXioDevctl("hdd0:", HDIOC_SETOSDMBR_LOCAL,
                           &info, sizeof(info), NULL, 0);
    if (result < 0) {
        app_error_record(APP_ERROR_DOMAIN_IOP, result, "HDIOC_SETOSDMBR");
        disk_status_end();
        return result;
    }
    disk_status_phase("Flushing updated APA master pointer");
    disk_status_io(DISK_STATUS_FLUSH, 0, 2, 0, 0);
    result = fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL,
                           NULL, 0, NULL, 0);
    if (result < 0)
        app_error_record(APP_ERROR_DOMAIN_IOP, result, "HDIOC_FLUSH after SETOSDMBR");
    disk_status_end();
    return result;
}

int hdd_write_verify_osd_mbr(unsigned char destination[APA_HEADER_SIZE],
                             u32 expected_start, u32 expected_size)
{
    int result;

    disk_status_io(DISK_STATUS_VERIFY, 0, 2, 0, 0);
    if (hdd_read_raw_sectors(0, 2, header_verify_buffer) < 0) {
        app_error_record(APP_ERROR_DOMAIN_HDD_WRITE,
                         HDD_WRITE_HEADER_READ_FAILED,
                         "verify sectors 0-1 after pointer update");
        return HDD_WRITE_HEADER_READ_FAILED;
    }
    if (!is_standard_apa_header(header_verify_buffer)) {
        app_error_record(APP_ERROR_DOMAIN_HDD_WRITE,
                         HDD_WRITE_HEADER_INVALID,
                         "verify canonical APA master");
        return HDD_WRITE_HEADER_INVALID;
    }
    if (read_le32(header_verify_buffer + APA_OSD_START_OFFSET) !=
        expected_start) {
        app_error_record(APP_ERROR_DOMAIN_HDD_WRITE,
                         HDD_WRITE_START_MISMATCH,
                         "verify osdStart read-back");
        return HDD_WRITE_START_MISMATCH;
    }
    if (read_le32(header_verify_buffer + APA_OSD_SIZE_OFFSET) !=
        expected_size) {
        app_error_record(APP_ERROR_DOMAIN_HDD_WRITE,
                         HDD_WRITE_SIZE_MISMATCH,
                         "verify osdSize read-back");
        return HDD_WRITE_SIZE_MISMATCH;
    }
    memcpy(destination, header_verify_buffer, APA_HEADER_SIZE);
    result = 0;
    return result;
}

int hdd_write_payload_verified(const unsigned char *payload,
                               unsigned int payload_size,
                               u32 start_sector)
{
    unsigned int offset = 0;
    u32 sector_offset = 0;
    u32 total_sectors = (payload_size + HDD_SECTOR_SIZE - 1u) /
                        HDD_SECTOR_SIZE;

    disk_status_begin("Bootstrap payload transaction",
                      "Writing payload sectors");
    while (offset < payload_size) {
        unsigned int remaining = payload_size - offset;
        unsigned int bytes = remaining > HDD_TRANSFER_BYTES
                                 ? HDD_TRANSFER_BYTES : remaining;
        u32 sectors = (bytes + HDD_SECTOR_SIZE - 1) / HDD_SECTOR_SIZE;
        int result;

        write_packet.lba = start_sector + sector_offset;
        write_packet.size = sectors;
        memset(write_packet.data, 0, HDD_TRANSFER_BYTES);
        memcpy(write_packet.data, payload + offset, bytes);
        disk_status_io(DISK_STATUS_WRITE, write_packet.lba, sectors,
                       sector_offset, total_sectors);
        result = fileXioDevctl(
            "hdd0:", HDIOC_WRITESECTOR_LOCAL, &write_packet,
            sizeof(write_packet.lba) + sizeof(write_packet.size) +
                (sectors * HDD_SECTOR_SIZE),
            NULL, 0);
        if (result < 0) {
            app_error_record(APP_ERROR_DOMAIN_IOP, result,
                             "HDIOC_WRITESECTOR bootstrap payload");
            disk_status_end();
            return result;
        }
        offset += bytes;
        sector_offset += sectors;
    }

    disk_status_phase("Flushing bootstrap payload");
    disk_status_io(DISK_STATUS_FLUSH, start_sector, total_sectors,
                   total_sectors, total_sectors);
    if (fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL,
                      NULL, 0, NULL, 0) < 0) {
        app_error_record(APP_ERROR_DOMAIN_HDD_WRITE,
                         HDD_WRITE_PAYLOAD_FLUSH_FAILED,
                         "flush bootstrap payload");
        disk_status_end();
        return HDD_WRITE_PAYLOAD_FLUSH_FAILED;
    }

    disk_status_phase("Reading payload back for exact verification");
    offset = 0;
    sector_offset = 0;
    while (offset < payload_size) {
        unsigned int remaining = payload_size - offset;
        unsigned int bytes = remaining > HDD_TRANSFER_BYTES
                                 ? HDD_TRANSFER_BYTES : remaining;
        u32 sectors = (bytes + HDD_SECTOR_SIZE - 1) / HDD_SECTOR_SIZE;
        int result;

        memset(write_packet.data, 0, HDD_TRANSFER_BYTES);
        memcpy(write_packet.data, payload + offset, bytes);
        disk_status_io(DISK_STATUS_VERIFY, start_sector + sector_offset,
                       sectors, sector_offset, total_sectors);
        result = hdd_read_raw_sectors(start_sector + sector_offset,
                                      sectors, sector_verify_buffer);
        if (result < 0) {
            disk_status_end();
            return result;
        }
        if (memcmp(write_packet.data, sector_verify_buffer,
                   sectors * HDD_SECTOR_SIZE) != 0) {
            app_error_record(APP_ERROR_DOMAIN_HDD_WRITE,
                             HDD_WRITE_PAYLOAD_COMPARE_FAILED,
                             "compare bootstrap payload read-back");
            disk_status_end();
            return HDD_WRITE_PAYLOAD_COMPARE_FAILED;
        }
        offset += bytes;
        sector_offset += sectors;
    }
    disk_status_end();
    return 0;
}
