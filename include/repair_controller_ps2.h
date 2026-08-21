#ifndef PS2_HDD_BOOTSTRAP_MANAGER_REPAIR_CONTROLLER_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_REPAIR_CONTROLLER_PS2_H

#include "apa.h"
#include "boot_chain.h"

typedef enum {
    REPAIR_CONTROLLER_NONE = 0,
    REPAIR_CONTROLLER_REQUEST_DISABLE = 1,
    REPAIR_CONTROLLER_RESTART_REQUIRED = 2,
    REPAIR_CONTROLLER_BLOCKED = -340
} repair_controller_result_t;

/*
 * Called after raw sectors 0-1 are readable, even when ps2hdd status is not 0.
 * It offers only planner-approved sector-zero repairs and otherwise returns
 * BLOCKED so normal startup can remain fail-closed.
 */
int repair_controller_startup(unsigned char header[APA_HEADER_SIZE],
                              int hdd_status);

/*
 * Read/preview repair screen for an already mounted APA device. It may perform
 * a planner-approved raw header repair (requiring restart), or ask main.c to
 * reuse the established full backup + pointer-disable workflow.
 */
int repair_controller_health(unsigned char header[APA_HEADER_SIZE],
                             const boot_chain_info_t *boot_chain);

#endif
