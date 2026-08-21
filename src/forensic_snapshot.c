/* Versioned preservation of every APA header touched by forensic repair. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "forensic_snapshot.h"

#include <stdlib.h>
#include <string.h>

#include "app_error.h"
#include "sha256.h"
#include "storage.h"

#define SNAPSHOT_HEADER_BYTES 64u
#define SNAPSHOT_ENTRY_BYTES (4u + 32u + APA_HEADER_SIZE)
#define SNAPSHOT_TRAILER_BYTES 32u

static const unsigned char snapshot_magic[8] = {
    'A', 'P', 'A', 'M', 'E', 'T', 'A', '1'
};
static const char *const snapshot_names[FORENSIC_SNAPSHOT_SLOT_COUNT] = {
    "HDDMETA.BIN", "HDDMETA2.BIN"
};

static int fail_snapshot(int code, const char *stage)
{
    app_error_record(APP_ERROR_DOMAIN_FORENSIC_SNAPSHOT, code, stage);
    return code;
}

static void write_le32_snapshot(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static int build_snapshot_image(const apa_forensic_result_t *result,
                                const apa_forensic_repair_plan_t *plan,
                                unsigned char **image_out,
                                unsigned int *size_out)
{
    unsigned int size;
    unsigned int offset;
    unsigned int one_or_two_bit_count = 0;
    unsigned int i;
    unsigned char *image;

    if (plan->patch_count == 0 || plan->patch_count > APA_FORENSIC_MAX_PATCHES)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate forensic patch count");
    if (plan->patch_count >
        (0xffffffffu - SNAPSHOT_HEADER_BYTES - SNAPSHOT_TRAILER_BYTES) /
            SNAPSHOT_ENTRY_BYTES)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate HDDMETA image size");

    for (i = 0; i < plan->patch_count; i++) {
        unsigned int distance =
            apa_forensic_patch_bit_distance(&plan->patches[i]);
        if (distance == 1u || distance == 2u)
            one_or_two_bit_count++;
    }

    size = SNAPSHOT_HEADER_BYTES +
           plan->patch_count * SNAPSHOT_ENTRY_BYTES +
           SNAPSHOT_TRAILER_BYTES;
    image = malloc(size);
    if (image == NULL)
        return fail_snapshot(FORENSIC_SNAPSHOT_ALLOC_FAILED,
                             "allocate HDDMETA image");
    memset(image, 0, size);

    memcpy(image, snapshot_magic, sizeof(snapshot_magic));
    write_le32_snapshot(image + 8, FORENSIC_SNAPSHOT_VERSION);
    write_le32_snapshot(image + 12, result->total_sectors);
    write_le32_snapshot(image + 16, plan->map_index);
    write_le32_snapshot(image + 20, plan->confidence);
    write_le32_snapshot(image + 24, plan->patch_count);
    write_le32_snapshot(image + 28, plan->corroborated_count);
    write_le32_snapshot(image + 32, plan->speculative_count);
    /* Offset 36 was reserved in APAMETA1. Fill it with the number of patches
     * whose topology delta changes exactly one or two bits. The format stays
     * version 1 because the remaining header bytes were explicitly reserved
     * and existing development snapshots stored zero here. */
    write_le32_snapshot(image + 36, one_or_two_bit_count);

    offset = SNAPSHOT_HEADER_BYTES;
    for (i = 0; i < plan->patch_count; i++) {
        const apa_forensic_patch_t *patch = &plan->patches[i];
        const apa_forensic_node_t *node;
        unsigned char digest[32];

        if (patch->node_index >= result->node_count) {
            free(image);
            return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                                 "resolve patch node for HDDMETA");
        }
        node = &result->nodes[patch->node_index];
        if (node->lba != patch->lba) {
            free(image);
            return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                                 "match patch LBA to scanned node");
        }

        write_le32_snapshot(image + offset, patch->lba);
        sha256_buffer(node->header, APA_HEADER_SIZE, digest);
        memcpy(image + offset + 4, digest, sizeof(digest));
        memcpy(image + offset + 36, node->header, APA_HEADER_SIZE);
        offset += SNAPSHOT_ENTRY_BYTES;
    }

    sha256_buffer(image, size - SNAPSHOT_TRAILER_BYTES,
                  image + size - SNAPSHOT_TRAILER_BYTES);
    *image_out = image;
    *size_out = size;
    return 0;
}

int forensic_snapshot_save(unsigned int storage,
                           const apa_forensic_result_t *result,
                           const apa_forensic_repair_plan_t *plan,
                           char path_out[FORENSIC_SNAPSHOT_PATH_SIZE])
{
    unsigned char *image = NULL;
    unsigned char *verify = NULL;
    unsigned int image_size = 0;
    unsigned int slot;
    int result_code;

    if (storage >= STORAGE_TARGET_COUNT || result == NULL || plan == NULL ||
        path_out == NULL)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate HDDMETA snapshot arguments");

    result_code = build_snapshot_image(result, plan, &image, &image_size);
    if (result_code < 0)
        return result_code;
    verify = malloc(image_size);
    if (verify == NULL) {
        free(image);
        return fail_snapshot(FORENSIC_SNAPSHOT_ALLOC_FAILED,
                             "allocate HDDMETA read-back buffer");
    }

    for (slot = 0; slot < FORENSIC_SNAPSHOT_SLOT_COUNT; slot++) {
        char path[FORENSIC_SNAPSHOT_PATH_SIZE];
        iox_stat_t stat;
        int stat_result;

        storage_path(path, sizeof(path), storage, snapshot_names[slot]);
        memset(&stat, 0, sizeof(stat));
        stat_result = fileXioGetStat(path, &stat);
        if (stat_result >= 0) {
            if (stat.size == image_size &&
                read_exact_file(path, verify, (int)image_size) == 0 &&
                memcmp(verify, image, image_size) == 0) {
                strncpy(path_out, path, FORENSIC_SNAPSHOT_PATH_SIZE - 1u);
                path_out[FORENSIC_SNAPSHOT_PATH_SIZE - 1u] = '\0';
                free(verify);
                free(image);
                return 0;
            }
            continue;
        }

        if (write_whole_file(path, image, (int)image_size) < 0) {
            free(verify);
            free(image);
            return fail_snapshot(FORENSIC_SNAPSHOT_WRITE_FAILED,
                                 "write HDDMETA snapshot");
        }
        if (read_exact_file(path, verify, (int)image_size) < 0 ||
            memcmp(verify, image, image_size) != 0) {
            free(verify);
            free(image);
            return fail_snapshot(FORENSIC_SNAPSHOT_VERIFY_FAILED,
                                 "read back/compare HDDMETA snapshot");
        }

        strncpy(path_out, path, FORENSIC_SNAPSHOT_PATH_SIZE - 1u);
        path_out[FORENSIC_SNAPSHOT_PATH_SIZE - 1u] = '\0';
        free(verify);
        free(image);
        return 0;
    }

    free(verify);
    free(image);
    return fail_snapshot(FORENSIC_SNAPSHOT_NO_SLOT,
                         "select non-overwriting HDDMETA slot");
}
