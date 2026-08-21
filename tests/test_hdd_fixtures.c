/* Host-side tests for sparse synthetic HDD images generated from known cases. */

#include "apa.h"
#include "hdd_bounds.h"
#include "hdd_limits.h"
#include "kelf.h"

#include <stdio.h>
#include <string.h>

#define NO_CHECK 999999

typedef struct {
    const char *name;
    int standard;
    int hybrid;
    int gpt;
    unsigned int start;
    unsigned int sectors;
    unsigned int mbr_size;
    int bounds;
    int kelf;
    unsigned int payload_lba;
} fixture_case_t;

static const fixture_case_t cases[] = {
    {"valid_disabled", 1, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"valid_enabled", 1, 0, 0, 0x2000, 1, 0x4000,
     0, KELF_IMAGE_VALID, 0x2000},
    {"garbage_payload", 1, 0, 0, 0x2000, 1, 0x4000,
     0, KELF_IMAGE_ERR_CALCULATED_SIZE, 0x2000},
    {"bad_checksum", 0, 0, 0, 0x2000, 1, 0x4000, 0, NO_CHECK, 0},
    {"bad_apa_magic", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bad_mbr_id", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bad_sony_magic", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bitflip_apa_magic", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bitflip_mbr_id", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bitflip_sony_magic", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bad_master_start", 1, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bad_master_type", 1, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bad_mbr_version", 1, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bitflip_master_start", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bitflip_master_type", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"bitflip_mbr_version", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"pointer_start_only", 1, 0, 0, 0x2000, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"pointer_size_only", 1, 0, 0, 0, 1, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"pointer_before_reserved", 1, 0, 0, 0x1000, 1, 0x4000,
     HDD_PAYLOAD_ERR_BEFORE_RESERVED_AREA, NO_CHECK, 0},
    {"pointer_too_large", 1, 0, 0, 0x2000, 8193, 0x6000,
     HDD_PAYLOAD_ERR_TOO_LARGE, NO_CHECK, 0},
    {"pointer_outside_mbr", 1, 0, 0, 0x3fff, 2, 0x4000,
     HDD_PAYLOAD_ERR_OUTSIDE_MBR, NO_CHECK, 0},
    {"apa_pc_signature_only", 1, 1, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"hybrid_apa_gpt", 1, 1, 1, 0x2000, 1, 0x4000,
     0, KELF_IMAGE_VALID, 0x2000},
    {"gpt_only", 0, 1, 1, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0},
    {"deterministic_garbage", 0, 0, 0, 0, 0, 0, NO_CHECK, NO_CHECK, 0},
    {"interrupted_payload_written_pointer_zero", 1, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, KELF_IMAGE_VALID, 0x2000},
    {"interrupted_partial_payload_pointer_zero", 1, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, KELF_IMAGE_ERR_SIZE_FIELDS, 0x2000},
    {"enabled_zeroed_payload", 1, 0, 0, 0x2000, 1, 0x4000,
     0, KELF_IMAGE_ERR_CALCULATED_SIZE, 0x2000},
    {"enabled_partial_overwrite", 1, 0, 0, 0x2000, 1, 0x4000,
     0, KELF_IMAGE_ERR_SIZE_FIELDS, 0x2000},
    {"torn_disable_stale_checksum", 0, 0, 0, 0, 0, 0x4000,
     HDD_PAYLOAD_ERR_EMPTY_POINTER, NO_CHECK, 0}
};

static int read_fixture(const char *directory, const char *name, long offset,
                        unsigned char *buffer, size_t size)
{
    char path[512];
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s.raw", directory, name);
    file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    if (fseek(file, offset, SEEK_SET) != 0) {
        fclose(file);
        return -2;
    }
    if (fread(buffer, 1, size, file) != size) {
        fclose(file);
        return -3;
    }
    fclose(file);
    return 0;
}

static int run_case(const char *directory, const fixture_case_t *test)
{
    unsigned char header[APA_HEADER_SIZE];
    unsigned char payload[HDD_SECTOR_SIZE];
    unsigned int file_bytes = 0;
    int result;

    if (read_fixture(directory, test->name, 0, header, sizeof(header)) < 0) {
        fprintf(stderr, "%s: could not read generated image.\n", test->name);
        return 0;
    }
    if (is_standard_apa_header(header) != test->standard) {
        fprintf(stderr, "%s: APA classification mismatch.\n", test->name);
        return 0;
    }
    if (is_hybrid_gpt(header) != test->hybrid) {
        fprintf(stderr, "%s: protective-MBR classification mismatch.\n",
                test->name);
        return 0;
    }
    result = memcmp(header + HDD_SECTOR_SIZE, "EFI PART", 8) == 0;
    if (result != test->gpt) {
        fprintf(stderr, "%s: GPT-header presence mismatch.\n", test->name);
        return 0;
    }

    if (test->bounds != NO_CHECK) {
        if (read_le32(header + APA_OSD_START_OFFSET) != test->start ||
            read_le32(header + APA_OSD_SIZE_OFFSET) != test->sectors) {
            fprintf(stderr, "%s: OSD pointer fields mismatch.\n", test->name);
            return 0;
        }
        result = hdd_validate_payload_bounds_geometry(
            test->start, test->sectors, 0, test->mbr_size);
        if (result != test->bounds) {
            fprintf(stderr, "%s: bounds code %d, expected %d.\n",
                    test->name, result, test->bounds);
            return 0;
        }
    }

    if (test->kelf != NO_CHECK) {
        if (read_fixture(directory, test->name,
                         (long)test->payload_lba * HDD_SECTOR_SIZE,
                         payload, sizeof(payload)) < 0) {
            fprintf(stderr, "%s: payload read failed.\n", test->name);
            return 0;
        }
        result = kelf_size_from_disk_image(payload, sizeof(payload), &file_bytes);
        if (result != test->kelf) {
            fprintf(stderr, "%s: KELF result %d, expected %d.\n",
                    test->name, result, test->kelf);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : "tests/generated_hdds";
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (!run_case(directory, &cases[i]))
            return 1;
    }
    printf("All %u synthetic HDD fixture cases passed.\n",
           (unsigned int)(sizeof(cases) / sizeof(cases[0])));
    return 0;
}
