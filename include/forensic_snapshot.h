#ifndef PS2_HDD_BOOTSTRAP_MANAGER_FORENSIC_SNAPSHOT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_FORENSIC_SNAPSHOT_H

#include "apa_forensic.h"

#define FORENSIC_SNAPSHOT_PATH_SIZE 64u
#define FORENSIC_SNAPSHOT_SLOT_COUNT 2u
#define FORENSIC_SNAPSHOT_VERSION 1u

enum {
    FORENSIC_SNAPSHOT_INVALID_ARGUMENT = -360,
    FORENSIC_SNAPSHOT_ALLOC_FAILED = -361,
    FORENSIC_SNAPSHOT_NO_SLOT = -362,
    FORENSIC_SNAPSHOT_WRITE_FAILED = -363,
    FORENSIC_SNAPSHOT_VERIFY_FAILED = -364
};

/*
 * Save every original 1024-byte APA header touched by a forensic topology
 * repair. The versioned file includes per-header SHA-256 digests and a digest
 * of the complete preceding image, then is read back byte-for-byte before any
 * HDD metadata write is allowed.
 */
int forensic_snapshot_save(unsigned int storage,
                           const apa_forensic_result_t *result,
                           const apa_forensic_repair_plan_t *plan,
                           char path_out[FORENSIC_SNAPSHOT_PATH_SIZE]);

#endif