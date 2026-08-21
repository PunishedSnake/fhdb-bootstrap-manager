#ifndef PS2_HDD_BOOTSTRAP_MANAGER_DIAGNOSTICS_CONTROLLER_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_DIAGNOSTICS_CONTROLLER_PS2_H

#include "apa.h"
#include "boot_chain.h"

void diagnostics_controller_refresh(
    const unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain,
    int save_to_storage);

void diagnostics_controller_screen(
    const unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

#endif
