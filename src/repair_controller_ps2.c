/*
 * Guarded PS2 presentation/controller for deterministic APA recovery.
 *
 * Canonical header-repair policy lives in portable apa_repair.c and mounted-
 * disk pointer-health policy lives in portable repair_health.c. This module
 * owns only the confirmation screens, snapshot gate, and routing to the narrow
 * PS2 raw-header writer or the established pointer-disable workflow.
 */

#include <tamtypes.h>
#include <debug.h>
#include <libpad.h>

#include <string.h>

#include "apa.h"
#include "apa_repair.h"
#include "boot_chain.h"
#include "disk_status_ps2.h"
#include "hdd_read.h"
#include "hdd_repair_ps2.h"
#include "platform.h"
#include "repair_controller_ps2.h"
#include "repair_health.h"
#include "repair_snapshot.h"
#include "session_log.h"
#include "storage.h"

static void print_plan(const apa_repair_plan_t *plan)
{
    if ((plan->issues & APA_REPAIR_ISSUE_CHECKSUM) != 0)
        scr_printf("- APA checksum mismatch\n");
    if ((plan->issues & APA_REPAIR_ISSUE_APA_MAGIC) != 0)
        scr_printf("- APA magic damaged\n");
    if ((plan->issues & APA_REPAIR_ISSUE_MBR_ID) != 0)
        scr_printf("- __mbr identifier damaged\n");
    if ((plan->issues & APA_REPAIR_ISSUE_SONY_MAGIC) != 0)
        scr_printf("- Sony MBR signature damaged\n");
    if ((plan->issues & APA_REPAIR_ISSUE_MASTER_START) != 0)
        scr_printf("- master start is not sector 0\n");
    if ((plan->issues & APA_REPAIR_ISSUE_MASTER_TYPE) != 0)
        scr_printf("- master APA type is not MBR\n");
    if ((plan->issues & APA_REPAIR_ISSUE_MBR_VERSION) != 0)
        scr_printf("- MBR version is not canonical\n");
    if ((plan->issues & APA_REPAIR_ISSUE_POINTER_INCONSISTENT) != 0)
        scr_printf("- osdStart/osdSize are inconsistent\n");

    if ((plan->blockers & APA_REPAIR_BLOCKER_HYBRID_GPT) != 0)
        scr_printf("BLOCKER: protective MBR/GPT signal present\n");
    if ((plan->blockers & APA_REPAIR_BLOCKER_LOW_IDENTITY) != 0)
        scr_printf("BLOCKER: insufficient APA identity evidence\n");
    if ((plan->blockers & APA_REPAIR_BLOCKER_NOT_MASTER) != 0)
        scr_printf("BLOCKER: sector 0 is not reliably the APA master\n");
    if ((plan->blockers & APA_REPAIR_BLOCKER_UNEXPLAINED_CHECKSUM) != 0)
        scr_printf("BLOCKER: checksum does not corroborate one exact repair\n");
}

