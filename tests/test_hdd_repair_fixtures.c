/* Exercise diagnosis and successful repair postconditions for every raw fixture. */

#include "apa.h"
#include "apa_repair.h"
#include "hdd_bounds.h"
#include "hdd_limits.h"
#include "kelf.h"
#include "repair_health.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    EXPECT_NO_REPAIR = 0,
    EXPECT_HEADER_REPAIR,
    EXPECT_POINTER_CLEAR,
    EXPECT_BLOCKED
} expected_repair_t;

typedef struct {
    const char *name;
    expected_repair_t expected;
    unsigned int mbr_size;
    int inspect_active_payload;
} repair_case_t;

static const repair_case_t cases[] = {
    {"valid_disabled", EXPECT_NO_REPAIR, 0x4000, 0},
    {"valid_enabled", EXPECT_NO_REPAIR, 0x4000, 1},
    {"garbage_payload", EXPECT_POINTER_CLEAR, 0x4000, 1},
    {"bad_checksum", EXPECT_BLOCKED, 0x4000, 0},
    {"bad_apa_magic", EXPECT_BLOCKED, 0x4000, 0},
    {"bad_mbr_id", EXPECT_BLOCKED, 0x4000, 0},
    {"bad_sony_magic", EXPECT_BLOCKED, 0x4000, 0},
    {"bitflip_apa_magic", EXPECT_HEADER_REPAIR, 0x4000, 0},
    {"bitflip_mbr_id", EXPECT_HEADER_REPAIR, 0x4000, 0},
    {"bitflip_sony_magic", EXPECT_HEADER_REPAIR, 0x4000, 0},
    {"bad_master_start", EXPECT_BLOCKED, 0x4000, 0},
    {"bad_master_type", EXPECT_BLOCKED, 0x4000, 0},
    {"bad_mbr_version", EXPECT_BLOCKED, 0x4000, 0},
    {"bitflip_master_start", EXPECT_HEADER_REPAIR, 0x4000, 0},
    {"bitflip_master_type", EXPECT_HEADER_REPAIR, 0x4000, 0},
    {"bitflip_mbr_version", EXPECT_HEADER_REPAIR, 0x4000, 0},
    {"pointer_start_only", EXPECT_POINTER_CLEAR, 0x4000, 0},
    {"pointer_size_only", EXPECT_POINTER_CLEAR, 0x4000, 0},
    {"pointer_before_reserved", EXPECT_POINTER_CLEAR, 0x4000, 0},
    {"pointer_too_large", EXPECT_POINTER_CLEAR, 0x6000, 0},
    {"pointer_outside_mbr", EXPECT_POINTER_CLEAR, 0x4000, 0},
    {"apa_pc_signature_only", EXPECT_BLOCKED, 0x4000, 0},
    {"hybrid_apa_gpt", EXPECT_BLOCKED, 0x4000, 1},
    {"gpt_only", EXPECT_BLOCKED, 0x4000, 0},
    {"deterministic_garbage", EXPECT_BLOCKED, 0, 0},
    {"interrupted_payload_written_pointer_zero", EXPECT_NO_REPAIR, 0x4000, 0},
    {"interrupted_partial_payload_pointer_zero", EXPECT_NO_REPAIR, 0x4000, 0},
    {"enabled_zeroed_payload", EXPECT_POINTER_CLEAR, 0x4000, 1},
    {"enabled_partial_overwrite", EXPECT_POINTER_CLEAR, 0x4000, 1},
    {"torn_disable_stale_checksum", EXPECT_BLOCKED, 0x4000, 0}
};

