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
 * Startup presentation/controller for a raw-readable master that may already
 * have been rejected by ps2hdd. Canonical repair decisions come from
 * apa_repair; this layer only presents/authorizes planner-approved recovery.
 */
int repair_controller_startup(unsigned char header[APA_HEADER_SIZE],
                              int hdd_status);

/*
 * Mounted-disk health UI. Portable repair_health policy decides whether a
 * pointer clear is recommended; this function may perform an approved raw
 * header repair or ask the caller to reuse normal backup + disable workflow.
 */
int repair_controller_health(unsigned char header[APA_HEADER_SIZE],
                             const boot_chain_info_t *boot_chain);

#endif
