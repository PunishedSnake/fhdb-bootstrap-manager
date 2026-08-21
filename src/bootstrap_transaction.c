/* Portable sequencing policy for dangerous bootstrap write transactions. */

#include <stddef.h>

#include "bootstrap_transaction.h"

static void reset_result(bootstrap_transaction_result_t *result)
{
    if (result != NULL) {
        result->stage = BOOTSTRAP_TRANSACTION_STAGE_NONE;
        result->code = 0;
    }
}

static int fail_result(bootstrap_transaction_result_t *result,
                       bootstrap_transaction_stage_t stage, int code)
{
    if (result != NULL) {
        result->stage = stage;
        result->code = code;
    }
    return code;
}

int bootstrap_transaction_set_pointer(
    const bootstrap_transaction_ops_t *ops,
    unsigned char header[APA_HEADER_SIZE],
    uint32_t start, uint32_t size,
    bootstrap_transaction_result_t *result)
{
    int code;

    reset_result(result);
    if (ops == NULL || result == NULL || header == NULL ||
        ops->set_osd_mbr == NULL || ops->verify_osd_mbr == NULL)
        return fail_result(result, BOOTSTRAP_TRANSACTION_STAGE_NONE,
                           BOOTSTRAP_TRANSACTION_INVALID_ARGUMENT);

    code = ops->set_osd_mbr(ops->context, start, size);
    if (code < 0)
        return fail_result(result, BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET,
                           code);

    code = ops->verify_osd_mbr(ops->context, header, start, size);
    if (code < 0)
        return fail_result(result, BOOTSTRAP_TRANSACTION_STAGE_POINTER_VERIFY,
                           code);
    return 0;
}

int bootstrap_transaction_activate(
    const bootstrap_transaction_ops_t *ops,
    unsigned char header[APA_HEADER_SIZE],
    const unsigned char *payload, unsigned int payload_size,
    uint32_t start, uint32_t size,
    bootstrap_transaction_release_fn release_payload,
    void *release_context,
    bootstrap_transaction_result_t *result)
{
    int code;

    reset_result(result);
    if (ops == NULL || result == NULL || header == NULL || payload == NULL ||
        payload_size == 0 || ops->write_payload_verified == NULL ||
        ops->set_osd_mbr == NULL || ops->verify_osd_mbr == NULL)
        return fail_result(result, BOOTSTRAP_TRANSACTION_STAGE_NONE,
                           BOOTSTRAP_TRANSACTION_INVALID_ARGUMENT);

    code = ops->write_payload_verified(ops->context, payload, payload_size,
                                       start);
    if (release_payload != NULL)
        release_payload(release_context);
    if (code < 0)
        return fail_result(result, BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD, code);

    return bootstrap_transaction_set_pointer(ops, header, start, size, result);
}
