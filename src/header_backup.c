/* PS2 storage-side policy for mandatory and legacy APA-header backups. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <stdio.h>
#include <string.h>

#include "header_backup.h"
#include "storage.h"

static unsigned char backup_scratch[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

static void backup_path_for_slot(char *path, unsigned int capacity,
                                 unsigned int storage, unsigned int slot)
{
    storage_path(path, capacity, storage,
                 slot == 0 ? "HDDMBR.BIN" : "HDDMBR2.BIN");
}

const char *header_backup_save(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    header_backup_diagnostics_t *diagnostics)
{
    unsigned int i;

    if (storage >= STORAGE_TARGET_COUNT || current_header == NULL ||
        diagnostics == NULL)
        return NULL;

    for (i = 0; i < HEADER_BACKUP_SLOT_COUNT; i++) {
        backup_path_for_slot(diagnostics->path[i],
                             sizeof(diagnostics->path[i]), storage, i);
        diagnostics->read_result[i] = HEADER_BACKUP_NOT_TRIED;
        diagnostics->write_result[i] = HEADER_BACKUP_NOT_TRIED;
        diagnostics->verify_result[i] = HEADER_BACKUP_NOT_TRIED;
    }

    for (i = 0; i < HEADER_BACKUP_SLOT_COUNT; i++) {
        iox_stat_t existing_stat;

        memset(&existing_stat, 0, sizeof(existing_stat));
        diagnostics->read_result[i] =
            fileXioGetStat(diagnostics->path[i], &existing_stat);
        if (diagnostics->read_result[i] >= 0) {
            if (existing_stat.size == APA_HEADER_SIZE &&
                read_exact_file(diagnostics->path[i], backup_scratch,
                                APA_HEADER_SIZE) == 0 &&
                is_standard_apa_header(backup_scratch) &&
                memcmp(current_header, backup_scratch, APA_HEADER_SIZE) == 0)
                return diagnostics->path[i];
            diagnostics->write_result[i] = HEADER_BACKUP_OCCUPIED;
            continue;
        }

        diagnostics->write_result[i] =
            write_whole_file(diagnostics->path[i], current_header,
                             APA_HEADER_SIZE);
        if (diagnostics->write_result[i] == 0) {
            diagnostics->verify_result[i] =
                read_exact_file(diagnostics->path[i], backup_scratch,
                                APA_HEADER_SIZE);
            if (diagnostics->verify_result[i] == 0 &&
                memcmp(current_header, backup_scratch, APA_HEADER_SIZE) == 0)
                return diagnostics->path[i];
            if (diagnostics->verify_result[i] == 0)
                diagnostics->verify_result[i] = -1;
        }
    }
    return NULL;
}

int header_backup_find_enabled(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    char *found_path, unsigned int path_capacity,
    uint32_t *start_out, uint32_t *size_out)
{
    static const char *const filenames[] = {
        "HDDMBR.BIN", "HDDMBR2.BIN", "FHDBMBR.BIN", "FHDBMBR2.BIN"
    };
    unsigned int i;

    if (storage >= STORAGE_TARGET_COUNT || current_header == NULL ||
        found_path == NULL || path_capacity == 0 || start_out == NULL ||
        size_out == NULL)
        return -1;

    for (i = 0; i < sizeof(filenames) / sizeof(filenames[0]); i++) {
        uint32_t start;
        uint32_t size;
        char path[HEADER_BACKUP_PATH_SIZE];

        storage_path(path, sizeof(path), storage, filenames[i]);
        if (read_exact_file(path, backup_scratch, APA_HEADER_SIZE) < 0 ||
            !is_standard_apa_header(backup_scratch) ||
            !headers_match_same_disk(current_header, backup_scratch))
            continue;
        start = read_le32(backup_scratch + APA_OSD_START_OFFSET);
        size = read_le32(backup_scratch + APA_OSD_SIZE_OFFSET);
        if (start == 0 || size == 0)
            continue;
        snprintf(found_path, path_capacity, "%s", path);
        *start_out = start;
        *size_out = size;
        return 0;
    }
    return -1;
}
