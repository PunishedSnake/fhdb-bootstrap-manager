#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_PARTITION_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_PARTITION_H

#include <stddef.h>
#include <stdint.h>

#define HDL_MAX_PARTITIONS 65u
#define HDL_METADATA_SIZE 1024u
/* Raw APA LBAs include the 4 KiB partition-information area that fileXio
 * hides before its extended-attribute view. HDLoader metadata is 1 MiB into
 * that file view, therefore the physical sector is main + (4096+1MiB)/512. */
#define HDL_METADATA_PHYSICAL_SECTOR_OFFSET 0x0808u
#define HDL_GAME_TITLE_MAX 160u
#define HDL_STARTUP_MAX 60u
#define HDL_PARTITION_ID_MAX 33u

enum {
    HDL_PARTITION_INVALID_ARGUMENT = -520,
    HDL_PARTITION_IMAGE_ALIGNMENT = -521,
    HDL_PARTITION_TOO_MANY = -522,
    HDL_PARTITION_MAX_SIZE_INVALID = -523,
    HDL_PARTITION_PHYSICAL_RANGE_INVALID = -524,
    HDL_PARTITION_TEXT_INVALID = -525,
    HDL_PARTITION_METADATA_INVALID = -526
};

typedef struct {
    uint64_t allocation_bytes;
    uint64_t payload_offset;
    uint32_t payload_bytes;
} hdl_slice_plan_t;

typedef struct {
    uint64_t image_bytes;
    uint64_t allocation_bytes;
    unsigned int count;
    hdl_slice_plan_t slices[HDL_MAX_PARTITIONS];
} hdl_partition_plan_t;

typedef struct {
    const char *game_title;
    const char *startup;
    uint32_t disc_type;
    uint32_t layer1_start;
    uint8_t hdl_compat_flags;
    uint8_t opl_compat_flags;
    uint8_t dma_type;
    uint8_t dma_mode;
} hdl_metadata_options_t;

typedef struct {
    char game_title[HDL_GAME_TITLE_MAX + 1u];
    char startup[HDL_STARTUP_MAX + 1u];
    uint32_t metadata_version;
    uint32_t disc_type;
    uint32_t layer1_start;
    unsigned int partition_count;
    uint8_t hdl_compat_flags;
    uint8_t opl_compat_flags;
    uint8_t dma_type;
    uint8_t dma_mode;
} hdl_metadata_info_t;

/* Plan APA main/sub allocations using the standard 128 MiB..4 GiB sizes. */
int hdl_partition_plan(uint64_t image_bytes, uint32_t max_partition_sectors,
                       hdl_partition_plan_t *plan);

/*
 * Serialize the canonical 1024-byte HDLoader metadata block. starts contains
 * physical 512-byte LBAs for the main partition followed by each sub.
 */
int hdl_metadata_build(const hdl_partition_plan_t *plan,
                       const uint32_t *starts, unsigned int start_count,
                       const hdl_metadata_options_t *options,
                       unsigned char metadata[HDL_METADATA_SIZE]);

/* Parse enough of an existing HDL metadata block for guarded management. */
int hdl_metadata_parse(const unsigned char metadata[HDL_METADATA_SIZE],
                       hdl_metadata_info_t *info);

/* Build an APA-safe PP.<disc>.HDL.<title> identifier, at most 32 bytes. */
int hdl_partition_id(const char *disc_id, const char *title,
                     char destination[HDL_PARTITION_ID_MAX]);

#endif
