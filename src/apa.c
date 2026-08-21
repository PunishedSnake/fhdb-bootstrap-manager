/*
 * Portable APA master-header parsing and identity checks.
 *
 * This is deliberately the read-only half of the future APA module. Raw HDD
 * transport and pointer updates remain in main.c until this logic has its own
 * regression coverage on both host and R5900 builds.
 */

#include "apa.h"

#include <string.h>

/* Read explicitly little-endian values without alignment assumptions. */
uint16_t read_le16(const unsigned char *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

uint32_t read_le32(const unsigned char *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

/* APA checksum: the sum of words 1..255, excluding the checksum word itself. */
uint32_t apa_checksum(const unsigned char *header)
{
    uint32_t sum = 0;
    unsigned int i;

    for (i = 1; i < 256; i++)
        sum += read_le32(header + (i * 4));
    return sum;
}

/* Reject anything that is not a normal, internally consistent APA master header. */
int is_standard_apa_header(const unsigned char *header)
{
    static const unsigned char apa_magic[4] = {0x41, 0x50, 0x41, 0x00};
    static const char mbr_id[] = "__mbr";
    static const char sce_magic[] = "Sony Computer Entertainment Inc.";

    if (memcmp(header + APA_MAGIC_OFFSET, apa_magic, sizeof(apa_magic)) != 0)
        return 0;
    if (memcmp(header + APA_ID_OFFSET, mbr_id, sizeof(mbr_id) - 1) != 0)
        return 0;
    if (memcmp(header + APA_MBR_MAGIC_OFFSET, sce_magic,
               sizeof(sce_magic) - 1) != 0)
        return 0;
    if (read_le32(header) != apa_checksum(header))
        return 0;
    return 1;
}

/* Hybrid APA/GPT disks use the conventional 0x55AA signature in sector zero. */
int is_hybrid_gpt(const unsigned char *header)
{
    return header[PC_MBR_SIGNATURE_OFFSET] == 0x55 &&
           header[PC_MBR_SIGNATURE_OFFSET + 1] == 0xaa;
}

/* Match a backup to this disk while ignoring checksum and mutable OSD fields. */
int headers_match_same_disk(const unsigned char *a, const unsigned char *b)
{
    const unsigned int mutable_end = APA_OSD_SIZE_OFFSET + 4u;

    /* Only checksum and osdStart/osdSize are intentionally mutable. Compare
       the two immutable spans directly so libc can use aligned word loads on
       the EE instead of executing a branch for every byte of the header. */
    return memcmp(a + 4u, b + 4u, APA_OSD_START_OFFSET - 4u) == 0 &&
           memcmp(a + mutable_end, b + mutable_end,
                  APA_HEADER_SIZE - mutable_end) == 0;
}
