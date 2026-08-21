#ifndef PS2_HDD_BOOTSTRAP_MANAGER_RESCUE_IMAGE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_RESCUE_IMAGE_H

#include <stddef.h>

#include "capsule_format.h"

enum {
    RESCUE_IMAGE_TOO_SMALL = -181,
    RESCUE_IMAGE_APA_INVALID = -182,
    RESCUE_IMAGE_PAYLOAD_HASH_MISMATCH = -183,
    RESCUE_IMAGE_KELF_MISMATCH = -184
};

/*
 * Validate one complete HDDRESCUE*.BIN image entirely in memory. The return
 * values preserve Torii's historical rescue diagnostics. Decode failures are
 * mapped exactly as -190 + rescue_capsule_decode()'s negative result.
 */
int rescue_image_validate(const unsigned char *data, unsigned int size,
                          rescue_capsule_info_t *info);

/*
 * Compare the state fields historically used to decide whether a protected
 * rescue slot already contains the current state. Descriptive strings and the
 * derived unpadded KELF byte count are deliberately not part of slot identity.
 */
int rescue_image_state_matches(const rescue_capsule_info_t *existing,
                               const rescue_capsule_info_t *expected);

#endif
