#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_PAYLOAD_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_PAYLOAD_PS2_H

#include "boot_chain.h"

/*
 * Collect only active-payload evidence for the supplied APA pointer. The
 * implementation may read hdd0: but cannot write sectors or change osdStart.
 */
void scan_active_payload_evidence(boot_chain_info_t *info,
                                  unsigned int start,
                                  unsigned int sectors);

#endif
