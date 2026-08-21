/* Portable payload-pointer and __mbr geometry validation. */

#include "hdd_bounds.h"
#include "hdd_limits.h"

int hdd_validate_payload_shape(unsigned int start, unsigned int sectors)
{
    if (start == 0 || sectors == 0)
        return HDD_PAYLOAD_ERR_EMPTY_POINTER;
    if (sectors > HDD_MAX_MBR_PAYLOAD_SIZE / HDD_SECTOR_SIZE)
        return HDD_PAYLOAD_ERR_TOO_LARGE;
    if (start < HDD_MBR_PAYLOAD_START)
        return HDD_PAYLOAD_ERR_BEFORE_RESERVED_AREA;
    return 0;
}

int hdd_validate_payload_bounds_geometry(unsigned int start,
                                         unsigned int sectors,
                                         unsigned int mbr_start,
                                         unsigned int mbr_size)
{
    int result = hdd_validate_payload_shape(start, sectors);

    if (result < 0)
        return result;
    if (mbr_start != 0 || start >= mbr_size ||
        sectors > mbr_size - start)
        return HDD_PAYLOAD_ERR_OUTSIDE_MBR;
    return 0;
}
