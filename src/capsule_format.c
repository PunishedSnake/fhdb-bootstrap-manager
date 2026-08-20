/*
 * Serialization for the PS2 HDD Bootstrap Manager rescue capsule.
 *
 * Explicit offsets and little-endian integers keep the backup format stable
 * across compiler revisions and make it straightforward to inspect on a PC.
 */

#include "capsule_format.h"

#include <string.h>

#define OFFSET_MAGIC 0u
#define OFFSET_VERSION 8u
#define OFFSET_METADATA_SIZE 12u
#define OFFSET_COMPLETE_SIZE 16u
#define OFFSET_FLAGS 20u
#define OFFSET_PAYLOAD_START 24u
#define OFFSET_PAYLOAD_SECTORS 28u
#define OFFSET_PAYLOAD_BYTES 32u
#define OFFSET_APA_BYTES 36u
#define OFFSET_APA_SHA256 40u
#define OFFSET_PAYLOAD_SHA256 72u
#define OFFSET_ROMVER 104u
#define OFFSET_FAMILY 120u
#define OFFSET_CONFIDENCE 152u
#define OFFSET_KELF_FILE_BYTES 168u

static const unsigned char capsule_magic[8] = {
    'P', 'S', '2', 'H', 'B', 'R', 'C', '\0'
};

static uint32_t read_le32_local(const unsigned char *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static void write_le32_local(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

void rescue_capsule_encode(unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE],
                           const rescue_capsule_info_t *info)
{
    uint32_t complete_size = RESCUE_CAPSULE_METADATA_SIZE +
                             RESCUE_CAPSULE_APA_HEADER_SIZE +
                             info->payload_bytes;

    memset(metadata, 0, RESCUE_CAPSULE_METADATA_SIZE);
    memcpy(metadata + OFFSET_MAGIC, capsule_magic, sizeof(capsule_magic));
    write_le32_local(metadata + OFFSET_VERSION, RESCUE_CAPSULE_VERSION);
    write_le32_local(metadata + OFFSET_METADATA_SIZE,
                     RESCUE_CAPSULE_METADATA_SIZE);
    write_le32_local(metadata + OFFSET_COMPLETE_SIZE, complete_size);
    write_le32_local(metadata + OFFSET_FLAGS, info->flags);
    write_le32_local(metadata + OFFSET_PAYLOAD_START, info->payload_start);
    write_le32_local(metadata + OFFSET_PAYLOAD_SECTORS,
                     info->payload_sectors);
    write_le32_local(metadata + OFFSET_PAYLOAD_BYTES, info->payload_bytes);
    write_le32_local(metadata + OFFSET_APA_BYTES,
                     RESCUE_CAPSULE_APA_HEADER_SIZE);
    memcpy(metadata + OFFSET_APA_SHA256, info->apa_sha256, 32);
    memcpy(metadata + OFFSET_PAYLOAD_SHA256, info->payload_sha256, 32);
    memcpy(metadata + OFFSET_ROMVER, info->romver,
           RESCUE_CAPSULE_ROMVER_SIZE);
    memcpy(metadata + OFFSET_FAMILY, info->family,
           RESCUE_CAPSULE_FAMILY_SIZE);
    memcpy(metadata + OFFSET_CONFIDENCE, info->confidence,
           RESCUE_CAPSULE_CONFIDENCE_SIZE);
    write_le32_local(metadata + OFFSET_KELF_FILE_BYTES,
                     info->kelf_file_bytes);
}

int rescue_capsule_decode(const unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE],
                          size_t complete_file_size,
                          rescue_capsule_info_t *info)
{
    const uint32_t known_flags = RESCUE_CAPSULE_FLAG_VALID_APA |
                                 RESCUE_CAPSULE_FLAG_HAS_PAYLOAD |
                                 RESCUE_CAPSULE_FLAG_VALID_KELF;
    uint32_t recorded_size;
    uint32_t payload_bytes;
    uint32_t payload_sectors;

    if (memcmp(metadata + OFFSET_MAGIC, capsule_magic,
               sizeof(capsule_magic)) != 0)
        return -1;
    if (read_le32_local(metadata + OFFSET_VERSION) != RESCUE_CAPSULE_VERSION)
        return -2;
    if (read_le32_local(metadata + OFFSET_METADATA_SIZE) !=
        RESCUE_CAPSULE_METADATA_SIZE)
        return -3;
    if (read_le32_local(metadata + OFFSET_APA_BYTES) !=
        RESCUE_CAPSULE_APA_HEADER_SIZE)
        return -4;

    payload_bytes = read_le32_local(metadata + OFFSET_PAYLOAD_BYTES);
    payload_sectors = read_le32_local(metadata + OFFSET_PAYLOAD_SECTORS);
    if (payload_sectors > UINT32_MAX / 512u ||
        payload_bytes != payload_sectors * 512u)
        return -5;
    if (payload_bytes > UINT32_MAX - RESCUE_CAPSULE_METADATA_SIZE -
                        RESCUE_CAPSULE_APA_HEADER_SIZE)
        return -6;

    recorded_size = read_le32_local(metadata + OFFSET_COMPLETE_SIZE);
    if (recorded_size != RESCUE_CAPSULE_METADATA_SIZE +
                         RESCUE_CAPSULE_APA_HEADER_SIZE + payload_bytes ||
        complete_file_size != recorded_size)
        return -7;

    memset(info, 0, sizeof(*info));
    info->flags = read_le32_local(metadata + OFFSET_FLAGS);
    if ((info->flags & ~known_flags) != 0)
        return -8;
    info->payload_start = read_le32_local(metadata + OFFSET_PAYLOAD_START);
    info->payload_sectors = payload_sectors;
    info->payload_bytes = payload_bytes;
    info->kelf_file_bytes =
        read_le32_local(metadata + OFFSET_KELF_FILE_BYTES);
    if (info->kelf_file_bytes > info->payload_bytes)
        return -9;
    if ((info->flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) == 0 &&
        (info->payload_start != 0 || info->payload_sectors != 0 ||
         info->payload_bytes != 0))
        return -10;
    if ((info->flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) != 0 &&
        (info->payload_start == 0 || info->payload_sectors == 0))
        return -11;
    if ((info->flags & RESCUE_CAPSULE_FLAG_VALID_KELF) != 0 &&
        ((info->flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) == 0 ||
         info->kelf_file_bytes == 0))
        return -12;
    if ((info->flags & RESCUE_CAPSULE_FLAG_VALID_KELF) == 0 &&
        info->kelf_file_bytes != 0)
        return -13;

    memcpy(info->apa_sha256, metadata + OFFSET_APA_SHA256, 32);
    memcpy(info->payload_sha256, metadata + OFFSET_PAYLOAD_SHA256, 32);
    memcpy(info->romver, metadata + OFFSET_ROMVER,
           RESCUE_CAPSULE_ROMVER_SIZE);
    memcpy(info->family, metadata + OFFSET_FAMILY,
           RESCUE_CAPSULE_FAMILY_SIZE);
    memcpy(info->confidence, metadata + OFFSET_CONFIDENCE,
           RESCUE_CAPSULE_CONFIDENCE_SIZE);
    info->romver[RESCUE_CAPSULE_ROMVER_SIZE - 1] = '\0';
    info->family[RESCUE_CAPSULE_FAMILY_SIZE - 1] = '\0';
    info->confidence[RESCUE_CAPSULE_CONFIDENCE_SIZE - 1] = '\0';
    return 0;
}
