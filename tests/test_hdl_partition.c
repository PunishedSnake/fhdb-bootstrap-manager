#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hdl_partition.h"

#define MIB (1024ull * 1024ull)

static unsigned int read_le32(const unsigned char *source)
{
    return (unsigned int)source[0] | ((unsigned int)source[1] << 8) |
           ((unsigned int)source[2] << 16) |
           ((unsigned int)source[3] << 24);
}

static void test_small_image_uses_one_main(void)
{
    hdl_partition_plan_t plan;

    assert(hdl_partition_plan(100 * MIB, 0x800000, &plan) == 0);
    assert(plan.count == 1);
    assert(plan.image_bytes == 100 * MIB);
    assert(plan.allocation_bytes == 128 * MIB);
    assert(plan.slices[0].allocation_bytes == 128 * MIB);
    assert(plan.slices[0].payload_offset == 0);
    assert(plan.slices[0].payload_bytes == 100 * MIB);
}

static void test_large_image_is_split_without_gaps(void)
{
    hdl_partition_plan_t plan;
    uint64_t image_bytes = 5ull * 1024ull * MIB;

    assert(hdl_partition_plan(image_bytes, 0x800000, &plan) == 0);
    assert(plan.count == 2);
    assert(plan.slices[0].allocation_bytes == 4096 * MIB);
    assert(plan.slices[0].payload_bytes == 4092 * MIB);
    assert(plan.slices[1].payload_offset == 4092 * MIB);
    assert(plan.slices[1].payload_bytes == 1028 * MIB);
    assert(plan.slices[1].allocation_bytes == 2048 * MIB);
    assert(plan.allocation_bytes == 6144 * MIB);
}

static void test_max_partition_limit_and_overflow(void)
{
    hdl_partition_plan_t plan;
    uint64_t too_large = 66ull * (128ull * MIB - 2048ull);

    assert(hdl_partition_plan(2048, 0x10000, &plan) ==
           HDL_PARTITION_MAX_SIZE_INVALID);
    assert(hdl_partition_plan(123, 0x800000, &plan) ==
           HDL_PARTITION_IMAGE_ALIGNMENT);
    assert(hdl_partition_plan(too_large, 0x40000, &plan) ==
           HDL_PARTITION_TOO_MANY);
}

static void test_metadata_layout(void)
{
    hdl_partition_plan_t plan;
    hdl_metadata_options_t options = {
        "Example Game",
        "SLUS_123.45",
        0x14,
        0x123456,
        0x05,
        0x08,
        0,
        4
    };
    uint32_t starts[2] = {0x100000, 0x900000};
    unsigned char metadata[HDL_METADATA_SIZE];

    assert(hdl_partition_plan(5ull * 1024ull * MIB, 0x800000, &plan) == 0);
    assert(hdl_metadata_build(&plan, starts, 2, &options, metadata) == 0);
    assert(read_le32(metadata) == 0xDEADFEED);
    assert(memcmp(metadata + 8, "Example Game", 12) == 0);
    assert(metadata[168] == 0x05);
    assert(metadata[169] == 0x08);
    assert(metadata[171] == 4);
    assert(memcmp(metadata + 172, "SLUS_123.45", 11) == 0);
    assert(read_le32(metadata + 232) == 0x123456);
    assert(read_le32(metadata + 236) == 0x14);
    assert(read_le32(metadata + 240) == 2);
    assert(read_le32(metadata + 244) == 0);
    assert(read_le32(metadata + 248) == 0x102000);
    assert(read_le32(metadata + 252) == 4092 * MIB);
    assert(read_le32(metadata + 256) == (4092 * MIB) / 2048);
    assert(read_le32(metadata + 260) == 0x900004);
    assert(read_le32(metadata + 264) == 1028 * MIB);
}

static void test_partition_id_is_sanitized_and_bounded(void)
{
    char id[HDL_PARTITION_ID_MAX];

    assert(hdl_partition_id("SLUS-12345", "A very/long: game title!", id) == 0);
    assert(strlen(id) <= 32);
    assert(strncmp(id, "PP.SLUS-12345.HDL.", 18) == 0);
    assert(strchr(id, '/') == NULL);
    assert(strchr(id, ':') == NULL);
    assert(strchr(id, '!') == NULL);
}

int main(void)
{
    test_small_image_uses_one_main();
    test_large_image_is_split_without_gaps();
    test_max_partition_limit_and_overflow();
    test_metadata_layout();
    test_partition_id_is_sanitized_and_bounded();
    puts("All HDL partition planning tests passed.");
    return 0;
}
