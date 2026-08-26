#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hdl_hash_checkpoint.h"

static hdl_transaction_t copying_transaction(void)
{
    hdl_transaction_t transaction;
    unsigned int i;

    memset(&transaction, 0, sizeof(transaction));
    transaction.stage = HDL_TRANSACTION_STAGE_COPYING;
    transaction.source_bytes = 8192;
    transaction.total_sectors = 4;
    transaction.completed_sectors = 2;
    transaction.partition_count = 1;
    strcpy(transaction.target, "PP.SLUS-12345.HDL.TEST");
    strcpy(transaction.startup, "SLUS_123.45");
    strcpy(transaction.source_path, "mass:/TEST.ISO");
    strcpy(transaction.game_title, "Test Game");
    transaction.disc_type = 0x14;
    for (i = 0; i < sizeof(transaction.source_fingerprint); i++)
        transaction.source_fingerprint[i] = (unsigned char)(0xa0u + i);
    return transaction;
}

static void fill_payload(unsigned char *payload, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++)
        payload[i] = (unsigned char)((i * 37u + i / 17u) & 0xffu);
}

static void test_restore_continues_same_digest(void)
{
    hdl_transaction_t transaction = copying_transaction();
    unsigned char payload[8192];
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    unsigned char expected[32];
    unsigned char resumed_digest[32];
    sha256_context_t original;
    sha256_context_t resumed;

    fill_payload(payload, sizeof(payload));
    sha256_buffer(payload, sizeof(payload), expected);

    sha256_init(&original);
    sha256_update(&original, payload, 4096);
    assert(original.total_bytes == 4096);
    assert(original.block_used == 0);
    assert(hdl_hash_checkpoint_encode(&transaction, &original, record) == 0);

    memset(&resumed, 0xcc, sizeof(resumed));
    assert(hdl_hash_checkpoint_restore(record, &transaction, &resumed) == 0);
    assert(resumed.total_bytes == original.total_bytes);
    assert(resumed.block_used == original.block_used);
    assert(memcmp(resumed.state, original.state, sizeof(original.state)) == 0);

    sha256_update(&resumed, payload + 4096, sizeof(payload) - 4096);
    sha256_final(&resumed, resumed_digest);
    assert(memcmp(expected, resumed_digest, sizeof(expected)) == 0);
}

static void test_every_record_byte_is_authenticated(void)
{
    hdl_transaction_t transaction = copying_transaction();
    unsigned char payload[4096];
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    unsigned char damaged[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    sha256_context_t hash;
    sha256_context_t restored;
    unsigned int i;

    fill_payload(payload, sizeof(payload));
    sha256_init(&hash);
    sha256_update(&hash, payload, sizeof(payload));
    assert(hdl_hash_checkpoint_encode(&transaction, &hash, record) == 0);

    for (i = 0; i < sizeof(record); i++) {
        memcpy(damaged, record, sizeof(damaged));
        damaged[i] ^= 0x01u;
        assert(hdl_hash_checkpoint_restore(damaged, &transaction, &restored) < 0);
    }
}

static void test_transaction_identity_must_match(void)
{
    hdl_transaction_t transaction = copying_transaction();
    hdl_transaction_t wrong;
    unsigned char payload[4096];
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    sha256_context_t hash;
    sha256_context_t restored;

    fill_payload(payload, sizeof(payload));
    sha256_init(&hash);
    sha256_update(&hash, payload, sizeof(payload));
    assert(hdl_hash_checkpoint_encode(&transaction, &hash, record) == 0);

    wrong = transaction;
    wrong.completed_sectors = 1;
    assert(hdl_hash_checkpoint_restore(record, &wrong, &restored) ==
           HDL_HASH_CHECKPOINT_TRANSACTION_MISMATCH);

    wrong = transaction;
    wrong.source_fingerprint[7] ^= 0x80u;
    assert(hdl_hash_checkpoint_restore(record, &wrong, &restored) ==
           HDL_HASH_CHECKPOINT_TRANSACTION_MISMATCH);

    wrong = transaction;
    strcpy(wrong.target, "PP.SLUS-12345.OTHER");
    assert(hdl_hash_checkpoint_restore(record, &wrong, &restored) ==
           HDL_HASH_CHECKPOINT_TRANSACTION_MISMATCH);
}

static void test_context_progress_must_match_transaction(void)
{
    hdl_transaction_t transaction = copying_transaction();
    unsigned char payload[2048];
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    sha256_context_t hash;

    fill_payload(payload, sizeof(payload));
    sha256_init(&hash);
    sha256_update(&hash, payload, sizeof(payload));
    assert(hdl_hash_checkpoint_encode(&transaction, &hash, record) ==
           HDL_HASH_CHECKPOINT_CONTEXT_MISMATCH);

    transaction.completed_sectors = 0;
    sha256_init(&hash);
    assert(hdl_hash_checkpoint_encode(&transaction, &hash, record) ==
           HDL_HASH_CHECKPOINT_INVALID_ARGUMENT);
}

static void test_full_payload_checkpoint_can_finalize(void)
{
    hdl_transaction_t transaction = copying_transaction();
    unsigned char payload[8192];
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    unsigned char expected[32];
    unsigned char restored_digest[32];
    sha256_context_t hash;
    sha256_context_t restored;

    transaction.completed_sectors = transaction.total_sectors;
    fill_payload(payload, sizeof(payload));
    sha256_buffer(payload, sizeof(payload), expected);
    sha256_init(&hash);
    sha256_update(&hash, payload, sizeof(payload));
    assert(hdl_hash_checkpoint_encode(&transaction, &hash, record) == 0);
    assert(hdl_hash_checkpoint_restore(record, &transaction, &restored) == 0);
    sha256_final(&restored, restored_digest);
    assert(memcmp(expected, restored_digest, sizeof(expected)) == 0);
}

int main(void)
{
    test_restore_continues_same_digest();
    test_every_record_byte_is_authenticated();
    test_transaction_identity_must_match();
    test_context_progress_must_match_transaction();
    test_full_payload_checkpoint_can_finalize();
    puts("All HDL hash checkpoint tests passed.");
    return 0;
}
