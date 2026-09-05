#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_HASH_CHECKPOINT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_HASH_CHECKPOINT_H

#include "hdl_transaction.h"
#include "sha256.h"

#define HDL_HASH_CHECKPOINT_RECORD_SIZE 256u

enum {
    HDL_HASH_CHECKPOINT_INVALID_ARGUMENT = -545,
    HDL_HASH_CHECKPOINT_INVALID_RECORD = -546,
    HDL_HASH_CHECKPOINT_HASH_MISMATCH = -547,
    HDL_HASH_CHECKPOINT_TRANSACTION_MISMATCH = -548,
    HDL_HASH_CHECKPOINT_CONTEXT_MISMATCH = -549
};

/*
 * Portable checkpoint for the streaming source SHA-256 state used by a
 * resumable HDL copy. The record is deliberately separate from the stable
 * 512-byte transaction journal so old journals remain readable.
 *
 * A checkpoint is an optimization hint, never transaction authority. Restore
 * succeeds only when source size, completed byte count, source fingerprint and
 * target ID match the already authenticated transaction journal.
 */
int hdl_hash_checkpoint_encode(
    const hdl_transaction_t *transaction,
    const sha256_context_t *context,
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE]);

int hdl_hash_checkpoint_restore(
    const unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE],
    const hdl_transaction_t *transaction,
    sha256_context_t *context);

#endif