static int apply_header_repair(unsigned char header[APA_HEADER_SIZE],
                               const apa_repair_plan_t *plan)
{
    unsigned char repaired[APA_HEADER_SIZE];
    char snapshot_path[REPAIR_SNAPSHOT_PATH_SIZE];
    int result;

    disk_status_begin_at("Deterministic APA master recovery",
                         "Building planner-approved repaired header",
                         "In-memory copy of APA master sectors 0-1");
    disk_status_io(DISK_STATUS_SCAN, 0, 2, 1, 3);
    result = apa_repair_build_header(header, plan, repaired);
    if (result < 0) {
        disk_status_end();
        return REPAIR_CONTROLLER_BLOCKED;
    }

    /* Raw sectors 0-1 are the exceptional recovery path. Preserve the exact
       damaged bytes externally before the first write; normal Torii-style
       operations continue to avoid raw master-header writes entirely. */
    disk_status_phase_at("Saving exact pre-repair HDDRAW snapshot",
                         "Selected backup storage; source is sectors 0-1");
    disk_status_io(DISK_STATUS_SCAN, 0, 2, 2, 3);
    result = repair_snapshot_save(storage_selected(), header, snapshot_path);
    disk_status_end();
    if (result < 0) {
        scr_clear();
        scr_printf("Raw repair snapshot could not be saved.\n\n");
        scr_printf("Storage: %s\n", storage_targets[storage_selected()].name);
        scr_printf("Code   : %d\n\n", result);
        scr_printf("Sectors 0-1 were NOT modified.\n");
        scr_printf("Press X to return.\n");
        while (!(wait_for_press() & PAD_CROSS)) {}
        return REPAIR_CONTROLLER_NONE;
    }

    scr_clear();
    scr_printf("APA master-header repair\n\n");
    scr_printf("Raw snapshot: %s\n\n", snapshot_path);
    scr_printf("Only planner-approved canonical fields and checksum\n");
    scr_printf("will be rewritten in sectors 0-1.\n");
    scr_printf("No partition contents are being reconstructed.\n\n");
    print_plan(plan);
    scr_printf("\nHold L1+R1 and press START to repair.\n");
    scr_printf("TRIANGLE cancels.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_START))
        return REPAIR_CONTROLLER_NONE;

    result = hdd_repair_write_master_header_verified(repaired, header);
    if (result < 0) {
        session_log_line("Raw APA repair failed code=%d snapshot=%s", result,
                         snapshot_path);
        session_log_flush(storage_selected());
        scr_clear();
        scr_printf("APA repair FAILED.\n\n");
        scr_printf("Code: %d\n", result);
        scr_printf("Raw snapshot: %s\n\n", snapshot_path);
        scr_printf("Do not perform further HDD writes in this session.\n");
        scr_printf("Press X to return.\n");
        while (!(wait_for_press() & PAD_CROSS)) {}
        return REPAIR_CONTROLLER_BLOCKED;
    }

    session_log_line("Raw APA repair verified; snapshot=%s fixes=0x%08x",
                     snapshot_path, (unsigned int)plan->safe_header_fixes);
    session_log_flush(storage_selected());
    scr_clear();
    scr_printf("APA master header repaired and read back exactly.\n\n");
    scr_printf("Snapshot: %s\n\n", snapshot_path);
    scr_printf("Restart is required so ps2hdd re-reads the repaired disk.\n");
    scr_printf("Press X to restart.\n");
    while (!(wait_for_press() & PAD_CROSS)) {}
    return REPAIR_CONTROLLER_RESTART_REQUIRED;
}

int repair_controller_startup(unsigned char header[APA_HEADER_SIZE],
                              int hdd_status)
{
    const uint32_t header_issue_mask =
        APA_REPAIR_ISSUE_CHECKSUM |
        APA_REPAIR_ISSUE_APA_MAGIC |
        APA_REPAIR_ISSUE_MBR_ID |
        APA_REPAIR_ISSUE_SONY_MAGIC |
        APA_REPAIR_ISSUE_MASTER_START |
        APA_REPAIR_ISSUE_MASTER_TYPE |
        APA_REPAIR_ISSUE_MBR_VERSION;
    apa_repair_plan_t plan;

    disk_status_begin_at("Startup recovery analysis",
                         "Evaluating raw APA master identity and checksum",
                         "APA master header / sectors 0-1");
    disk_status_io(DISK_STATUS_SCAN, 0, 2, 1, 1);
    if (apa_repair_analyze(header, &plan) < 0) {
        disk_status_end();
        return REPAIR_CONTROLLER_BLOCKED;
    }
    disk_status_end();

    /* Pointer-only anomalies belong to normal mounted-disk health handling;
       startup recovery exists only to rescue a master rejected by ps2hdd. */
    if ((plan.issues & header_issue_mask) == 0 && plan.blockers == 0)
        return REPAIR_CONTROLLER_NONE;

    if (!plan.header_patch_safe) {
        scr_clear();
        scr_printf("APA recovery analysis\n\n");
        scr_printf("ps2hdd status: %d\n", hdd_status);
        scr_printf("Identity evidence: %u/3\n", plan.identity_matches);
        scr_printf("Master anchors   : %u/3\n\n", plan.master_anchor_matches);
        print_plan(&plan);
        scr_printf("\nThis state is not safe for automatic sector-0 repair.\n");
        scr_printf("No HDD data was modified.\n\n");
        scr_printf("Press X to continue to the fail-safe screen.\n");
        while (!(wait_for_press() & PAD_CROSS)) {}
        return REPAIR_CONTROLLER_BLOCKED;
    }

    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf("APA recovery analysis\n\n");
        scr_printf("ps2hdd status: %d\n", hdd_status);
        scr_printf("Identity evidence: %u/3\n", plan.identity_matches);
        scr_printf("Master anchors   : %u/3\n", plan.master_anchor_matches);
        scr_printf("Snapshot storage : %s\n\n",
                   storage_targets[storage_selected()].name);
        print_plan(&plan);
        scr_printf("\nX      Prepare exact repair\n");
        scr_printf("SELECT Cycle snapshot storage\n");
        scr_printf("TRIANGLE Cancel\n");
        pressed = wait_for_press();
        if (pressed & PAD_SELECT) {
            storage_set_selected((storage_selected() + 1u) %
                                 STORAGE_TARGET_COUNT);
            continue;
        }
        if (pressed & PAD_CROSS)
            return apply_header_repair(header, &plan);
        if (pressed & PAD_TRIANGLE)
            return REPAIR_CONTROLLER_BLOCKED;
    }
}

