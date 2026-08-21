#ifndef PS2_HDD_BOOTSTRAP_MANAGER_RESCUE_STORAGE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_RESCUE_STORAGE_H

#include "apa.h"
#include "capsule_format.h"

#define RESCUE_STORAGE_SLOT_COUNT 2u
#define RESCUE_STORAGE_PATH_SIZE 64u

enum {
    RESCUE_STORAGE_NOT_FOUND = -200,
    RESCUE_STORAGE_WRONG_DISK = -201,
    RESCUE_STORAGE_HEADER_ONLY = -202,
    RESCUE_STORAGE_INVALID_KELF = -203
};

typedef struct {
    rescue_capsule_info_t info;
    unsigned char *file_data;
    unsigned int file_size;
    char path[RESCUE_STORAGE_PATH_SIZE];
} rescue_storage_entry_t;

/*
 * Save the current APA header and, when enabled, the complete active payload
 * into a protected HDDRESCUE*.BIN slot. Existing non-matching files are never
 * overwritten. Returns a persistent path on success or NULL.
 */
const char *rescue_storage_save_current(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    const char *romver, const char *family, const char *confidence);

/*
 * Find the first valid, payload-bearing, valid-KELF capsule belonging to the
 * same disk. Absence, wrong-disk/damaged state, header-only state, and invalid
 * KELF remain distinguishable through the historical -200..-203 codes.
 */
int rescue_storage_find(
    unsigned int storage,
    const unsigned char current_header[APA_HEADER_SIZE],
    rescue_storage_entry_t *entry);

/* Return the payload bytes inside a successfully loaded entry. */
const unsigned char *rescue_storage_payload(
    const rescue_storage_entry_t *entry);

/* Release a loaded entry returned by rescue_storage_find(). */
void rescue_storage_entry_release(rescue_storage_entry_t *entry);

#endif
