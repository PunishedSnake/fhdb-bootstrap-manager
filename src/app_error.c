#include "app_error.h"

#include <string.h>

static app_error_record_t last_error;
static int last_error_valid;

void app_error_record(app_error_domain_t domain, int code,
                      const char *context)
{
    last_error.domain = domain;
    last_error.code = code;
    last_error.context[0] = '\0';
    if (context != NULL) {
        strncpy(last_error.context, context, APP_ERROR_CONTEXT_SIZE - 1u);
        last_error.context[APP_ERROR_CONTEXT_SIZE - 1u] = '\0';
    }
    last_error_valid = 1;
}

void app_error_clear(void)
{
    memset(&last_error, 0, sizeof(last_error));
    last_error_valid = 0;
}

int app_error_get(app_error_record_t *record)
{
    if (!last_error_valid || record == NULL)
        return 0;
    *record = last_error;
    return 1;
}

static void set_info(app_error_info_t *info, const char *symbol,
                     const char *summary, const char *detail,
                     const char *action)
{
    info->symbol = symbol;
    info->summary = summary;
    info->detail = detail;
    info->action = action;
}

void app_error_describe(app_error_domain_t domain, int code,
                        app_error_info_t *info)
{
    if (info == NULL)
        return;

    if (domain == APP_ERROR_DOMAIN_BOOTSTRAP_SOURCE) {
        switch (code) {
            case -120:
                set_info(info, "BOOTSTRAP_SOURCE_SIZE_INVALID",
                         "Bootstrap source has an invalid size.",
                         "The file is empty or larger than the supported MBR payload limit.",
                         "Replace MBR.XIN/XLF with a known-good source and retry.");
                return;
            case -121:
                set_info(info, "BOOTSTRAP_SOURCE_SEEK_FAILED",
                         "Bootstrap source could not be rewound.",
                         "The storage driver opened the file but rejected the required seek.",
                         "Check the storage medium and copy the source file again.");
                return;
            case -122:
                set_info(info, "BOOTSTRAP_SOURCE_ALLOC_FAILED",
                         "Not enough EE memory for the bootstrap source.",
                         "The source could not be buffered for validation/signing.",
                         "Restart the manager and retry with no other homebrew resident.");
                return;
            case -123:
                set_info(info, "BOOTSTRAP_SOURCE_SHORT_READ",
                         "Bootstrap source ended before the advertised file size.",
                         "The storage read was incomplete or the medium changed during the operation.",
                         "Reconnect/check the storage device and copy MBR.XIN/XLF again.");
                return;
            case -124:
                set_info(info, "BOOTSTRAP_SOURCE_CAPACITY_INVALID",
                         "The source does not fit the reserved __mbr payload area.",
                         "The live __mbr geometry cannot safely contain this payload.",
                         "Do not force the install; inspect disk geometry/source size first.");
                return;
            default:
                set_info(info, "BOOTSTRAP_SOURCE_IO",
                         "Bootstrap source could not be loaded.",
                         "The selected storage driver rejected opening or reading MBR.XIN/XLF. The raw driver code is retained below rather than guessed from the number alone.",
                         "Verify the selected storage target, filename, media readiness and file integrity.");
                return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_KELF) {
        set_info(info, "KELF_LAYOUT_REJECTED",
                 "The bootstrap source is not a structurally valid KELF.",
                 "KELF layout validation rejected the file before signing or HDD writes were attempted.",
                 "Use a known-good stock MBR.XIN/XLF and do not bypass validation.");
        return;
    }

    if (domain == APP_ERROR_DOMAIN_HDD_BOUNDS) {
        switch (code) {
            case -170: set_info(info, "HDD_PAYLOAD_EMPTY_POINTER", "Bootstrap pointer is empty.", "osdStart/osdSize do not describe an active payload.", "Use a disabled-bootstrap workflow or restore a verified rescue image."); return;
            case -171: set_info(info, "HDD_PAYLOAD_TOO_LARGE", "Bootstrap payload exceeds the manager safety limit.", "The pointer describes more than the supported 4 MiB payload window.", "Do not read/write it as a normal bootstrap; inspect it for corruption."); return;
            case -172: set_info(info, "HDD_PAYLOAD_BEFORE_RESERVED_AREA", "Bootstrap pointer begins before the reserved payload area.", "Normal payload data must start at or after LBA 0x2000 inside __mbr.", "Treat the pointer as unsafe and use diagnostics/recovery."); return;
            case -173: set_info(info, "HDD_PAYLOAD_OUTSIDE_MBR", "Bootstrap pointer leaves the live __mbr partition.", "The advertised payload range is outside current APA geometry.", "Do not follow the pointer; inspect/clear it through the guarded recovery path."); return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_HDD_WRITE) {
        switch (code) {
            case -1: set_info(info, "HDD_WRITE_HEADER_READ_FAILED", "Could not read the master header for verification.", "The post-operation sectors 0-1 read failed.", "Keep the backup and restart before attempting another write."); return;
            case -2: set_info(info, "HDD_WRITE_HEADER_INVALID", "Post-write master header validation failed.", "The read-back header is not a valid canonical APA master.", "Stop further writes and preserve backups/logs for analysis."); return;
            case -3: set_info(info, "HDD_WRITE_START_MISMATCH", "osdStart read-back does not match the requested value.", "The pointer update did not persist exactly as requested.", "Restart and inspect the master before any further write."); return;
            case -4: set_info(info, "HDD_WRITE_SIZE_MISMATCH", "osdSize read-back does not match the requested value.", "The pointer update did not persist exactly as requested.", "Restart and inspect the master before any further write."); return;
            case -130: set_info(info, "HDD_WRITE_PAYLOAD_FLUSH_FAILED", "HDD cache flush failed after payload write.", "Durability of the payload cannot be assumed.", "Do not enable/restore the pointer; power-cycle only after preserving diagnostics."); return;
            case -131: set_info(info, "HDD_WRITE_PAYLOAD_COMPARE_FAILED", "Payload read-back differs from the data written.", "At least one sector failed exact verification.", "Keep the pointer disabled and investigate the disk/adapter before retrying."); return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_REPAIR_SNAPSHOT) {
        switch (code) {
            case -320: set_info(info, "HDDRAW_INVALID_ARGUMENT", "Raw master snapshot request was invalid.", "The recovery controller supplied incomplete snapshot parameters.", "Do not continue with repair; preserve the current disk state."); return;
            case -321: set_info(info, "HDDRAW_NO_SLOT", "No non-overwriting HDDRAW snapshot slot is available.", "HDDRAW.BIN and HDDRAW2.BIN are occupied by different evidence.", "Copy them off the device or select another storage target."); return;
            case -322: set_info(info, "HDDRAW_WRITE_FAILED", "Could not write the raw pre-repair snapshot.", "External storage rejected the HDDRAW write.", "Fix/select another storage target; no HDD repair should be attempted."); return;
            case -323: set_info(info, "HDDRAW_VERIFY_FAILED", "HDDRAW snapshot read-back verification failed.", "The saved evidence does not exactly match the source master header.", "Do not repair the HDD; replace or reformat the external storage medium."); return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_MASTER_REPAIR) {
        switch (code) {
            case -330: set_info(info, "MASTER_REPAIR_INVALID_ARGUMENT", "Master repair request was invalid.", "The exceptional writer did not receive complete buffers.", "Stop the operation and preserve the raw snapshot."); return;
            case -331: set_info(info, "MASTER_REPAIR_UNSAFE_HEADER", "Candidate master failed final safety validation.", "The proposed sectors 0-1 image is not a canonical non-hybrid APA master.", "Do not force the write; re-run analysis from the saved evidence."); return;
            case -332: set_info(info, "MASTER_REPAIR_WRITE_FAILED", "Raw master write failed.", "HDIOC_WRITESECTOR did not commit sectors 0-1.", "Restart/power-cycle, keep HDDRAW, and inspect the disk before retrying."); return;
            case -333: set_info(info, "MASTER_REPAIR_FLUSH_FAILED", "Raw master cache flush failed.", "Durability of sectors 0-1 cannot be guaranteed.", "Stop all further HDD operations and restart/power-cycle."); return;
            case -334: set_info(info, "MASTER_REPAIR_READBACK_FAILED", "Could not read the repaired master back.", "Post-write verification could not obtain sectors 0-1.", "Stop all further HDD operations; keep HDDRAW and logs."); return;
            case -335: set_info(info, "MASTER_REPAIR_COMPARE_FAILED", "Repaired master differs on read-back.", "The physical bytes on disk do not match the approved repair image.", "Treat the disk/adapter as unreliable and do not attempt another repair in-session."); return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_FORENSIC_SNAPSHOT) {
        switch (code) {
            case -360: set_info(info, "HDDMETA_INVALID_ARGUMENT", "Forensic snapshot request was invalid.", "The selected repair plan cannot be serialized safely.", "Return to the scan and rebuild the candidate/plan."); return;
            case -361: set_info(info, "HDDMETA_ALLOC_FAILED", "Not enough EE memory for HDDMETA.", "The complete touched-header snapshot could not be assembled.", "Restart and retry; no HDD metadata was modified."); return;
            case -362: set_info(info, "HDDMETA_NO_SLOT", "No non-overwriting HDDMETA slot is available.", "HDDMETA.BIN and HDDMETA2.BIN contain different existing evidence.", "Copy them off-device or select another storage target."); return;
            case -363: set_info(info, "HDDMETA_WRITE_FAILED", "Could not save HDDMETA.", "External storage rejected the forensic snapshot write.", "Fix/select another storage target; do not authorize repair."); return;
            case -364: set_info(info, "HDDMETA_VERIFY_FAILED", "HDDMETA read-back verification failed.", "The saved multi-header evidence is not byte-identical to the generated snapshot.", "Do not authorize repair; replace/check external storage."); return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_FORENSIC_REPAIR) {
        switch (code) {
            case -370: set_info(info, "FORENSIC_REPAIR_INVALID_ARGUMENT", "Forensic repair request was invalid.", "The scan/plan pair is incomplete.", "Return to the forensic scan and rebuild the plan."); return;
            case -371: set_info(info, "FORENSIC_REPAIR_PLAN_BLOCKED", "Forensic repair plan is not authorized for writes.", "The selected map does not satisfy the manual write gate.", "Keep the disk read-only and inspect competing evidence/maps."); return;
            case -372: set_info(info, "FORENSIC_REPAIR_SOURCE_CHANGED", "A source header changed after the scan.", "Current disk bytes no longer match the evidence used to build the plan.", "Abort this plan and perform a fresh raw scan."); return;
            case -373: set_info(info, "FORENSIC_REPAIR_PATCH_INVALID", "A proposed patched header failed final validation.", "The reconstructed topology cannot be committed safely.", "Do not force the write; preserve HDDMETA/FORENSIC.TXT."); return;
            case -374: set_info(info, "FORENSIC_REPAIR_WRITE_FAILED", "APA header write failed.", "The current topology transaction stopped during HDIOC_WRITESECTOR.", "Restart before further HDD work; some earlier interior headers may already be committed."); return;
            case -375: set_info(info, "FORENSIC_REPAIR_FLUSH_FAILED", "APA header flush failed.", "Durability of the most recent metadata write is unknown.", "Restart/power-cycle and rescan from raw evidence before any additional repair."); return;
            case -376: set_info(info, "FORENSIC_REPAIR_READBACK_FAILED", "APA header read-back failed.", "The writer could not verify a committed header.", "Restart and perform a complete raw forensic rescan."); return;
            case -377: set_info(info, "FORENSIC_REPAIR_COMPARE_FAILED", "APA header read-back differs from the repair image.", "The physical disk does not contain exactly the bytes approved by the plan.", "Stop writes, restart and investigate the disk/adapter using the saved HDDMETA snapshot."); return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_STARTUP) {
        if (code == -101) {
            set_info(info, "STARTUP_APA_MASTER_INVALID", "APA master admission failed.", "Sectors 0-1 did not pass the normal canonical APA/checksum gate.", "Use the offered guarded recovery path if available; otherwise preserve raw evidence.");
            return;
        }
        if (code == -102) {
            set_info(info, "STARTUP_HYBRID_GPT_BLOCKED", "Hybrid/protective APA-GPT layout is blocked.", "The manager refuses write-capable admission for this mixed layout.", "Do not bypass this guard; inspect the disk on a host first.");
            return;
        }
    }

    if (domain == APP_ERROR_DOMAIN_IOP) {
        set_info(info, "IOP_DRIVER_ERROR",
                 "A PS2 I/O driver rejected the requested operation.",
                 "The raw negative driver code is intentionally preserved. Different IOP modules can reuse small numeric values, so the operation context is more reliable than guessing from the number alone.",
                 "Check the named operation/device, preserve logs, and retry only if the underlying media/path is known-good.");
        return;
    }

    set_info(info, "UNCLASSIFIED_ERROR",
             "The operation failed with an unclassified code.",
             "No matching project-owned error entry exists for this domain/code pair yet.",
             "Preserve HDDMAN.LOG and related recovery artifacts so the code can be classified without guessing.");
}
