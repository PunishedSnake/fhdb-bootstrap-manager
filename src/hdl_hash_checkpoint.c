#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hdl_hash_checkpoint.h"

#define HDL_HASH_CHECKPOINT_VERSION 1u
#define HDL_HASH_CHECKPOINT_HASH_OFFSET 224u
#define HDL_HASH_CHECKPOINT_TARGET_OFFSET 64u
#define HDL_HASH_CHECKPOINT_TARGET_BYTES HDL_TRANSACTION_TARGET_MAX
#define HDL_HASH_CHECKPOINT_STATE_OFFSET 100u
#define HDL_HASH_CHECKPOINT_TOTAL_OFFSET 132u
#define HDL_HASH_CHECKPOINT_BLOCK_USED_OFFSET 140u
#define HDL_HASH_CHECKPOINT_BLOCK_OFFSET 144u

static const unsigned char checkpoint_magic[8] = {
    'H', 'D', 'L', 'H', 'A', 'S', 'H', 1
};

static void write_le32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void write_le64(unsigned char *destination, uint64_t value)
{
    write_le32(destination, (uint32_t)value);
    write_le32(destination + 4, (uint32_t)(value >> 32));
}

static uint32_t read_le32(const unsigned char *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static uint64_t read_le64(const unsigned char *source)
{
    return (uint64_t)read_le32(source) |
           ((uint64_t)read_le32(source + 4) << 32);
}

static size_t bounded_length(const char *text, size_t capacity)
{
    const char *end = memchr(text, '\0', capacity);

    return end == NULL ? capacity : (size_t)(end - text);
}

static int checkpoint_progress(const hdl_transaction_t *transaction,
                               uint64_t *completed_bytes)
{
    if (transaction == NULL || completed_bytes == NULL ||
        transaction->source_bytes == 0 ||
        (transaction->source_bytes & 2047u) != 0 ||
        transaction->completed_sectors > transaction->total_sectors ||
        transaction->total_sectors != transaction->source_bytes / 2048u ||
        transaction->completed_sectors == 0 ||
        memchr(transaction->target, '\0', sizeof(transaction->target)) == NULL ||
        transaction->target[0] == '\0')
        return 0;

    *completed_bytes = transaction->completed_sectors * 2048u;
    return *completed_bytes <= transaction->source_bytes;
}

int hdl_hash_checkpoint_encode(
    const hdl_transaction_t *transaction,
    const sha256_context_t *context,
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE])
{
    unsigned char digest[32];
    uint64_t completed_bytes;
    size_t target_length;
    unsigned int i;

    if (record == NULL || context == NULL ||
        !checkpoint_progress(transaction, &completed_bytes))
        return HDL_HASH_CHECKPOINT_INVALID_ARGUMENT;
    if (context->total_bytes != completed_bytes || context->block_used > 63u ||
        context->block_used != (size_t)(completed_bytes & 63u))
        return HDL_HASH_CHECKPOINT_CONTEXT_MISMATCH;

    memset(record, 0, HDL_HASH_CHECKPOINT_RECORD_SIZE);
    memcpy(record, checkpoint_magic, sizeof(checkpoint_magic));
    write_le32(record + 8, HDL_HASH_CHECKPOINT_VERSION);
    write_le64(record + 16, transaction->source_bytes);
    write_le64(record + 24, completed_bytes);
    memcpy(record + 32, transaction->source_fingerprint,
           sizeof(transaction->source_fingerprint));
    target_length = bounded_length(transaction->target,
                                   sizeof(transaction->target));
    if (target_length >= HDL_HASH_CHECKPOINT_TARGET_BYTES)
        return HDL_HASH_CHECKPOINT_INVALID_ARGUMENT;
    memcpy(record + HDL_HASH_CHECKPOINT_TARGET_OFFSET,
           transaction->target, target_length);

    for (i = 0; i < 8u; i++)
        write_le32(record + HDL_HASH_CHECKPOINT_STATE_OFFSET + i * 4u,
                   context->state[i]);
    write_le64(record + HDL_HASH_CHECKPOINT_TOTAL_OFFSET,
               context->total_bytes);
    write_le32(record + HDL_HASH_CHECKPOINT_BLOCK_USED_OFFSET,
               (uint32_t)context->block_used);
    if (context->block_used != 0)
        memcpy(record + HDL_HASH_CHECKPOINT_BLOCK_OFFSET,
               context->block, context->block_used);

    sha256_buffer(record, HDL_HASH_CHECKPOINT_HASH_OFFSET, digest);
    memcpy(record + HDL_HASH_CHECKPOINT_HASH_OFFSET, digest, sizeof(digest));
    return 0;
}

int hdl_hash_checkpoint_restore(
    const unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE],
    const hdl_transaction_t *transaction,
    sha256_context_t *context)
{
    unsigned char digest[32];
    char target[HDL_TRANSACTION_TARGET_MAX];
    uint64_t completed_bytes;
    uint64_t stored_source_bytes;
    uint64_t stored_completed_bytes;
    uint64_t stored_total_bytes;
    uint32_t block_used;
    unsigned int i;

    if (record == NULL || context == NULL ||
        !checkpoint_progress(transaction, &completed_bytes))
        return HDL_HASH_CHECKPOINT_INVALID_ARGUMENT;
    if (memcmp(record, checkpoint_magic, sizeof(checkpoint_magic)) != 0 ||
        read_le32(record + 8) != HDL_HASH_CHECKPOINT_VERSION)
        return HDL_HASH_CHECKPOINT_INVALID_RECORD;

    sha256_buffer(record, HDL_HASH_CHECKPOINT_HASH_OFFSET, digest);
    if (memcmp(digest, record + HDL_HASH_CHECKPOINT_HASH_OFFSET,
               sizeof(digest)) != 0)
        return HDL_HASH_CHECKPOINT_HASH_MISMATCH;

    memset(target, 0, sizeof(target));
    memcpy(target, record + HDL_HASH_CHECKPOINT_TARGET_OFFSET,
           sizeof(target));
    if (memchr(target, '\0', sizeof(target)) == NULL)
        return HDL_HASH_CHECKPOINT_INVALID_RECORD;

    stored_source_bytes = read_le64(record + 16);
    stored_completed_bytes = read_le64(record + 24);
    if (stored_source_bytes != transaction->source_bytes ||
        stored_completed_bytes != completed_bytes ||
        memcmp(record + 32, transaction->source_fingerprint,
               sizeof(transaction->source_fingerprint)) != 0 ||
        strcmp(target, transaction->target) != 0)
        return HDL_HASH_CHECKPOINT_TRANSACTION_MISMATCH;

    stored_total_bytes = read_le64(record + HDL_HASH_CHECKPOINT_TOTAL_OFFSET);
    block_used = read_le32(record + HDL_HASH_CHECKPOINT_BLOCK_USED_OFFSET);
    if (stored_total_bytes != stored_completed_bytes || block_used > 63u ||
        block_used != (uint32_t)(stored_total_bytes & 63u))
        return HDL_HASH_CHECKPOINT_INVALID_RECORD;

    memset(context, 0, sizeof(*context));
    for (i = 0; i < 8u; i++)
        context->state[i] =
            read_le32(record + HDL_HASH_CHECKPOINT_STATE_OFFSET + i * 4u);
    context->total_bytes = stored_total_bytes;
    context->block_used = (size_t)block_used;
    if (block_used != 0)
        memcpy(context->block, record + HDL_HASH_CHECKPOINT_BLOCK_OFFSET,
               block_used);
    return 0;
}
