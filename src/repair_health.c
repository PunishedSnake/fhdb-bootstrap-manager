/* Portable decision layer for normal mounted-disk repair recommendations. */

#include "repair_health.h"

#include <string.h>

int repair_health_assess(const unsigned char header[APA_HEADER_SIZE],
                         const boot_chain_info_t *boot_chain,
                         int bounds_result,
                         repair_health_t *health)
{
    if (header == NULL || health == NULL)
        return -1;

    memset(health, 0, sizeof(*health));
    if (apa_repair_analyze(header, &health->header_plan) < 0)
        return -1;

    health->osd_start = read_le32(header + APA_OSD_START_OFFSET);
    health->osd_size = read_le32(header + APA_OSD_SIZE_OFFSET);
    health->bounds_result = bounds_result;

    if ((health->osd_start == 0) != (health->osd_size == 0)) {
        health->pointer_clear_recommended = 1;
    } else if (health->osd_start != 0) {
        if (bounds_result < 0)
            health->pointer_clear_recommended = 1;
        if (boot_chain != NULL &&
            (boot_chain->payload_read_result < 0 ||
             boot_chain->payload_kelf_result < 0))
            health->pointer_clear_recommended = 1;
    }

    if (health->header_plan.pointer_clear_recommended)
        health->pointer_clear_recommended = 1;

    return 0;
}
