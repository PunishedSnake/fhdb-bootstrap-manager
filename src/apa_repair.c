/* Conservative, PS2SDK-free planning for repairable APA master-header faults. */

#include "apa_repair.h"

#include <string.h>

static const unsigned char canonical_apa_magic[4] = {'A', 'P', 'A', 0};
static const char canonical_mbr_id[] = "__mbr";
static const char canonical_sony_magic[] = "Sony Computer Entertainment Inc.";

static void write_le16_local(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32_local(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static unsigned int bit_count32(uint32_t value)
{
    unsigned int count = 0;

    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

int apa_repair_analyze(const unsigned char header[APA_HEADER_SIZE],
                       apa_repair_plan_t *plan)
{
    uint32_t identity_issues = 0;
    uint32_t anchor_issues = 0;
    uint32_t start;
    uint32_t size;

    if (header == NULL || plan == NULL)
        return -1;

    memset(plan, 0, sizeof(*plan));

    if (memcmp(header + APA_MAGIC_OFFSET, canonical_apa_magic,
               sizeof(canonical_apa_magic)) != 0)
        identity_issues |= APA_REPAIR_ISSUE_APA_MAGIC;
    else
        plan->identity_matches++;

    if (memcmp(header + APA_ID_OFFSET, canonical_mbr_id,
               sizeof(canonical_mbr_id) - 1) != 0)
        identity_issues |= APA_REPAIR_ISSUE_MBR_ID;
    else
        plan->identity_matches++;

    if (memcmp(header + APA_MBR_MAGIC_OFFSET, canonical_sony_magic,
               sizeof(canonical_sony_magic) - 1) != 0)
        identity_issues |= APA_REPAIR_ISSUE_SONY_MAGIC;
    else
        plan->identity_matches++;

    if (read_le32(header + APA_START_OFFSET) != 0)
        anchor_issues |= APA_REPAIR_ISSUE_MASTER_START;
    else
        plan->master_anchor_matches++;

    if (read_le16(header + APA_TYPE_OFFSET) != APA_MASTER_TYPE_VALUE)
        anchor_issues |= APA_REPAIR_ISSUE_MASTER_TYPE;
    else
        plan->master_anchor_matches++;

    if (read_le32(header + APA_MBR_VERSION_OFFSET) !=
        APA_MASTER_VERSION_VALUE)
        anchor_issues |= APA_REPAIR_ISSUE_MBR_VERSION;
    else
        plan->master_anchor_matches++;

    plan->issues = identity_issues | anchor_issues;
    if (read_le32(header) != apa_checksum(header))
        plan->issues |= APA_REPAIR_ISSUE_CHECKSUM;

    start = read_le32(header + APA_OSD_START_OFFSET);
    size = read_le32(header + APA_OSD_SIZE_OFFSET);
    if ((start == 0) != (size == 0)) {
        plan->issues |= APA_REPAIR_ISSUE_POINTER_INCONSISTENT;
        plan->pointer_clear_recommended = 1;
    }

    if (is_hybrid_gpt(header)) {
        plan->blockers |= APA_REPAIR_BLOCKER_HYBRID_GPT;
        return 0;
    }

    /*
     * Identity repair is accepted only when exactly one canonical identity
     * marker is damaged and all three independent master anchors agree.
     */
    if (identity_issues != 0) {
        if (plan->identity_matches >= 2 && plan->master_anchor_matches == 3 &&
            bit_count32(identity_issues) == 1) {
            plan->safe_header_fixes |= identity_issues;
        } else {
            plan->blockers |= APA_REPAIR_BLOCKER_LOW_IDENTITY;
        }
    }

    /*
     * A single damaged master anchor can be repaired only when all three
     * identity markers agree and the other two anchors still identify sector
     * zero as the APA MBR. Multiple damaged anchors are intentionally refused.
     */
    if (anchor_issues != 0) {
        if (plan->identity_matches == 3 && plan->master_anchor_matches >= 2 &&
            bit_count32(anchor_issues) == 1) {
            plan->safe_header_fixes |= anchor_issues;
        } else {
            plan->blockers |= APA_REPAIR_BLOCKER_NOT_MASTER;
        }
    }

    if ((plan->issues & APA_REPAIR_ISSUE_CHECKSUM) != 0 &&
        plan->blockers == 0)
        plan->safe_header_fixes |= APA_REPAIR_ISSUE_CHECKSUM;

    plan->header_patch_safe =
        plan->blockers == 0 && plan->safe_header_fixes != 0;
    return 0;
}

int apa_repair_build_header(const unsigned char source[APA_HEADER_SIZE],
                            const apa_repair_plan_t *plan,
                            unsigned char repaired[APA_HEADER_SIZE])
{
    uint32_t fixes;

    if (source == NULL || plan == NULL || repaired == NULL ||
        !plan->header_patch_safe || plan->blockers != 0)
        return -1;

    fixes = plan->safe_header_fixes;
    memcpy(repaired, source, APA_HEADER_SIZE);

    if ((fixes & APA_REPAIR_ISSUE_APA_MAGIC) != 0)
        memcpy(repaired + APA_MAGIC_OFFSET, canonical_apa_magic,
               sizeof(canonical_apa_magic));
    if ((fixes & APA_REPAIR_ISSUE_MBR_ID) != 0)
        memcpy(repaired + APA_ID_OFFSET, canonical_mbr_id,
               sizeof(canonical_mbr_id) - 1);
    if ((fixes & APA_REPAIR_ISSUE_SONY_MAGIC) != 0)
        memcpy(repaired + APA_MBR_MAGIC_OFFSET, canonical_sony_magic,
               sizeof(canonical_sony_magic) - 1);
    if ((fixes & APA_REPAIR_ISSUE_MASTER_START) != 0)
        write_le32_local(repaired + APA_START_OFFSET, 0);
    if ((fixes & APA_REPAIR_ISSUE_MASTER_TYPE) != 0)
        write_le16_local(repaired + APA_TYPE_OFFSET, APA_MASTER_TYPE_VALUE);
    if ((fixes & APA_REPAIR_ISSUE_MBR_VERSION) != 0)
        write_le32_local(repaired + APA_MBR_VERSION_OFFSET,
                         APA_MASTER_VERSION_VALUE);

    /* Every accepted header repair ends by rebuilding the checksum. */
    write_le32_local(repaired, apa_checksum(repaired));
    return 0;
}
