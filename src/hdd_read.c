/*
 * PS2-specific, read-only raw HDD transport.
 *
 * This module deliberately owns only HDIOC_READSECTOR and read-only __mbr
 * bounds checks. It cannot write sectors, flush write caches, or change the
 * ROM bootstrap pointer. The two-sector transfer size is the same conservative
 * fileXio RPC size used by the Torii write-verification path.
 */

#include <fileXio_rpc.h>
#include <io_common.h>

#include <stdlib.h>
#include <string.h>

#include "hdd_limits.h"
#include "hdd_read.h"

#define HDIOC_READSECTOR_LOCAL 0x6836

typedef struct {
    unsigned int lba;
    unsigned int size;
} hdd_raw_transfer_t;

static unsigned char read_transfer_buffer[HDD_TRANSFER_BYTES]
    __attribute__((aligned(64)));

int hdd_read_raw_sectors(unsigned int lba, unsigned int sectors,
                         unsigned char *destination)
{
    hdd_raw_transfer_t transfer;

    transfer.lba = lba;
    transfer.size = sectors;
    memset(destination, 0, sectors * HDD_SECTOR_SIZE);
    return fileXioDevctl("hdd0:", HDIOC_READSECTOR_LOCAL,
                         &transfer, sizeof(transfer), destination,
                         sectors * HDD_SECTOR_SIZE);
}

int hdd_validate_payload_bounds(unsigned int start, unsigned int sectors)
{
    iox_stat_t mbr_status;
    int result;

    if (start == 0 || sectors == 0)
        return HDD_PAYLOAD_ERR_EMPTY_POINTER;
    if (sectors > HDD_MAX_MBR_PAYLOAD_SIZE / HDD_SECTOR_SIZE)
        return HDD_PAYLOAD_ERR_TOO_LARGE;
    if (start < HDD_MBR_PAYLOAD_START)
        return HDD_PAYLOAD_ERR_BEFORE_RESERVED_AREA;

    memset(&mbr_status, 0, sizeof(mbr_status));
    result = fileXioGetStat("hdd0:__mbr", &mbr_status);
    if (result < 0)
        return result;
    if (mbr_status.private_5 != 0 || start >= mbr_status.size ||
        sectors > mbr_status.size - start)
        return HDD_PAYLOAD_ERR_OUTSIDE_MBR;
    return 0;
}

int hdd_read_payload_image(unsigned int start, unsigned int sectors,
                           unsigned char **payload_out,
                           unsigned int *bytes_out)
{
    unsigned int bytes;
    unsigned int offset = 0;
    unsigned int sector_offset = 0;
    unsigned char *payload;

    if (sectors == 0 ||
        sectors > HDD_MAX_MBR_PAYLOAD_SIZE / HDD_SECTOR_SIZE)
        return HDD_PAYLOAD_ERR_IMAGE_SECTORS;

    bytes = sectors * HDD_SECTOR_SIZE;
    payload = malloc(bytes);
    if (payload == NULL)
        return HDD_PAYLOAD_ERR_ALLOC;

    while (sector_offset < sectors) {
        unsigned int chunk_sectors = sectors - sector_offset;
        int result;

        if (chunk_sectors > HDD_TRANSFER_SECTORS)
            chunk_sectors = HDD_TRANSFER_SECTORS;
        result = hdd_read_raw_sectors(start + sector_offset, chunk_sectors,
                                      read_transfer_buffer);
        if (result < 0) {
            free(payload);
            return result;
        }
        memcpy(payload + offset, read_transfer_buffer,
               chunk_sectors * HDD_SECTOR_SIZE);
        sector_offset += chunk_sectors;
        offset += chunk_sectors * HDD_SECTOR_SIZE;
    }

    *payload_out = payload;
    *bytes_out = bytes;
    return 0;
}
