#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hdl_transaction.h"
#include "sha256.h"

#define HDL_TRANSACTION_TEST_LEGACY_HASH_OFFSET 480u
#define HDL_TRANSACTION_TEST_CURRENT_HASH_OFFSET 512u

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
    strcpy(transaction.source_path, "mass:/TEST.ISO");
    strcpy(transaction.game_title, "Test Game");
    transaction.disc_type = 0x14;
    transaction.layer1_start = 0x123456;
    transaction.hdl_compat_flags = 0x05;
    transaction.opl_compat_flags = 0x08;
    transaction.dma_type = 0x40;
    transaction.dma_mode = 4;
    for (i = 0; i < sizeof(transaction.source_fingerprint); i++)
        transaction.source_fingerprint[i] = (unsigned char)i;
    return transaction;
}

static void write_version(unsigned char *record, uint32_t version)
{
    record[8] = (unsigned char)version;
    record[9] = (unsigned char)(version >> 8);
    record[10] = (unsigned char)(version >> 16);
    record[11] = (unsigned char)(version >> 24);
}

static void make_legacy_record(const hdl_transaction_t *transaction,
                               uint32_t version,
                               unsigned char legacy[HDL_TRANSACTION_RECORD_SIZE_LEGACY])
{
    unsigned char current[HDL_TRANSACTION_RECORD_SIZE];
    unsigned char digest[32];

    assert(hdl_transaction_encode(transaction, current) == 0);
    memcpy(legacy, current, HDL_TRANSACTION_TEST_LEGACY_HASH_OFFSET);
    write_version(legacy, version);
    sha256_buffer(legacy, HDL_TRANSACTION_TEST_LEGACY_HASH_OFFSET, digest);
    memcpy(legacy + HDL_TRANSACTION_TEST_LEGACY_HASH_OFFSET,
           digest, sizeof(digest));
}

static void resign_current_record(unsigned char record[HDL_TRANSACTION_RECORD_SIZE])
{
    unsigned char digest[32];

    sha256_buffer(record, HDL_TRANSACTION_TEST_CURRENT_HASH_OFFSET, digest);
    memcpy(record + HDL_TRANSACTION_TEST_CURRENT_HASH_OFFSET,
           digest, sizeof(digest));
}

static void test_record_round_trip_and_corruption(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    assert(hdl_transaction_encode(&transaction, record) == 0);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) == 0);
    assert(decoded.record_version == HDL_TRANSACTION_RECORD_VERSION_CURRENT);
    assert(decoded.stage == transaction.stage);
    assert(decoded.source_bytes == transaction.source_bytes);
    assert(decoded.total_sectors == transaction.total_sectors);
    assert(decoded.completed_sectors == 0);
    assert(decoded.partition_count == 1);
    assert(strcmp(decoded.target, transaction.target) == 0);
    assert(strcmp(decoded.startup, transaction.startup) == 0);
    assert(strcmp(decoded.source_path, transaction.source_path) == 0);
    assert(strcmp(decoded.game_title, transaction.game_title) == 0);
    assert(decoded.disc_type == 0x14);
    assert(decoded.layer1_start == 0x123456);
    assert(decoded.hdl_compat_flags == 0x05);
    assert(decoded.opl_compat_flags == 0x08);
    assert(decoded.dma_type == 0x40);
    assert(decoded.dma_mode == 4);
    assert(memcmp(decoded.source_fingerprint,
                  transaction.source_fingerprint, 32) == 0);
    assert(!decoded.source_hash_checkpoint_valid);
    assert(!decoded.allocation_armed);

    record[90] ^= 0x80;
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) ==
           HDL_TRANSACTION_HASH_MISMATCH);
}

static void fill_copy_checkpoint(hdl_transaction_t *transaction)
{
    unsigned int i;

    assert(hdl_transaction_set_stage(
               transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0) == 0);
    assert(hdl_transaction_set_stage(
               transaction, HDL_TRANSACTION_STAGE_COPYING, 0) == 0);
    assert(hdl_transaction_set_stage(
               transaction, HDL_TRANSACTION_STAGE_COPYING, 1) == 0);
    for (i = 0; i < sizeof(transaction->source_hash_checkpoint); i++)
        transaction->source_hash_checkpoint[i] = (unsigned char)(0xa0u + i);
    transaction->source_hash_checkpoint_valid = 1;
}

static void test_current_copy_checkpoint_round_trip(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 0) == 0);
    assert(hdl_transaction_set_stage(
               &transaction, HDL_TRANSACTION_STAGE_COPYING, 1) == 0);
    assert(hdl_transaction_encode(&transaction, record) ==
           HDL_TRANSACTION_INVALID_ARGUMENT);

    transaction = planned_transaction();
    fill_copy_checkpoint(&transaction);
    assert(hdl_transaction_encode(&transaction, record) == 0);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) == 0);
    assert(decoded.record_version == HDL_TRANSACTION_RECORD_VERSION_CURRENT);
    assert(decoded.stage == HDL_TRANSACTION_STAGE_COPYING);
    assert(decoded.completed_sectors == 1);
    assert(decoded.source_hash_checkpoint_valid);
    assert(memcmp(decoded.source_hash_checkpoint,
                  transaction.source_hash_checkpoint, 32) == 0);
    assert(memcmp(decoded.source_fingerprint,
                  transaction.source_fingerprint, 32) == 0);
    assert(!decoded.allocation_armed);
}

