/* PS2 storage-side preservation of exact pre-repair sector-zero state. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <string.h>

#include "repair_snapshot.h"
#include "storage.h"

static unsigned char snapshot_verify[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

static const char *const snapshot_names[REPAIR_SNAPSHOT_SLOT_COUNT] = {
    "HDDRAW.BIN", "HDDRAW2.BIN"
};

int repair_snapshot_save(unsigned int storage,
                         const unsigned char header[APA_HEADER_SIZE],
                         char path_out[REPAIR_SNAPSHOT_PATH_SIZE])
{
    unsigned int i;

    if (storage >= STORAGE_TARGET_COUNT || header == NULL || path_out == NULL)
        return REPAIR_SNAPSHOT_INVALID_ARGUMENT;

    for (i = 0; i < REPAIR_SNAPSHOT_SLOT_COUNT; i++) {
        char path[REPAIR_SNAPSHOT_PATH_SIZE];
        iox_stat_t stat;
        int result;

        storage_path(path, sizeof(path), storage, snapshot_names[i]);
        memset(&stat, 0, sizeof(stat));
        result = fileXioGetStat(path, &stat);
        if (result >= 0) {
            if (stat.size == APA_HEADER_SIZE &&
                read_exact_file(path, snapshot_verify, APA_HEADER_SIZE) == 0 &&
                memcmp(snapshot_verify, header, APA_HEADER_SIZE) == 0) {
                strncpy(path_out, path, REPAIR_SNAPSHOT_PATH_SIZE - 1u);
                path_out[REPAIR_SNAPSHOT_PATH_SIZE - 1u] = '\0';
                return 0;
            }
            continue;
        }

        result = write_whole_file(path, header, APA_HEADER_SIZE);
        if (result < 0)
            return REPAIR_SNAPSHOT_WRITE_FAILED;
        result = read_exact_file(path, snapshot_verify, APA_HEADER_SIZE);
        if (result < 0 ||
            memcmp(snapshot_verify, header, APA_HEADER_SIZE) != 0)
            return REPAIR_SNAPSHOT_VERIFY_FAILED;

        strncpy(path_out, path, REPAIR_SNAPSHOT_PATH_SIZE - 1u);
        path_out[REPAIR_SNAPSHOT_PATH_SIZE - 1u] = '\0';
        return 0;
    }

    return REPAIR_SNAPSHOT_NO_SLOT;
}
