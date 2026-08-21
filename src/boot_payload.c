/* Portable conversion from raw sector-image bytes into boot-chain evidence. */

#include "boot_payload.h"
#include "kelf.h"
#include "sha256.h"

#include <string.h>

void boot_payload_fingerprint(boot_chain_info_t *info,
                              const unsigned char *payload,
                              unsigned int payload_bytes)
{
    info->payload_bytes = 0;
    info->kelf_file_bytes = 0;
    info->payload_kelf_result = -1;
    memset(info->payload_sha256, 0, sizeof(info->payload_sha256));
    memset(info->kelf_sha256, 0, sizeof(info->kelf_sha256));

    if (payload == NULL || payload_bytes == 0)
        return;

    info->payload_bytes = payload_bytes;
    sha256_buffer(payload, payload_bytes, info->payload_sha256);
    info->payload_kelf_result =
        kelf_size_from_disk_image(payload, payload_bytes,
                                  &info->kelf_file_bytes);
    if (info->payload_kelf_result == KELF_IMAGE_VALID)
        sha256_buffer(payload, info->kelf_file_bytes, info->kelf_sha256);
}
