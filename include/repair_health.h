#ifndef PS2_HDD_BOOTSTRAP_MANAGER_REPAIR_HEALTH_H
#define PS2_HDD_BOOTSTRAP_MANAGER_REPAIR_HEALTH_H

#include "apa_repair.h"
#include "boot_chain.h"

typedef struct {
    apa_repair_plan_t header_plan;
    unsigned int osd_start;
    unsigned int osd_size;
    int bounds_result;
    int pointer_clear_recommended;
} repair_health_t;

/*
 * Combine portable master-header repair policy with an already-evaluated
 * payload-bounds result and optional boot-chain payload evidence. Device I/O
 * stays outside this module so the same decision is host-testable.
 */
int repair_health_assess(const unsigned char header[APA_HEADER_SIZE],
                         const boot_chain_info_t *boot_chain,
                         int bounds_result,
                         repair_health_t *health);

#endif
