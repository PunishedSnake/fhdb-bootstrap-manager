/* PS2-only orchestration of the read-only boot-chain evidence sources. */

#include <string.h>

#include "boot_chain.h"
#include "boot_chain_ps2.h"
#include "boot_diagnostics_ps2.h"
#include "boot_payload_ps2.h"
#include "storage.h"

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

    read_romver(info->romver);
    expected_system_folder(info->romver, info->expected_system_folder,
                           sizeof(info->expected_system_folder));
    scan_active_payload_evidence(info, start, sectors);
    scan_skip_hdd_settings(info);
    scan_memory_card_boot_files(info);
    scan_sysconf_partition(info);
    scan_system_partition(info);
    classify_boot_chain(info, start, sectors);
}
