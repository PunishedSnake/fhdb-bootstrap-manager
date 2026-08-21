#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDD_REPAIR_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDD_REPAIR_PS2_H

#include "apa.h"

enum {
    HDD_REPAIR_INVALID_ARGUMENT = -330,
    HDD_REPAIR_UNSAFE_HEADER = -331,
    HDD_REPAIR_WRITE_FAILED = -332,
    HDD_REPAIR_FLUSH_FAILED = -333,
    HDD_REPAIR_READBACK_FAILED = -334,
    HDD_REPAIR_COMPARE_FAILED = -335
};

/*
 * Dangerous by design and intentionally narrow: write exactly the repaired
 * 1024-byte APA master header to sectors 0-1, flush, then compare exact bytes.
 * No general sector-zero write primitive is exposed outside this module.
 */
int hdd_repair_write_master_header_verified(
    const unsigned char repaired[APA_HEADER_SIZE],
    unsigned char readback[APA_HEADER_SIZE]);

#endif
