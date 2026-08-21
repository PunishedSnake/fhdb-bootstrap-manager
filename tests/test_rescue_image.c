/* Host-side validation tests for complete rescue-capsule images. */

#include "apa.h"
#include "capsule_format.h"
#include "rescue_image.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

#define TEST_FILE_SIZE \
    (RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE + 512u)

static void write_le32_test(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void make_standard_apa_header(unsigned char header[APA_HEADER_SIZE],
                                     uint32_t start, uint32_t sectors)
{
    static const unsigned char magic[4] = {'A', 'P', 'A', 0};
    static const char sce_magic[] = "Sony Computer Entertainment Inc.";

    memset(header, 0, APA_HEADER_SIZE);
    memcpy(header + APA_MAGIC_OFFSET, magic, sizeof(magic));
    memcpy(header + APA_ID_OFFSET, "__mbr", 5);
    memcpy(header + APA_MBR_MAGIC_OFFSET, sce_magic, sizeof(sce_magic) - 1);
    write_le32_test(header + APA_OSD_START_OFFSET, start);
    write_le32_test(header + APA_OSD_SIZE_OFFSET, sectors);
    write_le32_test(header, apa_checksum(header));
}

static unsigned int make_payload_capsule(unsigned char image[TEST_FILE_SIZE],
                                         rescue_capsule_info_t *source)
{
    unsigned char *header = image + RESCUE_CAPSULE_METADATA_SIZE;
    unsigned char *payload = header + APA_HEADER_SIZE;

    memset(source, 0, sizeof(*source));
    source->flags = RESCUE_CAPSULE_FLAG_VALID_APA |
                    RESCUE_CAPSULE_FLAG_HAS_PAYLOAD;
    source->payload_start = 0x2000u;
    source->payload_sectors = 1u;
    source->payload_bytes = 512u;
    memcpy(source->romver, "0220JC20060905", 14);
    memcpy(source->family, "unknown encrypted KELF", 22);
    memcpy(source->confidence, "low", 3);

    make_standard_apa_header(header, source->payload_start,
                             source->payload_sectors);
    memset(payload, 0x5a, source->payload_bytes);
    sha256_buffer(header, APA_HEADER_SIZE, source->apa_sha256);
    sha256_buffer(payload, source->payload_bytes, source->payload_sha256);
    rescue_capsule_encode(image, source);
    return TEST_FILE_SIZE;
}

static int test_valid_payload_image(void)
{
    unsigned char image[TEST_FILE_SIZE];
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned int size = make_payload_capsule(image, &source);

    if (rescue_image_validate(image, size, &decoded) != 0)
        return 0;
    return rescue_image_state_matches(&decoded, &source);
}

static int test_rejects_payload_hash_change(void)
{
    unsigned char image[TEST_FILE_SIZE];
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned int size = make_payload_capsule(image, &source);

    image[size - 1] ^= 1u;
    return rescue_image_validate(image, size, &decoded) ==
           RESCUE_IMAGE_PAYLOAD_HASH_MISMATCH;
}

static int test_rejects_apa_change(void)
{
    unsigned char image[TEST_FILE_SIZE];
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned int size = make_payload_capsule(image, &source);

    image[RESCUE_CAPSULE_METADATA_SIZE + 0x200u] ^= 1u;
    return rescue_image_validate(image, size, &decoded) ==
           RESCUE_IMAGE_APA_INVALID;
}

static int test_header_only_image(void)
{
    unsigned char image[RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE];
    unsigned char *header = image + RESCUE_CAPSULE_METADATA_SIZE;
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;

    memset(&source, 0, sizeof(source));
    source.flags = RESCUE_CAPSULE_FLAG_VALID_APA;
    make_standard_apa_header(header, 0, 0);
    sha256_buffer(header, APA_HEADER_SIZE, source.apa_sha256);
    rescue_capsule_encode(image, &source);

    return rescue_image_validate(image, sizeof(image), &decoded) == 0 &&
           decoded.flags == RESCUE_CAPSULE_FLAG_VALID_APA;
}

static int test_rejects_stale_capsule_version(void)
{
    unsigned char image[TEST_FILE_SIZE];
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned int size = make_payload_capsule(image, &source);

    /* Version is a little-endian u32 at metadata offset 8. */
    memset(image + 8, 0, 4);
    return rescue_image_validate(image, size, &decoded) == -192;
}

static int test_state_identity_contract(void)
{
    rescue_capsule_info_t a;
    rescue_capsule_info_t b;

    memset(&a, 0, sizeof(a));
    a.flags = RESCUE_CAPSULE_FLAG_VALID_APA |
              RESCUE_CAPSULE_FLAG_HAS_PAYLOAD;
    a.payload_start = 0x2000u;
    a.payload_sectors = 1u;
    a.payload_bytes = 512u;
    memset(a.apa_sha256, 0x11, sizeof(a.apa_sha256));
    memset(a.payload_sha256, 0x22, sizeof(a.payload_sha256));
    b = a;

    /* These fields were never part of protected-slot state identity. */
    b.kelf_file_bytes = 123u;
    memcpy(b.family, "different label", 15);
    if (!rescue_image_state_matches(&a, &b))
        return 0;

    b = a;
    b.payload_start++;
    if (rescue_image_state_matches(&a, &b))
        return 0;

    /* A capsule from another disk or another payload must never be reused. */
    b = a;
    b.apa_sha256[0] ^= 1u;
    if (rescue_image_state_matches(&a, &b))
        return 0;
    b = a;
    b.payload_sha256[0] ^= 1u;
    return !rescue_image_state_matches(&a, &b);
}

int main(void)
{
    if (!test_valid_payload_image()) {
        fprintf(stderr, "Valid rescue image was rejected.\n");
        return 1;
    }
    if (!test_rejects_payload_hash_change()) {
        fprintf(stderr, "Payload hash corruption was not rejected.\n");
        return 2;
    }
    if (!test_rejects_apa_change()) {
        fprintf(stderr, "APA corruption was not rejected.\n");
        return 3;
    }
    if (!test_header_only_image()) {
        fprintf(stderr, "Header-only rescue image was rejected.\n");
        return 4;
    }
    if (!test_rejects_stale_capsule_version()) {
        fprintf(stderr, "Stale rescue capsule version was not rejected.\n");
        return 5;
    }
    if (!test_state_identity_contract()) {
        fprintf(stderr, "Rescue slot identity contract changed.\n");
        return 6;
    }

    puts("All rescue image validation tests passed.");
    return 0;
}