int repair_controller_health(unsigned char header[APA_HEADER_SIZE],
                             const boot_chain_info_t *boot_chain)
{
    repair_health_t health;
    int bounds_result = 0;
    unsigned int start = read_le32(header + APA_OSD_START_OFFSET);
    unsigned int size = read_le32(header + APA_OSD_SIZE_OFFSET);

    disk_status_begin_at("Deterministic HDD health analysis",
                         "Checking APA master identity and pointer state",
                         "APA master header / sectors 0-1");
    disk_status_io(DISK_STATUS_SCAN, 0, 2, 1, 3);

    if (start != 0 && size != 0) {
        disk_status_phase_at("Validating active bootstrap pointer bounds",
                             "Reserved __mbr payload geometry");
        disk_status_io(DISK_STATUS_SCAN, start, size, 2, 3);
        bounds_result = hdd_validate_payload_bounds(start, size);
    } else {
        disk_status_phase_at("Bootstrap pointer is disabled",
                             "APA master osdStart/osdSize");
        disk_status_io(DISK_STATUS_SCAN, 0, 2, 2, 3);
    }

    disk_status_phase_at("Combining header, pointer and payload evidence",
                         "In-memory deterministic repair policy");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 3, 3);
    if (repair_health_assess(header, boot_chain, bounds_result, &health) < 0) {
        disk_status_end();
        return REPAIR_CONTROLLER_BLOCKED;
    }
    disk_status_end();

    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf("HDD structure health / repair\n\n");
        scr_printf("APA identity : %u/3\n",
                   health.header_plan.identity_matches);
        scr_printf("Master anchors: %u/3\n",
                   health.header_plan.master_anchor_matches);
        scr_printf("Checksum     : %s\n",
                   (health.header_plan.issues & APA_REPAIR_ISSUE_CHECKSUM)
                       ? "BAD" : "OK");
        scr_printf("osdStart     : 0x%08x\n", health.osd_start);
        scr_printf("osdSize      : 0x%08x\n", health.osd_size);
        if (health.osd_start != 0 && health.osd_size != 0)
            scr_printf("Pointer bounds: %d\n", health.bounds_result);
        if (boot_chain != NULL &&
            health.osd_start != 0 && health.osd_size != 0) {
            scr_printf("Payload read : %d\n", boot_chain->payload_read_result);
            scr_printf("Payload KELF : %d\n", boot_chain->payload_kelf_result);
        }
        scr_printf("\n");
        if (health.header_plan.issues != 0 ||
            health.header_plan.blockers != 0)
            print_plan(&health.header_plan);
        else
            scr_printf("No master-header defect detected.\n");

        if (health.header_plan.header_patch_safe)
            scr_printf("\nX      Repair canonical master-header field(s)\n");
        if (health.pointer_clear_recommended)
            scr_printf("SQUARE Use normal backup + disable workflow\n");
        if (!health.header_plan.header_patch_safe &&
            !health.pointer_clear_recommended)
            scr_printf("\nNothing deterministic needs repair.\n");
        scr_printf("TRIANGLE Return\n");

        pressed = wait_for_press();
        if ((pressed & PAD_CROSS) && health.header_plan.header_patch_safe)
            return apply_header_repair(header, &health.header_plan);
        if ((pressed & PAD_SQUARE) && health.pointer_clear_recommended)
            return REPAIR_CONTROLLER_REQUEST_DISABLE;
        if (pressed & PAD_TRIANGLE)
            return REPAIR_CONTROLLER_NONE;
    }
}
