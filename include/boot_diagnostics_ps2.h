#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_DIAGNOSTICS_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_DIAGNOSTICS_PS2_H

#include "boot_chain.h"

/*
 * Collect one complete read-only boot-chain evidence snapshot for the
 * supplied APA bootstrap pointer. This boundary performs no rendering,
 * persistence, UI, rescue, signing, or disk-changing operation.
 */
void boot_diagnostics_scan(boot_chain_info_t *info,
                           unsigned int start,
                           unsigned int sectors);

#endif
