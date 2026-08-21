#ifndef PS2_HDD_BOOTSTRAP_MANAGER_REPAIR_SNAPSHOT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_REPAIR_SNAPSHOT_H

#include "apa.h"

#define REPAIR_SNAPSHOT_PATH_SIZE 64u
#define REPAIR_SNAPSHOT_SLOT_COUNT 2u

enum {
    REPAIR_SNAPSHOT_INVALID_ARGUMENT = -320,
    REPAIR_SNAPSHOT_NO_SLOT = -321,
    REPAIR_SNAPSHOT_WRITE_FAILED = -322,
    REPAIR_SNAPSHOT_VERIFY_FAILED = -323
};

/*
 * Save the exact pre-repair 1024 bytes without requiring them to be valid APA.
 * Slots are non-overwriting; an identical existing snapshot may be reused.
 */
int repair_snapshot_save(unsigned int storage,
                         const unsigned char header[APA_HEADER_SIZE],
                         char path_out[REPAIR_SNAPSHOT_PATH_SIZE]);

#endif
