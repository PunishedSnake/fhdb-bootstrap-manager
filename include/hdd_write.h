#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDD_WRITE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDD_WRITE_H

#include <tamtypes.h>
#include "apa.h"

/*
 * PS2-only write transport. These functions deliberately expose
 * individual transaction steps so caller policy must still perform
 * backup/confirmation and preserve payload-first/pointer-last order.
 */
int hdd_write_payload_verified(const unsigned char *payload,
                               unsigned int payload_size,
                               u32 start_sector);
int hdd_write_set_osd_mbr(u32 start, u32 size);
int hdd_write_verify_osd_mbr(unsigned char destination[APA_HEADER_SIZE],
                             u32 expected_start, u32 expected_size);

#endif
