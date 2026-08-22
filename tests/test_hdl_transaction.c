#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hdl_transaction.h"

static hdl_transaction_t planned_transaction(void)
{
    hdl_transaction_t transaction;
    unsigned int i;

    memset(&transaction, 0, sizeof(transaction));
    transaction.stage = HDL_TRANSACTION_STAGE_PLANNED;
    transaction.source_bytes = 4096;
    transaction.total_sectors = 2;
    transaction.partition_count = 1;
    strcpy(transaction.target, "PP.SLUS-12345.HDL.TEST");
    strcpy(transaction.startup, "SLUS_123.45");
    for (i = 0; i < sizeof(transaction.source_fingerprint); i++)
        transaction.source_fingerprint[i] = (unsigned char)i;
    return transaction;
}

static void test_record_round_trip_and_corruption(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    assert(hdl_transaction_encode(&transaction, record) == 0);
    assert(hdl_transaction_decode(record, &decoded) == 0);
    assert(decoded.stage == transaction.stage);
    assert(decoded.source_bytes == transaction.source_bytes);
    assert(decoded.total_sectors == transaction.total_sectors);
    assert(decoded.completed_sectors == 0);
    assert(decoded.partition_count == 1);
    assert(strcmp(decoded.target, transaction.target) == 0);
    assert(strcmp(decoded.startup, transaction.startup) == 0);
    assert(memcmp(decoded.source_fingerprint,
                  transaction.source_fingerprint, 32) == 0);

    record[90] ^= 0x80;
    assert(hdl_transaction_decode(record, &decoded) ==
           HDL_TRANSACTION_HASH_MISMATCH);
}

static void test_legal_install_sequence(void)
{
    hdl_transaction_t transaction = planned_transaction();

    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 1) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 0) ==
           HDL_TRANSACTION_PROGRESS_INVALID);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED, 2) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_METADATA_COMMITTED, 2) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COMPLETE, 2) == 0);
}

static void test_illegal_transitions_fail_closed(void)
{
    hdl_transaction_t transaction = planned_transaction();

    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 0) ==
           HDL_TRANSACTION_INVALID_STAGE);
    assert(transaction.stage == HDL_TRANSACTION_STAGE_PLANNED);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 1) ==
           HDL_TRANSACTION_PROGRESS_INVALID);
    assert(transaction.stage == HDL_TRANSACTION_STAGE_PLANNED);

    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_ABORTED, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PLANNED, 0) ==
           HDL_TRANSACTION_INVALID_STAGE);
}

static void test_verified_requires_complete_payload(void)
{
    hdl_transaction_t transaction = planned_transaction();

    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED, 1) ==
           HDL_TRANSACTION_PROGRESS_INVALID);
}

int main(void)
{
    test_record_round_trip_and_corruption();
    test_legal_install_sequence();
    test_illegal_transitions_fail_closed();
    test_verified_requires_complete_payload();
    puts("All HDL transaction journal tests passed.");
    return 0;
}
