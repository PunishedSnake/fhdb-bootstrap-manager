/* PS2-only active-payload acquisition for boot-chain diagnostics. */

#include "boot_payload.h"
#include "boot_payload_ps2.h"
#include "hdd_read.h"

#include <stdlib.h>

void scan_active_payload_evidence(boot_chain_info_t *info,
                                  unsigned int start,
                                  unsigned int sectors)
{
    unsigned char *payload = NULL;
    unsigned int payload_bytes = 0;

    info->pointer_consistent = ((start == 0) == (sectors == 0));
    info->payload_read_result = -1;
    info->payload_kelf_result = -1;

    if (start == 0 || sectors == 0)
        return;

    info->payload_read_result = hdd_validate_payload_bounds(start, sectors);
    if (info->payload_read_result < 0)
        return;

    info->payload_read_result =
        hdd_read_payload_image(start, sectors, &payload, &payload_bytes);
    if (info->payload_read_result < 0)
        return;

    boot_payload_fingerprint(info, payload, payload_bytes);
    free(payload);
}
