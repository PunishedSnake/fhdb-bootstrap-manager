/* Host-side tests for conservative APA master-header repair planning. */

#include "apa.h"
#include "apa_repair.h"

#include <stdio.h>
#include <string.h>

static void write_le16_test(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32_test(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void checksum_header(unsigned char header[APA_HEADER_SIZE])
{
    write_le32_test(header, apa_checksum(header));
}

static void make_master(unsigned char header[APA_HEADER_SIZE],
                        unsigned int osd_start, unsigned int osd_size)
{
    memset(header, 0, APA_HEADER_SIZE);
    memcpy(header + APA_MAGIC_OFFSET, "APA\0", 4);
    memcpy(header + APA_ID_OFFSET, "__mbr", 5);
    write_le32_test(header + APA_START_OFFSET, 0);
    write_le32_test(header + APA_LENGTH_OFFSET, 0x4000u);
    write_le16_test(header + APA_TYPE_OFFSET, APA_MASTER_TYPE_VALUE);
    memcpy(header + APA_MBR_MAGIC_OFFSET,
           "Sony Computer Entertainment Inc.", 32);
    write_le32_test(header + APA_MBR_VERSION_OFFSET,
                    APA_MASTER_VERSION_VALUE);
    write_le32_test(header + APA_OSD_START_OFFSET, osd_start);
    write_le32_test(header + APA_OSD_SIZE_OFFSET, osd_size);
    checksum_header(header);
}

static int build_and_require_standard(const unsigned char source[APA_HEADER_SIZE],
                                      const apa_repair_plan_t *plan)
{
    unsigned char repaired[APA_HEADER_SIZE];

    if (apa_repair_build_header(source, plan, repaired) != 0)
        return 0;
    return is_standard_apa_header(repaired) &&
           read_le32(repaired) == apa_checksum(repaired);
}

static int test_valid_header_has_no_repair(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0, 0);
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return plan.issues == 0 && plan.blockers == 0 &&
           !plan.header_patch_safe && !plan.pointer_clear_recommended;
}

static int test_checksum_only_is_refused(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0x2000u, 1u);
    header[0x220] ^= 1u;
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return plan.issues == APA_REPAIR_ISSUE_CHECKSUM &&
           plan.safe_header_fixes == 0 && !plan.header_patch_safe &&
           (plan.blockers & APA_REPAIR_BLOCKER_UNEXPLAINED_CHECKSUM) != 0;
}

static int test_known_bitflip_explains_checksum(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0, 0);
    /* Do not update checksum: this models a physical bit flip in a known field. */
    header[APA_MAGIC_OFFSET] ^= 1u;
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return (plan.issues & APA_REPAIR_ISSUE_APA_MAGIC) != 0 &&
           (plan.issues & APA_REPAIR_ISSUE_CHECKSUM) != 0 &&
           plan.header_patch_safe && plan.blockers == 0 &&
           build_and_require_standard(header, &plan);
}

static int test_known_plus_unknown_corruption_is_refused(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0, 0);
    header[APA_MAGIC_OFFSET] ^= 1u;
    header[0x220] ^= 1u;
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return !plan.header_patch_safe &&
           (plan.blockers & APA_REPAIR_BLOCKER_UNEXPLAINED_CHECKSUM) != 0;
}

static int test_single_identity_repairs(void)
{
    static const unsigned int offsets[] = {
        APA_MAGIC_OFFSET, APA_ID_OFFSET, APA_MBR_MAGIC_OFFSET
    };
    unsigned int i;

    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        unsigned char header[APA_HEADER_SIZE];
        unsigned char repaired[APA_HEADER_SIZE];
        apa_repair_plan_t plan;

        make_master(header, 0, 0);
        header[0x80] = 0x5a;
        header[offsets[i]] ^= 1u;
        checksum_header(header);
        if (apa_repair_analyze(header, &plan) != 0 ||
            !plan.header_patch_safe || plan.blockers != 0 ||
            apa_repair_build_header(header, &plan, repaired) != 0 ||
            !is_standard_apa_header(repaired) || repaired[0x80] != 0x5a)
            return 0;
    }
    return 1;
}

