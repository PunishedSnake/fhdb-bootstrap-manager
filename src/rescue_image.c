/* Portable validation of complete HDD rescue-capsule images. */

#include <string.h>

#include "apa.h"
#include "kelf.h"
#include "rescue_image.h"
#include "sha256.h"

int rescue_image_validate(const unsigned char *data, unsigned int size,
                          rescue_capsule_info_t *info)
{
    unsigned char digest[32];
    const unsigned char *saved_header;
    const unsigned char *saved_payload;
    int result;

    if (data == NULL || info == NULL ||
        size < RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE)
        return RESCUE_IMAGE_TOO_SMALL;

    result = rescue_capsule_decode(data, size, info);
    if (result < 0)
        return -190 + result;

    saved_header = data + RESCUE_CAPSULE_METADATA_SIZE;
    saved_payload = saved_header + APA_HEADER_SIZE;
    sha256_buffer(saved_header, APA_HEADER_SIZE, digest);
    if (memcmp(digest, info->apa_sha256, sizeof(digest)) != 0 ||
        !is_standard_apa_header(saved_header) ||
        (info->flags & RESCUE_CAPSULE_FLAG_VALID_APA) == 0)
        return RESCUE_IMAGE_APA_INVALID;

    if ((info->flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) != 0) {
        unsigned int kelf_bytes = 0;

        sha256_buffer(saved_payload, info->payload_bytes, digest);
        if (memcmp(digest, info->payload_sha256, sizeof(digest)) != 0)
            return RESCUE_IMAGE_PAYLOAD_HASH_MISMATCH;
        if ((info->flags & RESCUE_CAPSULE_FLAG_VALID_KELF) != 0 &&
            (kelf_size_from_disk_image(saved_payload, info->payload_bytes,
                                       &kelf_bytes) < 0 ||
             kelf_bytes != info->kelf_file_bytes))
            return RESCUE_IMAGE_KELF_MISMATCH;
    }

    return 0;
}

int rescue_image_state_matches(const rescue_capsule_info_t *existing,
                               const rescue_capsule_info_t *expected)
{
    if (existing == NULL || expected == NULL)
        return 0;

    return existing->flags == expected->flags &&
           existing->payload_start == expected->payload_start &&
           existing->payload_sectors == expected->payload_sectors &&
           existing->payload_bytes == expected->payload_bytes &&
           memcmp(existing->apa_sha256, expected->apa_sha256, 32) == 0 &&
           memcmp(existing->payload_sha256,
                  expected->payload_sha256, 32) == 0;
}
