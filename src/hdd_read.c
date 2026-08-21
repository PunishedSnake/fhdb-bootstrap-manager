/*
 * PS2-specific, read-only raw HDD transport.
 *
 * This module deliberately owns only HDIOC_READSECTOR and live __mbr geometry
 * acquisition. Portable pointer/bounds policy lives in hdd_bounds.c. It cannot
 * write sectors, flush write caches, or change the ROM bootstrap pointer. The
 * two-sector transfer size is the same conservative fileXio RPC size used by
 * the Torii write-verification path.
 */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <stdlib.h>
#include <string.h>

#include "app_error.h"
#include "disk_status_ps2.h"
#include "hdd_bounds.h"
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
    disk_status_io(DISK_STATUS_READ, lba, sectors, 0, 0);
    memset(destination, 0, sectors * HDD_SECTOR_SIZE);
    return fileXioDevctl("hdd0:", HDIOC_READSECTOR_LOCAL,
                         &transfer, sizeof(transfer), destination,
                         sectors * HDD_SECTOR_SIZE);
}

int hdd_validate_payload_bounds(unsigned int start, unsigned int sectors)
{
    iox_stat_t mbr_status;
    int result;

    /* Preserve Torii's error precedence before asking the IOP for geometry. */
    result = hdd_validate_payload_shape(start, sectors);
    if (result < 0) {
        app_error_record(APP_ERROR_DOMAIN_HDD_BOUNDS, result,
                         "bootstrap pointer shape");
        return result;
    }

    memset(&mbr_status, 0, sizeof(mbr_status));
    result = fileXioGetStat("hdd0:__mbr", &mbr_status);
    if (result < 0) {
        app_error_record(APP_ERROR_DOMAIN_IOP, result,
                         "fileXioGetStat hdd0:__mbr");
        return result;
    }

    result = hdd_validate_payload_bounds_geometry(
        start, sectors, (unsigned int)mbr_status.private_5,
        (unsigned int)mbr_status.size);
    if (result < 0)
        app_error_record(APP_ERROR_DOMAIN_HDD_BOUNDS, result,
                         "live __mbr geometry");
    return result;
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

    disk_status_begin("Bootstrap payload read",
                      "Reading active bootstrap payload");
    while (sector_offset < sectors) {
        unsigned int chunk_sectors = sectors - sector_offset;
        int result;

        if (chunk_sectors > HDD_TRANSFER_SECTORS)
            chunk_sectors = HDD_TRANSFER_SECTORS;
        disk_status_io(DISK_STATUS_READ, start + sector_offset,
                       chunk_sectors, sector_offset, sectors);
        result = hdd_read_raw_sectors(start + sector_offset, chunk_sectors,
                                      read_transfer_buffer);
        if (result < 0) {
            app_error_record(APP_ERROR_DOMAIN_IOP, result,
                             "read active bootstrap payload");
            disk_status_end();
            free(payload);
            return result;
        }
        memcpy(payload + offset, read_transfer_buffer,
               chunk_sectors * HDD_SECTOR_SIZE);
        sector_offset += chunk_sectors;
        offset += chunk_sectors * HDD_SECTOR_SIZE;
    }
    disk_status_end();

    *payload_out = payload;
    *bytes_out = bytes;
    return 0;
}
