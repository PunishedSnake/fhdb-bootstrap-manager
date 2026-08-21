#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HEADER_BACKUP_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HEADER_BACKUP_H

#include <tamtypes.h>

#include "apa.h"

#define HEADER_BACKUP_SLOT_COUNT 2u
#define HEADER_BACKUP_PATH_SIZE 64u
#define HEADER_BACKUP_NOT_TRIED 999999
#define HEADER_BACKUP_OCCUPIED 999998

typedef struct {
    int read_result[HEADER_BACKUP_SLOT_COUNT];
    int write_result[HEADER_BACKUP_SLOT_COUNT];
    int verify_result[HEADER_BACKUP_SLOT_COUNT];
    char path[HEADER_BACKUP_SLOT_COUNT][HEADER_BACKUP_PATH_SIZE];
} header_backup_diagnostics_t;

/*
 * Save or reuse an exact non-overwriting APA-header backup on one selected
 * storage target. Returns the persistent diagnostics path on success or NULL.
 * This module never accesses raw HDD write interfaces.
 */
const char *header_backup_save(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    header_backup_diagnostics_t *diagnostics);

/*
 * Locate the first compatible enabled header backup using current filenames
 * first and v0.1.x FHDB names second. Same-disk matching and non-zero pointer
 * checks happen before start/size are returned.
 */
int header_backup_find_enabled(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    char *found_path, unsigned int path_capacity,
    u32 *start_out, u32 *size_out);

#endif
