#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDD_READ_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDD_READ_H

#include "hdd_bounds.h"

/* Stable read/allocation errors; negative PS2SDK/IOP errors are forwarded. */
enum {
    HDD_PAYLOAD_ERR_IMAGE_SECTORS = -174,
    HDD_PAYLOAD_ERR_ALLOC = -175
};

/*
 * Read-only raw HDD helpers. Callers of hdd_read_raw_sectors() must provide an
 * EE buffer suitable for fileXio DMA and large enough for sectors * 512 bytes.
 * No function in this interface can write sectors or update APA pointers.
 */
int hdd_read_raw_sectors(unsigned int lba, unsigned int sectors,
                         unsigned char *destination);
int hdd_validate_payload_bounds(unsigned int start, unsigned int sectors);
int hdd_read_payload_image(unsigned int start, unsigned int sectors,
                           unsigned char **payload_out,
                           unsigned int *bytes_out);

#endif