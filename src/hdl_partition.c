/* Portable HDLoader partition layout and metadata serialization. */

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hdl_partition.h"

#define HDL_MIB (1024ull * 1024ull)
#define HDL_MAIN_DATA_OFFSET (4ull * HDL_MIB)
#define HDL_SUB_DATA_OFFSET 2048ull
#define HDL_INFO_MAGIC 0xDEADFEEDu
#define HDL_INFO_VERSION 1u

static const uint64_t allocation_sizes[] = {
    128ull * HDL_MIB,
    256ull * HDL_MIB,
    512ull * HDL_MIB,
    1024ull * HDL_MIB,
    2048ull * HDL_MIB,
    4096ull * HDL_MIB
};

static void write_le16(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static uint64_t choose_allocation(uint64_t required, uint64_t maximum)
{
    size_t i;

    for (i = 0; i < sizeof(allocation_sizes) / sizeof(allocation_sizes[0]); i++) {
        if (allocation_sizes[i] >= required &&
            allocation_sizes[i] <= maximum)
            return allocation_sizes[i];
    }
    for (i = sizeof(allocation_sizes) / sizeof(allocation_sizes[0]); i > 0; i--) {
        if (allocation_sizes[i - 1] <= maximum)
            return allocation_sizes[i - 1];
    }
    return 0;
}

int hdl_partition_plan(uint64_t image_bytes, uint32_t max_partition_sectors,
                       hdl_partition_plan_t *plan)
{
    uint64_t maximum = (uint64_t)max_partition_sectors * 512ull;
    uint64_t remaining = image_bytes;
    uint64_t payload_offset = 0;

    if (plan == NULL || image_bytes == 0)
        return HDL_PARTITION_INVALID_ARGUMENT;
    memset(plan, 0, sizeof(*plan));
    if (image_bytes % 2048ull != 0)
        return HDL_PARTITION_IMAGE_ALIGNMENT;
    if (maximum < allocation_sizes[0])
        return HDL_PARTITION_MAX_SIZE_INVALID;
    if (maximum > allocation_sizes[sizeof(allocation_sizes) /
                                   sizeof(allocation_sizes[0]) - 1])
        maximum = allocation_sizes[sizeof(allocation_sizes) /
                                   sizeof(allocation_sizes[0]) - 1];

    while (remaining != 0) {
        hdl_slice_plan_t *slice;
        uint64_t overhead = plan->count == 0 ? HDL_MAIN_DATA_OFFSET :
                                                HDL_SUB_DATA_OFFSET;
        uint64_t allocation;
        uint64_t capacity;
        uint64_t payload;

        if (plan->count >= HDL_MAX_PARTITIONS)
            return HDL_PARTITION_TOO_MANY;
        allocation = choose_allocation(remaining + overhead, maximum);
        if (allocation == 0) {
            allocation = choose_allocation(maximum, maximum);
            if (allocation == 0 || allocation <= overhead)
                return HDL_PARTITION_MAX_SIZE_INVALID;
        }
        capacity = allocation - overhead;
        payload = remaining < capacity ? remaining : capacity;
        if (payload > UINT32_MAX)
            return HDL_PARTITION_PHYSICAL_RANGE_INVALID;

        slice = &plan->slices[plan->count++];
        slice->allocation_bytes = allocation;
        slice->payload_offset = payload_offset;
        slice->payload_bytes = (uint32_t)payload;
        plan->allocation_bytes += allocation;
        payload_offset += payload;
        remaining -= payload;
    }
    plan->image_bytes = image_bytes;
    return 0;
}

static int copy_metadata_text(unsigned char *destination, size_t capacity,
                              const char *text)
{
    size_t length;

    if (text == NULL)
        return HDL_PARTITION_TEXT_INVALID;
    length = strlen(text);
    if (length == 0 || length >= capacity)
        return HDL_PARTITION_TEXT_INVALID;
    memcpy(destination, text, length);
    return 0;
}

int hdl_metadata_build(const hdl_partition_plan_t *plan,
                       const uint32_t *starts, unsigned int start_count,
                       const hdl_metadata_options_t *options,
                       unsigned char metadata[HDL_METADATA_SIZE])
{
    unsigned int i;

    if (plan == NULL || starts == NULL || options == NULL || metadata == NULL ||
        plan->count == 0 || plan->count > HDL_MAX_PARTITIONS ||
        start_count != plan->count)
        return HDL_PARTITION_INVALID_ARGUMENT;
    memset(metadata, 0, HDL_METADATA_SIZE);
    if (copy_metadata_text(metadata + 8, HDL_GAME_TITLE_MAX,
                           options->game_title) < 0 ||
        copy_metadata_text(metadata + 172, HDL_STARTUP_MAX,
                           options->startup) < 0)
        return HDL_PARTITION_TEXT_INVALID;

    write_le32(metadata, HDL_INFO_MAGIC);
    write_le16(metadata + 4, 0);
    write_le16(metadata + 6, HDL_INFO_VERSION);
    metadata[168] = options->hdl_compat_flags;
    metadata[169] = options->opl_compat_flags;
    metadata[170] = options->dma_type;
    metadata[171] = options->dma_mode;
    write_le32(metadata + 232, options->layer1_start);
    write_le32(metadata + 236, options->disc_type);
    write_le32(metadata + 240, plan->count);

    for (i = 0; i < plan->count; i++) {
        uint64_t data_start = starts[i] + (i == 0 ? 0x2000ull : 4ull);
        uint64_t part_offset = plan->slices[i].payload_offset / 2048ull;
        unsigned char *entry = metadata + 244 + i * 12;

        if (data_start > UINT32_MAX || part_offset > UINT32_MAX)
            return HDL_PARTITION_PHYSICAL_RANGE_INVALID;
        write_le32(entry, (uint32_t)part_offset);
        write_le32(entry + 4, (uint32_t)data_start);
        write_le32(entry + 8, plan->slices[i].payload_bytes);
    }
    return 0;
}

static int partition_char(unsigned char character)
{
    if (isalnum(character) || character == ' ' || character == '-' ||
        character == '_' || character == '.')
        return toupper(character);
    return '_';
}

int hdl_partition_id(const char *disc_id, const char *title,
                     char destination[HDL_PARTITION_ID_MAX])
{
    static const char prefix[] = "PP.";
    static const char middle[] = ".HDL.";
    size_t position = 0;
    size_t i;

    if (disc_id == NULL || title == NULL || destination == NULL ||
        strlen(disc_id) != 10 || title[0] == '\0')
        return HDL_PARTITION_TEXT_INVALID;
    memcpy(destination + position, prefix, sizeof(prefix) - 1);
    position += sizeof(prefix) - 1;
    for (i = 0; disc_id[i] != '\0' && position < 32; i++)
        destination[position++] = (char)partition_char((unsigned char)disc_id[i]);
    if (position + sizeof(middle) - 1 > 32)
        return HDL_PARTITION_TEXT_INVALID;
    memcpy(destination + position, middle, sizeof(middle) - 1);
    position += sizeof(middle) - 1;
    for (i = 0; title[i] != '\0' && position < 32; i++)
        destination[position++] = (char)partition_char((unsigned char)title[i]);
    while (position > 0 && destination[position - 1] == ' ')
        position--;
    destination[position] = '\0';
    return position > sizeof(prefix) + sizeof(middle) ? 0 :
           HDL_PARTITION_TEXT_INVALID;
}
