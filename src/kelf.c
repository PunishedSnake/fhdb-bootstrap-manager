/*
 * Portable PS2 KELF structural parsing.
 *
 * The former implementation lived in main.c and cast the input buffer directly
 * to PS2SDK's SecrKELFHeader_t. That is fine on the little-endian EE, but it
 * couples a pure format check to target headers and native struct layout. This
 * module decodes the same fields by byte offset so host tests exercise exactly
 * the bytes that would be read from a file or from sector-aligned HDD storage.
 *
 * This is deliberately not a KELF decryptor or signer. It only rejects obvious
 * plain ELFs, impossible size relationships, malformed BIT tables, and header
 * layouts that cannot contain the key material expected by secrman.
 */

#include "kelf.h"

#include <stdint.h>

#define KELF_ELF_SIZE_OFFSET 0x10u
#define KELF_HEADER_SIZE_OFFSET 0x14u
#define KELF_FLAGS_OFFSET 0x18u
#define KELF_BIT_COUNT_OFFSET 0x1au
#define KELF_OPTIONAL_AREA_BYTES 8u
#define KELF_KEY_AREA_BYTES 32u

static uint16_t kelf_read_le16(const unsigned char *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

static uint32_t kelf_read_le32(const unsigned char *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

int kelf_validate_layout(const unsigned char *data, unsigned int size)
{
    uint32_t elf_size;
    uint16_t header_size;
    uint16_t flags;
    uint16_t bit_count;
    unsigned int offset;

    if (size < KELF_FIXED_HEADER_SIZE)
        return KELF_ERR_TOO_SMALL;

    /* A renamed ordinary ELF must never reach the MagicGate signing path. */
    if (data[0] == 0x7f && data[1] == 'E' &&
        data[2] == 'L' && data[3] == 'F')
        return KELF_ERR_PLAIN_ELF;

    elf_size = kelf_read_le32(data + KELF_ELF_SIZE_OFFSET);
    header_size = kelf_read_le16(data + KELF_HEADER_SIZE_OFFSET);
    flags = kelf_read_le16(data + KELF_FLAGS_OFFSET);
    bit_count = kelf_read_le16(data + KELF_BIT_COUNT_OFFSET);

    if (header_size < KELF_FIXED_HEADER_SIZE || header_size > size)
        return KELF_ERR_HEADER_SIZE;
    if (bit_count > KELF_MAX_BIT_COUNT)
        return KELF_ERR_BIT_COUNT;

    /* The supplied buffer must be exactly one unpadded KELF file. */
    if (elf_size == 0 || elf_size > size - header_size ||
        (unsigned int)header_size + elf_size != size)
        return KELF_ERR_FILE_SIZE;

    /* Every BIT entry occupies the 16-byte SecrBitBlockData_t wire layout. */
    offset = KELF_FIXED_HEADER_SIZE +
             ((unsigned int)bit_count * KELF_BIT_BLOCK_SIZE);
    if (offset > header_size)
        return KELF_ERR_BIT_TABLE;

    /*
     * Flag bit 0 inserts a length-prefixed variable section immediately after
     * the BIT entries. The length byte itself is included in the advance.
     */
    if ((flags & 1u) != 0) {
        if (offset >= header_size)
            return KELF_ERR_VARIABLE_SECTION;
        offset += (unsigned int)data[offset] + 1u;
    }

    /* Low-layout KELFs carry one additional eight-byte header area. */
    if ((flags & 0xf000u) == 0)
        offset += KELF_OPTIONAL_AREA_BYTES;

    /* secrman expects the remaining fixed key/check area inside the header. */
    if (offset > header_size ||
        KELF_KEY_AREA_BYTES > (unsigned int)header_size - offset)
        return KELF_ERR_KEY_AREA;

    return KELF_VALID;
}

int kelf_size_from_disk_image(const unsigned char *data,
                              unsigned int disk_bytes,
                              unsigned int *file_bytes)
{
    uint32_t elf_size;
    uint16_t header_size;
    unsigned int calculated;

    if (disk_bytes < KELF_FIXED_HEADER_SIZE)
        return KELF_IMAGE_ERR_TOO_SMALL;

    elf_size = kelf_read_le32(data + KELF_ELF_SIZE_OFFSET);
    header_size = kelf_read_le16(data + KELF_HEADER_SIZE_OFFSET);

    /* Check subtraction before addition so malformed u32 sizes cannot wrap. */
    if (elf_size > disk_bytes || header_size > disk_bytes - elf_size)
        return KELF_IMAGE_ERR_SIZE_FIELDS;

    calculated = (unsigned int)header_size + (unsigned int)elf_size;
    if (calculated == 0 || calculated > disk_bytes)
        return KELF_IMAGE_ERR_CALCULATED_SIZE;

    /* Validate only the real KELF bytes; sector padding is intentionally ignored. */
    if (kelf_validate_layout(data, calculated) < 0)
        return KELF_IMAGE_ERR_LAYOUT;

    *file_bytes = calculated;
    return KELF_IMAGE_VALID;
}
