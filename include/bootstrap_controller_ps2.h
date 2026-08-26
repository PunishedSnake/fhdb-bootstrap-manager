#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_CONTROLLER_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_CONTROLLER_PS2_H

#include "apa.h"
#include "boot_chain.h"

/* These are human-authorized, storage/MagicGate-dominated workflows. Keep each
 * controller as its own compact call target so whole-program LTO cannot fold
 * backup/disable/restore/install into the interactive Bootstrap menu and make
 * that menu compete with recovery code for the R5900's 16 KiB I-cache. */
#if defined(__GNUC__)
#define BOOTSTRAP_WORKFLOW_STAGE __attribute__((noinline, optimize("Os")))
#else
#define BOOTSTRAP_WORKFLOW_STAGE
#endif

void BOOTSTRAP_WORKFLOW_STAGE bootstrap_controller_backup_current(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

void BOOTSTRAP_WORKFLOW_STAGE bootstrap_controller_disable(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

void BOOTSTRAP_WORKFLOW_STAGE bootstrap_controller_restore(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

void BOOTSTRAP_WORKFLOW_STAGE bootstrap_controller_install(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain);

#undef BOOTSTRAP_WORKFLOW_STAGE

#endif
