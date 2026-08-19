#ifndef PS2_HDD_BOOTSTRAP_MANAGER_CAPSULE_FORMAT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_CAPSULE_FORMAT_H

/* Versioned, endian-stable metadata used by HDDRESCUE*.BIN files. */
#include <stddef.h>
#include <stdint.h>

#define RESCUE_CAPSULE_VERSION 1u
#define RESCUE_CAPSULE_METADATA_SIZE 256u
#define RESCUE_CAPSULE_APA_HEADER_SIZE 1024u
#define RESCUE_CAPSULE_FAMILY_SIZE 32u
#define RESCUE_CAPSULE_CONFIDENCE_SIZE 16u
#define RESCUE_CAPSULE_ROMVER_SIZE 16u

#define RESCUE_CAPSULE_FLAG_VALID_APA 0x00000001u
#define RESCUE_CAPSULE_FLAG_HAS_PAYLOAD 0x00000002u
#define RESCUE_CAPSULE_FLAG_VALID_KELF 0x00000004u

typedef struct {
    uint32_t flags;
    uint32_t payload_start;
    uint32_t payload_sectors;
    uint32_t payload_bytes;
    uint32_t kelf_file_bytes;
    unsigned char apa_sha256[32];
    unsigned char payload_sha256[32];
    char romver[RESCUE_CAPSULE_ROMVER_SIZE];
    char family[RESCUE_CAPSULE_FAMILY_SIZE];
    char confidence[RESCUE_CAPSULE_CONFIDENCE_SIZE];
} rescue_capsule_info_t;

void rescue_capsule_encode(unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE],
                           const rescue_capsule_info_t *info);
int rescue_capsule_decode(const unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE],
                          size_t complete_file_size,
                          rescue_capsule_info_t *info);

#endif
