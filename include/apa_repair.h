#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APA_REPAIR_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APA_REPAIR_H

#include <stdint.h>

#include "apa.h"

/* Independently observable faults in the 1024-byte APA master header. */
enum {
    APA_REPAIR_ISSUE_CHECKSUM = 1u << 0,
    APA_REPAIR_ISSUE_APA_MAGIC = 1u << 1,
    APA_REPAIR_ISSUE_MBR_ID = 1u << 2,
    APA_REPAIR_ISSUE_SONY_MAGIC = 1u << 3,
    APA_REPAIR_ISSUE_MASTER_START = 1u << 4,
    APA_REPAIR_ISSUE_MASTER_TYPE = 1u << 5,
    APA_REPAIR_ISSUE_MBR_VERSION = 1u << 6,
    APA_REPAIR_ISSUE_POINTER_INCONSISTENT = 1u << 7
};

/* Reasons the portable planner refuses to manufacture a sector-zero repair. */
enum {
    APA_REPAIR_BLOCKER_HYBRID_GPT = 1u << 0,
    APA_REPAIR_BLOCKER_LOW_IDENTITY = 1u << 1,
    APA_REPAIR_BLOCKER_NOT_MASTER = 1u << 2,
    APA_REPAIR_BLOCKER_UNEXPLAINED_CHECKSUM = 1u << 3
};

typedef struct {
    uint32_t issues;
    uint32_t safe_header_fixes;
    uint32_t blockers;
    unsigned int identity_matches;
    unsigned int master_anchor_matches;
    int header_patch_safe;
    int pointer_clear_recommended;
} apa_repair_plan_t;

/*
 * Inspect only fields whose canonical values are defined by the APA master
 * format. The planner never guesses partition-chain pointers, length,
 * passwords, timestamps, sub-partitions, or filesystem contents. A checksum
 * mismatch alone is diagnostic: without another known bad field (or an
 * external trusted backup) it cannot identify which protected word changed.
 */
int apa_repair_analyze(const unsigned char header[APA_HEADER_SIZE],
                       apa_repair_plan_t *plan);

/*
 * Build a repaired 1024-byte header in memory. Only plan.safe_header_fixes and
 * the checksum word may change. The function refuses blocked/ambiguous plans.
 */
int apa_repair_build_header(const unsigned char source[APA_HEADER_SIZE],
                            const apa_repair_plan_t *plan,
                            unsigned char repaired[APA_HEADER_SIZE]);

#endif
