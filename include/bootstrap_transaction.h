#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_TRANSACTION_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_TRANSACTION_H

#include <stdint.h>

#include "apa.h"

#define BOOTSTRAP_TRANSACTION_INVALID_ARGUMENT (-300)

typedef enum {
    BOOTSTRAP_TRANSACTION_STAGE_NONE = 0,
    BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD,
    BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET,
    BOOTSTRAP_TRANSACTION_STAGE_POINTER_VERIFY
} bootstrap_transaction_stage_t;

typedef struct {
    bootstrap_transaction_stage_t stage;
    int code;
} bootstrap_transaction_result_t;

typedef void (*bootstrap_transaction_release_fn)(void *context);

typedef struct {
    int (*write_payload_verified)(void *context,
                                  const unsigned char *payload,
                                  unsigned int payload_size,
                                  uint32_t start_sector);
    int (*set_osd_mbr)(void *context, uint32_t start, uint32_t size);
    int (*verify_osd_mbr)(void *context,
                          unsigned char destination[APA_HEADER_SIZE],
                          uint32_t expected_start,
                          uint32_t expected_size);
    void *context;
} bootstrap_transaction_ops_t;

/*
 * Commit one already-authorized pointer change and verify the resulting APA
 * header. The raw transport error is returned unchanged and result identifies
 * whether setting or read-back verification failed.
 */
int bootstrap_transaction_set_pointer(
    const bootstrap_transaction_ops_t *ops,
    unsigned char header[APA_HEADER_SIZE],
    uint32_t start, uint32_t size,
    bootstrap_transaction_result_t *result);

/*
 * Write+verify a payload, release its caller-owned backing allocation, then
 * and only then update+verify the pointer. release_payload is deliberately
 * called immediately after the payload step (including failure), preserving
 * Torii's payload-buffer lifetime while making ordering host-testable.
 */
int bootstrap_transaction_activate(
    const bootstrap_transaction_ops_t *ops,
    unsigned char header[APA_HEADER_SIZE],
    const unsigned char *payload, unsigned int payload_size,
    uint32_t start, uint32_t size,
    bootstrap_transaction_release_fn release_payload,
    void *release_context,
    bootstrap_transaction_result_t *result);

#endif
