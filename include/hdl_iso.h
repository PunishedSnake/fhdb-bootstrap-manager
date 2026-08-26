#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_ISO_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_ISO_H

#include <stddef.h>
#include <stdint.h>

#define HDL_ISO_SECTOR_SIZE 2048u
#define HDL_ISO_STARTUP_MAX 60u
#define HDL_ISO_DISC_ID_MAX 12u
#define HDL_ISO_TITLE_MAX 161u

enum {
    HDL_ISO_INVALID_ARGUMENT = -500,
    HDL_ISO_READ_FAILED = -501,
    HDL_ISO_INVALID_PVD = -502,
    HDL_ISO_INVALID_ROOT = -503,
    HDL_ISO_SYSTEM_CNF_MISSING = -504,
    HDL_ISO_SYSTEM_CNF_INVALID = -505,
    HDL_ISO_STARTUP_INVALID = -506,
    HDL_ISO_IMAGE_SIZE_INVALID = -507
};

typedef int (*hdl_iso_read_fn)(void *context, uint64_t offset,
                               void *destination, size_t size);

typedef struct {
    hdl_iso_read_fn read;
    void *context;
    uint64_t image_bytes;
} hdl_iso_source_t;

typedef struct {
    uint64_t image_bytes;
    uint32_t image_sectors;
    uint32_t layer1_start;
    uint32_t disc_type;
    int media_type_confident;
    int requires_layer_break;
    char startup[HDL_ISO_STARTUP_MAX];
    char disc_id[HDL_ISO_DISC_ID_MAX];
    char volume_title[HDL_ISO_TITLE_MAX];
} hdl_iso_info_t;

/*
 * Inspect an ISO9660 image without taking ownership of the source. The reader
 * must return zero only after filling the complete requested range.
 *
 * Small images cannot be distinguished reliably as CD or DVD from ISO9660
 * alone. They are reported as PS2 CD with media_type_confident == 0 so the PS2
 * front-end can require an explicit user choice. Images larger than a CD are
 * classified as PS2 DVD. DVD9-sized images additionally require a verified
 * layer break before an installer may commit HDL metadata.
 *
 * This probe runs once during install preflight and is dominated by bounded
 * ISO9660 parsing plus source reads, not steady-state payload transfer. Keep it
 * size-optimized so LTO does not spend I-cache on a one-shot control path.
 */
#if defined(__GNUC__)
#define HDL_ISO_PROBE_SIZE_OPT __attribute__((optimize("Os")))
#else
#define HDL_ISO_PROBE_SIZE_OPT
#endif

int HDL_ISO_PROBE_SIZE_OPT hdl_iso_probe(const hdl_iso_source_t *source,
                                         hdl_iso_info_t *info);

#undef HDL_ISO_PROBE_SIZE_OPT

#endif
