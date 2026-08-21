#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_CHAIN_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_CHAIN_PS2_H

#include "boot_chain.h"

/*
 * PS2-only evidence collection. These routines mount/read devices but never
 * modify HDD or memory-card contents. Classification remains in boot_chain.c.
 */
void scan_skip_hdd_settings(boot_chain_info_t *info);
void scan_memory_card_boot_files(boot_chain_info_t *info);
void scan_sysconf_partition(boot_chain_info_t *info);
void scan_system_partition(boot_chain_info_t *info);

#endif
