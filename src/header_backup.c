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

/*
 * Probe only the property this policy actually needs: whether a readable file
 * already occupies the backup slot and, if so, its byte size. fileXioGetStat
 * would also convert atime/mtime/ctime through the current PS2SDK POSIX glue,
 * pulling mktime/tzset/scanf machinery into the EE image even though backup
 * policy never consumes timestamps or POSIX mode bits.
 */
static int backup_file_size(const char *path, int *size_out)
{
    int fd;
    int size;

    if (path == NULL || size_out == NULL)
        return -1;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    size = fileXioLseek(fd, 0, FIO_SEEK_END);
    fileXioClose(fd);
    if (size < 0)
        return size;
    *size_out = size;
    return 0;
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
        int existing_size = 0;

        diagnostics->read_result[i] =
            backup_file_size(diagnostics->path[i], &existing_size);
        if (diagnostics->read_result[i] >= 0) {
            if (existing_size == APA_HEADER_SIZE &&
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
    u32 *start_out, u32 *size_out)
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
        u32 start;
        u32 size;
        char path[HEADER_BACKUP_PATH_SIZE];

        storage_path(path, sizeof(path), storage, filenames[i]);
        if (read_exact_file(path, backup_scratch, APA_HEADER_SIZE) < 0 ||
            !is_standard_apa_header(backup_scratch) ||
            !headers_match_same_disk(current_header, backup_scratch))
            continue;
        start = (u32)read_le32(backup_scratch + APA_OSD_START_OFFSET);
        size = (u32)read_le32(backup_scratch + APA_OSD_SIZE_OFFSET);
        if (start == 0 || size == 0)
            continue;
        snprintf(found_path, path_capacity, "%s", path);
        *start_out = start;
        *size_out = size;
        return 0;
    }
    return -1;
}
