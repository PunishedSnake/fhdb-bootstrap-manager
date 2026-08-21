#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_PAYLOAD_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_PAYLOAD_H

#include "boot_chain.h"

/*
 * Fill only the payload-derived evidence fields from one sector-aligned image.
 * The function has no PS2SDK dependency and performs no allocation or I/O.
 */
void boot_payload_fingerprint(boot_chain_info_t *info,
                              const unsigned char *payload,
                              unsigned int payload_bytes);

#endif
