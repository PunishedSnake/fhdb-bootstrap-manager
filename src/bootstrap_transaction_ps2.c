/* PS2 binding for the portable bootstrap transaction sequencer. */

#include "bootstrap_transaction_ps2.h"
#include "hdd_write.h"

static int ps2_write_payload_verified(void *context,
                                      const unsigned char *payload,
                                      unsigned int payload_size,
                                      uint32_t start_sector)
{
    (void)context;
    return hdd_write_payload_verified(payload, payload_size, start_sector);
}

static int ps2_set_osd_mbr(void *context, uint32_t start, uint32_t size)
{
    (void)context;
    return hdd_write_set_osd_mbr(start, size);
}

static int ps2_verify_osd_mbr(void *context,
                              unsigned char destination[APA_HEADER_SIZE],
                              uint32_t expected_start,
                              uint32_t expected_size)
{
    (void)context;
    return hdd_write_verify_osd_mbr(destination, expected_start,
                                    expected_size);
}

static const bootstrap_transaction_ops_t ps2_ops = {
    ps2_write_payload_verified,
    ps2_set_osd_mbr,
    ps2_verify_osd_mbr,
    NULL
};

int bootstrap_transaction_ps2_set_pointer(
    unsigned char header[APA_HEADER_SIZE],
    uint32_t start, uint32_t size,
    bootstrap_transaction_result_t *result)
{
    return bootstrap_transaction_set_pointer(&ps2_ops, header, start, size,
                                             result);
}

int bootstrap_transaction_ps2_activate(
    unsigned char header[APA_HEADER_SIZE],
    const unsigned char *payload, unsigned int payload_size,
    uint32_t start, uint32_t size,
    bootstrap_transaction_release_fn release_payload,
    void *release_context,
    bootstrap_transaction_result_t *result)
{
    return bootstrap_transaction_activate(
        &ps2_ops, header, payload, payload_size, start, size,
        release_payload, release_context, result);
}