static void test_v4_copy_checkpoint_remains_readable(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    fill_copy_checkpoint(&transaction);
    assert(hdl_transaction_encode(&transaction, record) == 0);
    write_version(record, HDL_TRANSACTION_RECORD_VERSION_CHECKPOINT);
    resign_current_record(record);

    assert(hdl_transaction_decode(record, sizeof(record), &decoded) == 0);
    assert(decoded.record_version == HDL_TRANSACTION_RECORD_VERSION_CHECKPOINT);
    assert(decoded.stage == HDL_TRANSACTION_STAGE_COPYING);
    assert(decoded.completed_sectors == 1);
    assert(decoded.source_hash_checkpoint_valid);
    assert(memcmp(decoded.source_hash_checkpoint,
                  transaction.source_hash_checkpoint, 32) == 0);
    assert(!decoded.allocation_armed);
}

static void test_v5_allocation_ownership_round_trip(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    transaction.allocation_armed = 1;
    assert(hdl_transaction_encode(&transaction, record) == 0);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) == 0);
    assert(decoded.record_version == HDL_TRANSACTION_RECORD_VERSION_CURRENT);
    assert(decoded.stage == HDL_TRANSACTION_STAGE_PLANNED);
    assert(decoded.allocation_armed);
    assert(!decoded.source_hash_checkpoint_valid);

    /* Successful creation transfers ownership from the PLANNED allocator
     * window to the PARTITIONS_CREATED stage in one portable state transition. */
    assert(hdl_transaction_set_stage(
               &decoded, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0) == 0);
    assert(!decoded.allocation_armed);

    decoded.allocation_armed = 1;
    assert(hdl_transaction_encode(&decoded, record) ==
           HDL_TRANSACTION_INVALID_ARGUMENT);
}

static void test_v4_rejects_v5_allocation_flag(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    transaction.allocation_armed = 1;
    assert(hdl_transaction_encode(&transaction, record) == 0);
    write_version(record, HDL_TRANSACTION_RECORD_VERSION_CHECKPOINT);
    resign_current_record(record);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) ==
           HDL_TRANSACTION_INVALID_RECORD);
}

static void test_legacy_records_remain_readable(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE_LEGACY];

    make_legacy_record(&transaction, HDL_TRANSACTION_RECORD_VERSION_LEGACY,
                       record);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) == 0);
    assert(decoded.record_version == HDL_TRANSACTION_RECORD_VERSION_LEGACY);
    assert(decoded.stage == transaction.stage);
    assert(memcmp(decoded.source_fingerprint,
                  transaction.source_fingerprint, 32) == 0);
    assert(!decoded.source_hash_checkpoint_valid);
    assert(!decoded.allocation_armed);

    make_legacy_record(&transaction, HDL_TRANSACTION_RECORD_VERSION_DIGEST,
                       record);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) == 0);
    assert(decoded.record_version == HDL_TRANSACTION_RECORD_VERSION_DIGEST);
    assert(memcmp(decoded.source_fingerprint,
                  transaction.source_fingerprint, 32) == 0);
    assert(!decoded.allocation_armed);
}

static void test_version_size_pairing_fails_closed(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char current[HDL_TRANSACTION_RECORD_SIZE];
    unsigned char legacy[HDL_TRANSACTION_RECORD_SIZE_LEGACY];

    assert(hdl_transaction_encode(&transaction, current) == 0);
    assert(hdl_transaction_decode(current, HDL_TRANSACTION_RECORD_SIZE_LEGACY,
                                  &decoded) == HDL_TRANSACTION_INVALID_RECORD);

    make_legacy_record(&transaction, HDL_TRANSACTION_RECORD_VERSION_DIGEST,
                       legacy);
    assert(hdl_transaction_decode(legacy, HDL_TRANSACTION_RECORD_SIZE,
                                  &decoded) == HDL_TRANSACTION_INVALID_RECORD);
}

static void test_unknown_record_version_fails_closed(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    assert(hdl_transaction_encode(&transaction, record) == 0);
    write_version(record, 99u);
    resign_current_record(record);
    assert(hdl_transaction_decode(record, sizeof(record), &decoded) ==
           HDL_TRANSACTION_INVALID_RECORD);
}

static void test_every_record_byte_is_authenticated(void)
{
    hdl_transaction_t transaction = planned_transaction();
    hdl_transaction_t decoded;
    unsigned char original[HDL_TRANSACTION_RECORD_SIZE];
    unsigned char damaged[HDL_TRANSACTION_RECORD_SIZE];
    unsigned int i;

    transaction.allocation_armed = 1;
    assert(hdl_transaction_encode(&transaction, original) == 0);
    for (i = 0; i < sizeof(original); i++) {
        memcpy(damaged, original, sizeof(damaged));
        damaged[i] ^= 0x01;
        assert(hdl_transaction_decode(damaged, sizeof(damaged), &decoded) < 0);
    }
}

static void test_size_and_sector_count_must_agree(void)
{
    hdl_transaction_t transaction = planned_transaction();
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];

    transaction.source_bytes++;
    assert(hdl_transaction_encode(&transaction, record) ==
           HDL_TRANSACTION_INVALID_ARGUMENT);
    transaction = planned_transaction();
    transaction.total_sectors++;
    assert(hdl_transaction_encode(&transaction, record) ==
           HDL_TRANSACTION_INVALID_ARGUMENT);
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
    test_current_copy_checkpoint_round_trip();
    test_v4_copy_checkpoint_remains_readable();
    test_v5_allocation_ownership_round_trip();
    test_v4_rejects_v5_allocation_flag();
    test_legacy_records_remain_readable();
    test_version_size_pairing_fails_closed();
    test_unknown_record_version_fails_closed();
    test_every_record_byte_is_authenticated();
    test_size_and_sector_count_must_agree();
    test_legal_install_sequence();
    test_illegal_transitions_fail_closed();
    test_verified_requires_complete_payload();
    puts("All HDL transaction journal tests passed.");
    return 0;
}
