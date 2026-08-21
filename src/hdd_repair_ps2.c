/*
 * Explicit PS2-only writer for the exceptional APA master recovery path.
 *
 * Normal manager operations never raw-write sectors 0-1: payload replacement
 * stays in the reserved __mbr program area and pointer changes go through
 * HDIOC_SETOSDMBR. This module exists only for a planner-approved damaged
 * master that cannot pass normal ps2hdd admission. The controller must save an
 * exact HDDRAW*.BIN snapshot before calling this function.
 *
 * Even here the writer owns mechanics, not authorization: the supplied 1024
 * bytes must already be a complete valid canonical APA master, GPT/protective
 * layouts are refused, the write is flushed, and both sectors are read back
 * and compared exactly before success is reported.
 */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include <string.h>

#include "apa.h"
#include "app_error.h"
#include "disk_status_ps2.h"
#include "hdd_read.h"
#include "hdd_repair_ps2.h"
#include "platform.h"

#define HDIOC_WRITESECTOR_LOCAL 0x6837
#define HDIOC_FLUSH_LOCAL 0x4804

typedef struct {
    unsigned int lba;
    unsigned int size;
    unsigned char data[APA_HEADER_SIZE];
} repair_write_packet_t;

static repair_write_packet_t repair_packet __attribute__((aligned(64)));
static unsigned char repair_verify[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

static int fail_master_repair(int code, const char *stage)
{
    app_error_record(APP_ERROR_DOMAIN_MASTER_REPAIR, code, stage);
    disk_status_end();
    return code;
}

int hdd_repair_write_master_header_verified(
    const unsigned char repaired[APA_HEADER_SIZE],
    unsigned char readback[APA_HEADER_SIZE])
{
    int result;

    if (repaired == NULL || readback == NULL) {
        app_error_record(APP_ERROR_DOMAIN_MASTER_REPAIR,
                         HDD_REPAIR_INVALID_ARGUMENT,
                         "validate recovery buffers");
        return HDD_REPAIR_INVALID_ARGUMENT;
    }

    if (!is_standard_apa_header(repaired) || is_hybrid_gpt(repaired) ||
        read_le32(repaired + APA_START_OFFSET) != 0 ||
        read_le16(repaired + APA_TYPE_OFFSET) != APA_MASTER_TYPE_VALUE ||
        read_le32(repaired + APA_MBR_VERSION_OFFSET) !=
            APA_MASTER_VERSION_VALUE) {
        app_error_record(APP_ERROR_DOMAIN_MASTER_REPAIR,
                         HDD_REPAIR_UNSAFE_HEADER,
                         "final canonical master validation");
        return HDD_REPAIR_UNSAFE_HEADER;
    }

    disk_status_begin_at("Exceptional APA master repair",
                         "Writing repaired sectors 0-1",
                         "APA master header / physical sectors 0-1");
    pad_activity_begin();
    repair_packet.lba = 0;
    repair_packet.size = 2;
    memcpy(repair_packet.data, repaired, APA_HEADER_SIZE);
    disk_status_io(DISK_STATUS_WRITE, 0, 2, 0, 2);
    result = fileXioDevctl("hdd0:", HDIOC_WRITESECTOR_LOCAL,
                           &repair_packet, sizeof(repair_packet), NULL, 0);
    if (result < 0) {
        pad_activity_end();
        return fail_master_repair(HDD_REPAIR_WRITE_FAILED,
                                  "HDIOC_WRITESECTOR sectors 0-1");
    }

    disk_status_phase_at("Flushing repaired APA master",
                         "ATA write cache / sectors 0-1 commit");
    disk_status_io(DISK_STATUS_FLUSH, 0, 2, 2, 2);
    result = fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL,
                           NULL, 0, NULL, 0);
    if (result < 0) {
        pad_activity_end();
        return fail_master_repair(HDD_REPAIR_FLUSH_FAILED,
                                  "HDIOC_FLUSH repaired master");
    }

    disk_status_phase_at("Reading sectors 0-1 back for exact verification",
                         "APA master header / physical sectors 0-1");
    disk_status_io(DISK_STATUS_VERIFY, 0, 2, 2, 2);
    result = hdd_read_raw_sectors(0, 2, repair_verify);
    if (result < 0) {
        pad_activity_end();
        return fail_master_repair(HDD_REPAIR_READBACK_FAILED,
                                  "read-back sectors 0-1");
    }
    if (memcmp(repair_verify, repaired, APA_HEADER_SIZE) != 0) {
        pad_activity_end();
        return fail_master_repair(HDD_REPAIR_COMPARE_FAILED,
                                  "compare repaired master read-back");
    }

    memcpy(readback, repair_verify, APA_HEADER_SIZE);
    pad_activity_end();
    disk_status_end();
    return 0;
}
