#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_TRANSACTION_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_TRANSACTION_PS2_H

#include <stdint.h>

#include "bootstrap_transaction.h"

int bootstrap_transaction_ps2_set_pointer(
    unsigned char header[APA_HEADER_SIZE],
    uint32_t start, uint32_t size,
    bootstrap_transaction_result_t *result);

int bootstrap_transaction_ps2_activate(
    unsigned char header[APA_HEADER_SIZE],
    const unsigned char *payload, unsigned int payload_size,
    uint32_t start, uint32_t size,
    bootstrap_transaction_release_fn release_payload,
    void *release_context,
    bootstrap_transaction_result_t *result);

#endif
