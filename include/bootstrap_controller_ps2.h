#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_CONTROLLER_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_CONTROLLER_PS2_H

#include "apa.h"
#include "boot_chain.h"

void bootstrap_controller_backup_current(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

void bootstrap_controller_disable(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

void bootstrap_controller_restore(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

void bootstrap_controller_install(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

#endif
