/* PS2 storage/acquisition lifecycle for versioned HDD rescue capsules. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <delaythread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdd_limits.h"
#include "hdd_read.h"
#include "kelf.h"
#include "rescue_image.h"
#include "rescue_storage.h"
#include "session_log.h"
#include "sha256.h"
#include "storage.h"

#define RESCUE_STORAGE_MAX_CAPSULE_SIZE \
    (RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE + HDD_MAX_MBR_PAYLOAD_SIZE)

static void rescue_path_for_slot(char *path, unsigned int capacity,
                                 unsigned int storage, unsigned int slot)
{
    storage_path(path, capacity, storage,
                 slot == 0 ? "HDDRESCUE.BIN" : "HDDRESCUE2.BIN");
}

static int write_rescue_file(unsigned int storage, const char *path,
                             const unsigned char *metadata,
                             const unsigned char *apa_header,
                             const unsigned char *payload,
                             unsigned int payload_bytes)
{
    int attempts = storage == 2 ? 20 : 1;
    int fd = -1;
    const unsigned char *parts[3];
    unsigned int sizes[3];
    unsigned int part;

    while (attempts-- > 0) {
        fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC,
                         0666);
        if (fd >= 0)
            break;
        DelayThread(250000);
    }
    if (fd < 0)
        return fd;

    parts[0] = metadata;
    parts[1] = apa_header;
    parts[2] = payload;
    sizes[0] = RESCUE_CAPSULE_METADATA_SIZE;
    sizes[1] = APA_HEADER_SIZE;
    sizes[2] = payload_bytes;
    for (part = 0; part < 3; part++) {
        unsigned int total = 0;

        while (total < sizes[part]) {
            int written = fileXioWrite(fd, parts[part] + total,
                                       sizes[part] - total);

            if (written <= 0) {
                fileXioClose(fd);
                return written < 0 ? written : -180;
            }
            total += (unsigned int)written;
        }
    }
    fileXioClose(fd);
    return 0;
}

static int load_rescue_file(const char *path, rescue_capsule_info_t *info,
                            unsigned char **file_data_out,
                            unsigned int *file_size_out)
{
    unsigned char *data = NULL;
    unsigned int size = 0;
    int result;

    result = read_bounded_file(path, RESCUE_STORAGE_MAX_CAPSULE_SIZE,
                               &data, &size);
    if (result < 0)
        return result;

    result = rescue_image_validate(data, size, info);
    if (result < 0) {
        free(data);
        return result;
    }

    *file_data_out = data;
    *file_size_out = size;
    return 0;
}

static int rescue_file_matches(const char *path,
                               const rescue_capsule_info_t *expected)
{
    rescue_capsule_info_t existing;
    unsigned char *data = NULL;
    unsigned int size = 0;
    int result = load_rescue_file(path, &existing, &data, &size);

    (void)size;
    if (result < 0)
        return 0;
    result = rescue_image_state_matches(&existing, expected);
    free(data);
    return result;
}

const char *rescue_storage_save_current(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    const char *romver, const char *family, const char *confidence)
{
    static char saved_path[RESCUE_STORAGE_PATH_SIZE];
    rescue_capsule_info_t info;
    rescue_capsule_info_t verified_info;
    unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE];
    unsigned char *payload = NULL;
    unsigned char *verified_file = NULL;
    unsigned int payload_bytes = 0;
    unsigned int kelf_file_bytes = 0;
    unsigned int verified_size = 0;
    unsigned int slot;
    u32 start;
    u32 sectors;
    int result;

    if (storage >= STORAGE_TARGET_COUNT || current_header == NULL)
        return NULL;

    start = (u32)read_le32(current_header + APA_OSD_START_OFFSET);
    sectors = (u32)read_le32(current_header + APA_OSD_SIZE_OFFSET);
    if ((start == 0) != (sectors == 0)) {
        session_log_line("Rescue capsule rejected inconsistent pointer state");
        return NULL;
    }

    memset(&info, 0, sizeof(info));
    info.flags = RESCUE_CAPSULE_FLAG_VALID_APA;
    info.payload_start = start;
    info.payload_sectors = sectors;
    snprintf(info.romver, sizeof(info.romver), "%s",
             romver != NULL ? romver : "");
    snprintf(info.family, sizeof(info.family), "%s",
             family != NULL ? family : "");
    snprintf(info.confidence, sizeof(info.confidence), "%s",
             confidence != NULL ? confidence : "");
    sha256_buffer(current_header, APA_HEADER_SIZE, info.apa_sha256);

    if (start != 0) {
        result = hdd_validate_payload_bounds(start, sectors);
        if (result < 0) {
            session_log_line("Rescue capsule payload bounds failed: %d", result);
            return NULL;
        }
        result = hdd_read_payload_image(start, sectors, &payload, &payload_bytes);
        if (result < 0) {
            session_log_line("Rescue capsule payload read failed: %d", result);
            return NULL;
        }
        info.flags |= RESCUE_CAPSULE_FLAG_HAS_PAYLOAD;
        info.payload_bytes = payload_bytes;
        sha256_buffer(payload, payload_bytes, info.payload_sha256);
        if (kelf_size_from_disk_image(payload, payload_bytes,
                                      &kelf_file_bytes) == 0) {
            info.kelf_file_bytes = kelf_file_bytes;
            info.flags |= RESCUE_CAPSULE_FLAG_VALID_KELF;
        }
    }

    rescue_capsule_encode(metadata, &info);
    for (slot = 0; slot < RESCUE_STORAGE_SLOT_COUNT; slot++) {
        iox_stat_t existing;

        rescue_path_for_slot(saved_path, sizeof(saved_path), storage, slot);
        memset(&existing, 0, sizeof(existing));
        if (fileXioGetStat(saved_path, &existing) >= 0) {
            if (rescue_file_matches(saved_path, &info)) {
                free(payload);
                session_log_line("Existing rescue capsule already matches: %s",
                                 saved_path);
                return saved_path;
            }
            continue;
        }

        result = write_rescue_file(storage, saved_path, metadata,
                                   current_header, payload, payload_bytes);
        if (result < 0) {
            session_log_line("Rescue capsule write failed at %s: %d",
                             saved_path, result);
            continue;
        }
        result = load_rescue_file(saved_path, &verified_info, &verified_file,
                                  &verified_size);
        if (result == 0 && verified_info.flags == info.flags &&
            memcmp(verified_info.apa_sha256, info.apa_sha256, 32) == 0 &&
            memcmp(verified_info.payload_sha256,
                   info.payload_sha256, 32) == 0) {
            free(verified_file);
            free(payload);
            session_log_line("Rescue capsule saved and verified: %s (%u bytes)",
                             saved_path, verified_size);
            return saved_path;
        }
        free(verified_file);
        verified_file = NULL;
        session_log_line("Rescue capsule read-back verification failed: %s (%d)",
                         saved_path, result);
    }

    free(payload);
    return NULL;
}

int rescue_storage_find(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    rescue_storage_entry_t *entry)
{
    unsigned int slot;
    int saw_existing = 0;
    int saw_invalid = 0;
    int saw_header_only = 0;
    int first_error = RESCUE_STORAGE_NOT_FOUND;

    if (storage >= STORAGE_TARGET_COUNT || current_header == NULL ||
        entry == NULL)
        return RESCUE_STORAGE_NOT_FOUND;

    memset(entry, 0, sizeof(*entry));
    for (slot = 0; slot < RESCUE_STORAGE_SLOT_COUNT; slot++) {
        char path[RESCUE_STORAGE_PATH_SIZE];
        rescue_capsule_info_t candidate;
        unsigned char *candidate_data = NULL;
        unsigned int candidate_size = 0;
        const unsigned char *candidate_header;
        int result;

        rescue_path_for_slot(path, sizeof(path), storage, slot);
        if (!path_exists(path))
            continue;
        saw_existing = 1;
        result = load_rescue_file(path, &candidate, &candidate_data,
                                  &candidate_size);
        if (result < 0) {
            saw_invalid = 1;
            if (first_error == RESCUE_STORAGE_NOT_FOUND ||
                first_error == RESCUE_STORAGE_HEADER_ONLY)
                first_error = result;
            continue;
        }

        candidate_header = candidate_data + RESCUE_CAPSULE_METADATA_SIZE;
        if (!headers_match_same_disk(current_header, candidate_header)) {
            free(candidate_data);
            saw_invalid = 1;
            if (first_error == RESCUE_STORAGE_NOT_FOUND ||
                first_error == RESCUE_STORAGE_HEADER_ONLY)
                first_error = RESCUE_STORAGE_WRONG_DISK;
            continue;
        }
        if ((candidate.flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) == 0) {
            free(candidate_data);
            saw_header_only = 1;
            if (first_error == RESCUE_STORAGE_NOT_FOUND)
                first_error = RESCUE_STORAGE_HEADER_ONLY;
            continue;
        }
        if ((candidate.flags & RESCUE_CAPSULE_FLAG_VALID_KELF) == 0) {
            free(candidate_data);
            saw_invalid = 1;
            if (first_error == RESCUE_STORAGE_NOT_FOUND ||
                first_error == RESCUE_STORAGE_HEADER_ONLY)
                first_error = RESCUE_STORAGE_INVALID_KELF;
            continue;
        }

        snprintf(entry->path, sizeof(entry->path), "%s", path);
        entry->info = candidate;
        entry->file_data = candidate_data;
        entry->file_size = candidate_size;
        return 0;
    }

    if (!saw_existing)
        return RESCUE_STORAGE_NOT_FOUND;
    if (saw_invalid)
        return first_error;
    return saw_header_only ? RESCUE_STORAGE_HEADER_ONLY : first_error;
}

const unsigned char *rescue_storage_payload(
    const rescue_storage_entry_t *entry)
{
    if (entry == NULL || entry->file_data == NULL ||
        (entry->info.flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) == 0)
        return NULL;
    return entry->file_data + RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE;
}

void rescue_storage_entry_release(rescue_storage_entry_t *entry)
{
    if (entry == NULL)
        return;
    free(entry->file_data);
    memset(entry, 0, sizeof(*entry));
}