static int read_bytes(const char *directory, const char *name, long offset,
                      unsigned char *data, size_t size)
{
    char path[512];
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s.raw", directory, name);
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    if (fseek(file, offset, SEEK_SET) != 0 ||
        fread(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static void write_le32_test(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static int master_anchors_match(const unsigned char header[APA_HEADER_SIZE])
{
    return read_le32(header + APA_START_OFFSET) == 0 &&
           read_le16(header + APA_TYPE_OFFSET) == APA_MASTER_TYPE_VALUE &&
           read_le32(header + APA_MBR_VERSION_OFFSET) == APA_MASTER_VERSION_VALUE;
}

static int pointer_clear_postcondition(
    const unsigned char source[APA_HEADER_SIZE])
{
    unsigned char cleared[APA_HEADER_SIZE];
    unsigned int i;

    memcpy(cleared, source, sizeof(cleared));
    write_le32_test(cleared + APA_OSD_START_OFFSET, 0);
    write_le32_test(cleared + APA_OSD_SIZE_OFFSET, 0);
    write_le32_test(cleared, apa_checksum(cleared));

    if (!is_standard_apa_header(cleared) || !master_anchors_match(cleared) ||
        is_hybrid_gpt(cleared) ||
        read_le32(cleared + APA_OSD_START_OFFSET) != 0 ||
        read_le32(cleared + APA_OSD_SIZE_OFFSET) != 0)
        return 0;

    for (i = 0; i < APA_HEADER_SIZE; i++) {
        if (i < 4 ||
            (i >= APA_OSD_START_OFFSET && i < APA_OSD_SIZE_OFFSET + 4))
            continue;
        if (cleared[i] != source[i])
            return 0;
    }
    return 1;
}

static expected_repair_t classify_case(const char *directory,
                                       const repair_case_t *test,
                                       int *detail_out)
{
    unsigned char header[APA_HEADER_SIZE];
    repair_health_t health;
    boot_chain_info_t evidence;
    unsigned int start;
    unsigned int sectors;
    int bounds_result = 0;
    int result;

    *detail_out = 0;
    if (!read_bytes(directory, test->name, 0, header, sizeof(header))) {
        *detail_out = -1;
        return EXPECT_BLOCKED;
    }

    memset(&evidence, 0, sizeof(evidence));
    start = read_le32(header + APA_OSD_START_OFFSET);
    sectors = read_le32(header + APA_OSD_SIZE_OFFSET);
    if (start != 0 && sectors != 0)
        bounds_result = hdd_validate_payload_bounds_geometry(
            start, sectors, 0, test->mbr_size);

    if (test->inspect_active_payload && start != 0 && sectors != 0 &&
        bounds_result >= 0) {
        unsigned char payload[HDD_SECTOR_SIZE];
        unsigned int file_bytes = 0;

        if (!read_bytes(directory, test->name,
                        (long)start * HDD_SECTOR_SIZE,
                        payload, sizeof(payload))) {
            evidence.payload_read_result = -3;
        } else {
            evidence.payload_read_result = 0;
            evidence.payload_kelf_result =
                kelf_size_from_disk_image(payload, sizeof(payload), &file_bytes);
        }
    }

    result = repair_health_assess(
        header, test->inspect_active_payload ? &evidence : NULL,
        bounds_result, &health);
    if (result < 0) {
        *detail_out = result;
        return EXPECT_BLOCKED;
    }

    if (health.header_plan.header_patch_safe) {
        unsigned char repaired[APA_HEADER_SIZE];

        if (apa_repair_build_header(
                header, &health.header_plan, repaired) != 0 ||
            !is_standard_apa_header(repaired) ||
            !master_anchors_match(repaired) || is_hybrid_gpt(repaired)) {
            *detail_out = -2;
            return EXPECT_BLOCKED;
        }
        return EXPECT_HEADER_REPAIR;
    }

    if (health.header_plan.blockers != 0) {
        unsigned char forbidden[APA_HEADER_SIZE];

        if (apa_repair_build_header(
                header, &health.header_plan, forbidden) == 0) {
            *detail_out = -4;
            return EXPECT_HEADER_REPAIR;
        }
        return EXPECT_BLOCKED;
    }

    if (health.pointer_clear_recommended) {
        if (!pointer_clear_postcondition(header)) {
            *detail_out = -5;
            return EXPECT_BLOCKED;
        }
        return EXPECT_POINTER_CLEAR;
    }

    return EXPECT_NO_REPAIR;
}

static const char *action_name(expected_repair_t action)
{
    switch (action) {
        case EXPECT_NO_REPAIR: return "no repair";
        case EXPECT_HEADER_REPAIR: return "header repair";
        case EXPECT_POINTER_CLEAR: return "pointer clear";
        case EXPECT_BLOCKED: return "blocked";
        default: return "unknown";
    }
}

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : "tests/generated_hdds";
    unsigned int counts[4] = {0, 0, 0, 0};
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int detail = 0;
        expected_repair_t actual = classify_case(directory, &cases[i], &detail);

        if (actual != cases[i].expected) {
            fprintf(stderr,
                    "%s: repair action %s, expected %s (detail %d).\n",
                    cases[i].name, action_name(actual),
                    action_name(cases[i].expected), detail);
            return 1;
        }
        counts[(unsigned int)actual]++;
    }

    printf("All %u generated HDD repair-policy cases passed with repair "
           "postconditions: %u no-repair, %u header-repair, "
           "%u pointer-clear, %u blocked.\n",
           (unsigned int)(sizeof(cases) / sizeof(cases[0])),
           counts[EXPECT_NO_REPAIR], counts[EXPECT_HEADER_REPAIR],
           counts[EXPECT_POINTER_CLEAR], counts[EXPECT_BLOCKED]);
    return 0;
}
