/* PS2 storage-side preservation of exact pre-repair sector-zero state. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <string.h>

#include "app_error.h"
#include "disk_status_ps2.h"
#include "repair_snapshot.h"
#include "storage.h"

static unsigned char snapshot_verify[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

static const char *const snapshot_names[REPAIR_SNAPSHOT_SLOT_COUNT] = {
    "HDDRAW.BIN", "HDDRAW2.BIN"
};

static int fail_snapshot(int code, const char *stage)
{
    app_error_record(APP_ERROR_DOMAIN_REPAIR_SNAPSHOT, code, stage);
    return code;
}

int repair_snapshot_save(unsigned int storage,
                         const unsigned char header[APA_HEADER_SIZE],
                         char path_out[REPAIR_SNAPSHOT_PATH_SIZE])
{
    unsigned int i;

    if (storage >= STORAGE_TARGET_COUNT || header == NULL || path_out == NULL)
        return fail_snapshot(REPAIR_SNAPSHOT_INVALID_ARGUMENT,
                             "validate HDDRAW snapshot arguments");

    disk_status_begin_at("Deterministic repair safety snapshot",
                         "Selecting a non-overwriting HDDRAW slot",
                         "Selected backup storage; source is APA sectors 0-1");
    for (i = 0; i < REPAIR_SNAPSHOT_SLOT_COUNT; i++) {
        char path[REPAIR_SNAPSHOT_PATH_SIZE];
        iox_stat_t stat;
        int result;

        storage_path(path, sizeof(path), storage, snapshot_names[i]);
        disk_status_phase_at("Checking HDDRAW snapshot slot", path);
        disk_status_io(DISK_STATUS_SCAN, 0, 2, i, REPAIR_SNAPSHOT_SLOT_COUNT);
        memset(&stat, 0, sizeof(stat));
        result = fileXioGetStat(path, &stat);
        if (result >= 0) {
            if (stat.size == APA_HEADER_SIZE &&
                read_exact_file(path, snapshot_verify, APA_HEADER_SIZE) == 0 &&
                memcmp(snapshot_verify, header, APA_HEADER_SIZE) == 0) {
                strncpy(path_out, path, REPAIR_SNAPSHOT_PATH_SIZE - 1u);
                path_out[REPAIR_SNAPSHOT_PATH_SIZE - 1u] = '\0';
                disk_status_end();
                return 0;
            }
            continue;
        }

        disk_status_phase_at("Writing exact 1024-byte HDDRAW snapshot", path);
        disk_status_io(DISK_STATUS_SCAN, 0, 2, i + 1u,
                       REPAIR_SNAPSHOT_SLOT_COUNT);
        result = write_whole_file(path, header, APA_HEADER_SIZE);
        if (result < 0) {
            disk_status_end();
            return fail_snapshot(REPAIR_SNAPSHOT_WRITE_FAILED,
                                 "write HDDRAW snapshot");
        }
        disk_status_phase_at("Reading HDDRAW back and comparing every byte", path);
        disk_status_io(DISK_STATUS_VERIFY, 0, 2, i + 1u,
                       REPAIR_SNAPSHOT_SLOT_COUNT);
        result = read_exact_file(path, snapshot_verify, APA_HEADER_SIZE);
        if (result < 0 ||
            memcmp(snapshot_verify, header, APA_HEADER_SIZE) != 0) {
            disk_status_end();
            return fail_snapshot(REPAIR_SNAPSHOT_VERIFY_FAILED,
                                 "read back/compare HDDRAW snapshot");
        }

        strncpy(path_out, path, REPAIR_SNAPSHOT_PATH_SIZE - 1u);
        path_out[REPAIR_SNAPSHOT_PATH_SIZE - 1u] = '\0';
        disk_status_end();
        return 0;
    }

    disk_status_end();
    return fail_snapshot(REPAIR_SNAPSHOT_NO_SLOT,
                         "select non-overwriting HDDRAW slot");
}
