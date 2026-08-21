/* Synthetic host-side fixtures for the portable KELF structural parser. */

#include "kelf.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_CAPACITY 2048u

#define TEST_ELF_SIZE_OFFSET 0x10u
#define TEST_HEADER_SIZE_OFFSET 0x14u
#define TEST_FLAGS_OFFSET 0x18u
#define TEST_BIT_COUNT_OFFSET 0x1au

static void write_le16_test(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32_test(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static unsigned int make_kelf(unsigned char *data,
                              uint16_t header_size,
                              uint32_t elf_size,
                              uint16_t flags,
                              uint16_t bit_count)
{
    memset(data, 0, FIXTURE_CAPACITY);
    write_le32_test(data + TEST_ELF_SIZE_OFFSET, elf_size);
    write_le16_test(data + TEST_HEADER_SIZE_OFFSET, header_size);
    write_le16_test(data + TEST_FLAGS_OFFSET, flags);
    write_le16_test(data + TEST_BIT_COUNT_OFFSET, bit_count);
    return (unsigned int)header_size + (unsigned int)elf_size;
}

static int test_valid_layouts(void)
{
    unsigned char data[FIXTURE_CAPACITY];
    unsigned int size;
    unsigned int variable_offset;

    /* Common low-layout KELF: fixed header + 8-byte area + 32-byte key area. */
    size = make_kelf(data, 72u, 16u, 0u, 0u);
    if (kelf_validate_layout(data, size) != KELF_VALID)
        return 0;

    /* High layout flag omits the additional eight-byte area. */
    size = make_kelf(data, 64u, 16u, 0x1000u, 0u);
    if (kelf_validate_layout(data, size) != KELF_VALID)
        return 0;

    /* Flag bit 0 adds a length-prefixed variable section after the BIT table. */
    size = make_kelf(data, 76u, 16u, 1u, 0u);
    variable_offset = KELF_FIXED_HEADER_SIZE;
    data[variable_offset] = 3u; /* Four bytes including this length byte. */
    if (kelf_validate_layout(data, size) != KELF_VALID)
        return 0;

    /* Exercise the public PS2SDK maximum of 63 BIT entries. */
    size = make_kelf(data, 1080u, 16u, 0u, KELF_MAX_BIT_COUNT);
    return kelf_validate_layout(data, size) == KELF_VALID;
}

static int test_rejection_codes(void)
{
    unsigned char data[FIXTURE_CAPACITY];
    unsigned int size;

    memset(data, 0, sizeof(data));
    if (kelf_validate_layout(data, KELF_FIXED_HEADER_SIZE - 1u) !=
        KELF_ERR_TOO_SMALL)
        return 0;

    size = make_kelf(data, 72u, 16u, 0u, 0u);
    data[0] = 0x7f;
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    if (kelf_validate_layout(data, size) != KELF_ERR_PLAIN_ELF)
        return 0;

    size = make_kelf(data, 31u, 16u, 0u, 0u);
    if (kelf_validate_layout(data, size) != KELF_ERR_HEADER_SIZE)
        return 0;

    size = make_kelf(data, 72u, 16u, 0u, 0u);
    write_le16_test(data + TEST_HEADER_SIZE_OFFSET, 100u);
    if (kelf_validate_layout(data, size) != KELF_ERR_HEADER_SIZE)
        return 0;

    size = make_kelf(data, 72u, 16u, 0u, 64u);
    if (kelf_validate_layout(data, size) != KELF_ERR_BIT_COUNT)
        return 0;

    size = make_kelf(data, 72u, 0u, 0u, 0u);
    if (kelf_validate_layout(data, size) != KELF_ERR_FILE_SIZE)
        return 0;

    size = make_kelf(data, 72u, 16u, 0u, 0u);
    if (kelf_validate_layout(data, size - 1u) != KELF_ERR_FILE_SIZE)
        return 0;

    /* Three BIT blocks cannot fit inside this 64-byte header. */
    size = make_kelf(data, 64u, 16u, 0x1000u, 3u);
    if (kelf_validate_layout(data, size) != KELF_ERR_BIT_TABLE)
        return 0;

    /* Two BIT blocks end exactly at header_size, leaving no length byte. */
    size = make_kelf(data, 64u, 16u, 1u, 2u);
    if (kelf_validate_layout(data, size) != KELF_ERR_VARIABLE_SECTION)
        return 0;

    /* A low-layout 64-byte header is eight bytes too short for the key area. */
    size = make_kelf(data, 64u, 16u, 0u, 0u);
    return kelf_validate_layout(data, size) == KELF_ERR_KEY_AREA;
}

static int test_sector_padded_image(void)
{
    unsigned char data[FIXTURE_CAPACITY];
    unsigned int file_bytes = 0;
    unsigned int size;

    size = make_kelf(data, 72u, 16u, 0u, 0u);
    memset(data + size, 0xa5, 512u - size);
    if (kelf_size_from_disk_image(data, 512u, &file_bytes) !=
            KELF_IMAGE_VALID ||
        file_bytes != size)
        return 0;

    if (kelf_size_from_disk_image(data, KELF_FIXED_HEADER_SIZE - 1u,
                                  &file_bytes) != KELF_IMAGE_ERR_TOO_SMALL)
        return 0;

    size = make_kelf(data, 72u, 16u, 0u, 0u);
    write_le32_test(data + TEST_ELF_SIZE_OFFSET, 600u);
    if (kelf_size_from_disk_image(data, 512u, &file_bytes) !=
        KELF_IMAGE_ERR_SIZE_FIELDS)
        return 0;

    memset(data, 0, sizeof(data));
    if (kelf_size_from_disk_image(data, 512u, &file_bytes) !=
        KELF_IMAGE_ERR_CALCULATED_SIZE)
        return 0;

    size = make_kelf(data, 72u, 16u, 0u, 64u);
    (void)size;
    return kelf_size_from_disk_image(data, 512u, &file_bytes) ==
           KELF_IMAGE_ERR_LAYOUT;
}

int main(void)
{
    if (!test_valid_layouts()) {
        fprintf(stderr, "Valid KELF layout fixtures failed.\n");
        return 1;
    }
    if (!test_rejection_codes()) {
        fprintf(stderr, "Malformed KELF rejection fixtures failed.\n");
        return 2;
    }
    if (!test_sector_padded_image()) {
        fprintf(stderr, "Sector-padded KELF image fixtures failed.\n");
        return 3;
    }

    puts("All portable KELF parser tests passed.");
    return 0;
}