static int test_single_master_anchor_repairs(void)
{
    unsigned char header[APA_HEADER_SIZE];
    unsigned char repaired[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0, 0);
    write_le16_test(header + APA_TYPE_OFFSET, 0x1337u);
    checksum_header(header);
    if (apa_repair_analyze(header, &plan) != 0 ||
        !plan.header_patch_safe ||
        (plan.safe_header_fixes & APA_REPAIR_ISSUE_MASTER_TYPE) == 0 ||
        apa_repair_build_header(header, &plan, repaired) != 0 ||
        read_le16(repaired + APA_TYPE_OFFSET) != APA_MASTER_TYPE_VALUE ||
        !is_standard_apa_header(repaired))
        return 0;

    make_master(header, 0, 0);
    write_le32_test(header + APA_MBR_VERSION_OFFSET, 99u);
    checksum_header(header);
    if (apa_repair_analyze(header, &plan) != 0 ||
        !plan.header_patch_safe ||
        apa_repair_build_header(header, &plan, repaired) != 0 ||
        read_le32(repaired + APA_MBR_VERSION_OFFSET) !=
            APA_MASTER_VERSION_VALUE)
        return 0;

    make_master(header, 0, 0);
    write_le32_test(header + APA_START_OFFSET, 0x1234u);
    checksum_header(header);
    if (apa_repair_analyze(header, &plan) != 0 ||
        !plan.header_patch_safe ||
        apa_repair_build_header(header, &plan, repaired) != 0 ||
        read_le32(repaired + APA_START_OFFSET) != 0)
        return 0;

    return 1;
}

static int test_ambiguous_damage_is_refused(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0, 0);
    header[APA_MAGIC_OFFSET] ^= 1u;
    header[APA_ID_OFFSET] ^= 1u;
    checksum_header(header);
    if (apa_repair_analyze(header, &plan) != 0 ||
        plan.header_patch_safe ||
        (plan.blockers & APA_REPAIR_BLOCKER_LOW_IDENTITY) == 0)
        return 0;

    make_master(header, 0, 0);
    write_le32_test(header + APA_START_OFFSET, 1u);
    write_le16_test(header + APA_TYPE_OFFSET, 0x1337u);
    checksum_header(header);
    if (apa_repair_analyze(header, &plan) != 0 ||
        plan.header_patch_safe ||
        (plan.blockers & APA_REPAIR_BLOCKER_NOT_MASTER) == 0)
        return 0;

    return 1;
}

static int test_hybrid_is_never_repaired(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0, 0);
    header[PC_MBR_SIGNATURE_OFFSET] = 0x55;
    header[PC_MBR_SIGNATURE_OFFSET + 1] = 0xaa;
    checksum_header(header);
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return !plan.header_patch_safe &&
           (plan.blockers & APA_REPAIR_BLOCKER_HYBRID_GPT) != 0;
}

static int test_inconsistent_pointer_recommends_clear(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0x2000u, 0);
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return is_standard_apa_header(header) &&
           (plan.issues & APA_REPAIR_ISSUE_POINTER_INCONSISTENT) != 0 &&
           plan.pointer_clear_recommended && !plan.header_patch_safe;
}

static int test_torn_disable_is_diagnostic_only(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    make_master(header, 0x2000u, 1u);
    write_le32_test(header + APA_OSD_START_OFFSET, 0);
    write_le32_test(header + APA_OSD_SIZE_OFFSET, 0);
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return !plan.header_patch_safe && plan.safe_header_fixes == 0 &&
           (plan.blockers & APA_REPAIR_BLOCKER_UNEXPLAINED_CHECKSUM) != 0;
}

static int test_random_data_is_refused(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;
    unsigned int i;
    unsigned int state = 0x12345678u;

    for (i = 0; i < sizeof(header); i++) {
        state = state * 1103515245u + 12345u;
        header[i] = (unsigned char)(state >> 16);
    }
    if (apa_repair_analyze(header, &plan) != 0)
        return 0;
    return !plan.header_patch_safe && plan.blockers != 0;
}

int main(void)
{
    if (!test_valid_header_has_no_repair())
        return fprintf(stderr, "Valid APA header produced a repair plan.\n"), 1;
    if (!test_checksum_only_is_refused())
        return fprintf(stderr, "Checksum-only corruption was considered safe.\n"), 2;
    if (!test_known_bitflip_explains_checksum())
        return fprintf(stderr, "Known-field checksum evidence was not accepted.\n"), 3;
    if (!test_known_plus_unknown_corruption_is_refused())
        return fprintf(stderr, "Unexplained additional corruption was accepted.\n"), 4;
    if (!test_single_identity_repairs())
        return fprintf(stderr, "Canonical identity repair failed.\n"), 5;
    if (!test_single_master_anchor_repairs())
        return fprintf(stderr, "Master anchor repair failed.\n"), 6;
    if (!test_ambiguous_damage_is_refused())
        return fprintf(stderr, "Ambiguous APA damage was not refused.\n"), 7;
    if (!test_hybrid_is_never_repaired())
        return fprintf(stderr, "Hybrid/GPT repair blocker failed.\n"), 8;
    if (!test_inconsistent_pointer_recommends_clear())
        return fprintf(stderr, "Pointer repair recommendation failed.\n"), 9;
    if (!test_torn_disable_is_diagnostic_only())
        return fprintf(stderr, "Torn disable was unsafely auto-repaired.\n"), 10;
    if (!test_random_data_is_refused())
        return fprintf(stderr, "Random disk data was considered repairable.\n"), 11;

    puts("All conservative APA repair planner tests passed.");
    return 0;
}
