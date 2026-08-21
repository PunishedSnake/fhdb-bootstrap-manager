#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDD_FORENSIC_REPAIR_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDD_FORENSIC_REPAIR_PS2_H

#include "apa_forensic.h"

enum {
    HDD_FORENSIC_REPAIR_INVALID_ARGUMENT = -370,
    HDD_FORENSIC_REPAIR_PLAN_BLOCKED = -371,
    HDD_FORENSIC_REPAIR_SOURCE_CHANGED = -372,
    HDD_FORENSIC_REPAIR_PATCH_INVALID = -373,
    HDD_FORENSIC_REPAIR_WRITE_FAILED = -374,
    HDD_FORENSIC_REPAIR_FLUSH_FAILED = -375,
    HDD_FORENSIC_REPAIR_READBACK_FAILED = -376,
    HDD_FORENSIC_REPAIR_COMPARE_FAILED = -377
};

/*
 * Apply an already-authorized topology-only plan. The writer refuses plans not
 * marked manual_allowed, proves every source header is unchanged since the
 * scan, writes non-master headers before LBA 0, flushes and compares every
 * write, then verifies the complete patch set again. Callers must create and
 * verify HDDMETA*.BIN before entering this function and must restart afterward.
 */
int hdd_forensic_repair_apply_verified(
    const apa_forensic_result_t *scan,
    const apa_forensic_repair_plan_t *plan);

#endif