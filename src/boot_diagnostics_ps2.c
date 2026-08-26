/* PS2-only orchestration of the read-only boot-chain evidence sources. */

#include <string.h>

#include "boot_chain.h"
#include "boot_chain_ps2.h"
#include "boot_diagnostics_ps2.h"
#include "boot_payload_ps2.h"
#include "disk_status_ps2.h"
#include "storage.h"

#define BOOT_DIAG_STAGE __attribute__((noinline))

/*
 * Diagnostics crosses ROM, HDD raw payload, memory cards and two PFS mounts.
 * Each source is an independent, blocking evidence stage. Keep those stages
 * behind explicit call boundaries so LTO cannot fold the entire device walk
 * plus classification policy into one multi-kilobyte instruction working set.
 */
static BOOT_DIAG_STAGE void scan_console_identity_stage(boot_chain_info_t *info)
{
    read_romver(info->romver);
    expected_system_folder(info->romver, info->expected_system_folder,
                           sizeof(info->expected_system_folder));
}

static BOOT_DIAG_STAGE void scan_active_payload_stage(boot_chain_info_t *info,
                                                       unsigned int start,
                                                       unsigned int sectors)
{
    scan_active_payload_evidence(info, start, sectors);
}

static BOOT_DIAG_STAGE void scan_skip_hdd_stage(boot_chain_info_t *info)
{
    scan_skip_hdd_settings(info);
}

static BOOT_DIAG_STAGE void scan_memory_card_stage(boot_chain_info_t *info)
{
    scan_memory_card_boot_files(info);
}

static BOOT_DIAG_STAGE void scan_sysconf_stage(boot_chain_info_t *info)
{
    scan_sysconf_partition(info);
}

static BOOT_DIAG_STAGE void scan_system_stage(boot_chain_info_t *info)
{
    scan_system_partition(info);
}

static BOOT_DIAG_STAGE void classify_boot_chain_stage(boot_chain_info_t *info,
                                                       unsigned int start,
                                                       unsigned int sectors)
{
    classify_boot_chain(info, start, sectors);
}

void boot_diagnostics_scan(boot_chain_info_t *info,
                           unsigned int start,
                           unsigned int sectors)
{
    if (info == NULL)
        return;

    memset(info, 0, sizeof(*info));
    info->skip_hdd[0] = -1;
    info->skip_hdd[1] = -1;
    info->skip_hdd[2] = -1;

    disk_status_begin_at("Boot-chain diagnostics",
                         "Reading console identity",
                         "rom0:ROMVER / expected system folder");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 0, 6);
    scan_console_identity_stage(info);

    disk_status_phase_at("Inspecting active bootstrap payload",
                         start != 0 && sectors != 0
                             ? "Reserved __mbr payload selected by osdStart/osdSize"
                             : "APA master pointer is disabled; no active payload");
    disk_status_io(DISK_STATUS_SCAN, start, sectors, 1, 6);
    scan_active_payload_stage(info, start, sectors);

    disk_status_phase_at("Checking FMCB Skip_HDD settings",
                         "Memory cards / FREEMCB.CNF");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 2, 6);
    scan_skip_hdd_stage(info);

    disk_status_phase_at("Scanning memory-card boot files",
                         "mc0:/ and mc1:/ system/boot locations");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 3, 6);
    scan_memory_card_stage(info);

    disk_status_phase_at("Inspecting __sysconf boot configuration",
                         "hdd0:__sysconf / OSDMBR.CNF and related files");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 4, 6);
    scan_sysconf_stage(info);

    disk_status_phase_at("Inspecting __system HDD boot modules",
                         "hdd0:__system / HDD OSD, FHDB and PSBBN evidence");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 5, 6);
    scan_system_stage(info);

    disk_status_phase_at("Classifying collected boot-chain evidence",
                         "In-memory evidence map; no HDD write");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 6, 6);
    classify_boot_chain_stage(info, start, sectors);
    disk_status_end();
}

#undef BOOT_DIAG_STAGE
