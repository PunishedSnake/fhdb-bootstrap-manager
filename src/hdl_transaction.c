/* Durable, checksummed transaction records for resumable HDL installs. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hdl_transaction.h"
#include "sha256.h"

#define HDL_TRANSACTION_LEGACY_HASH_OFFSET 480u
#define HDL_TRANSACTION_RECORD_FLAGS_OFFSET 476u
#define HDL_TRANSACTION_CHECKPOINT_OFFSET 480u
#define HDL_TRANSACTION_CURRENT_HASH_OFFSET 512u
#define HDL_TRANSACTION_FLAG_CHECKPOINT_VALID 0x00000001u
#define HDL_TRANSACTION_FLAG_ALLOCATION_ARMED 0x00000002u

static const unsigned char transaction_magic[8] = {
    'H', 'D', 'L', 'T', 'X', 'N', 1, 0
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

static int terminated(const char *text, size_t capacity)
{
    return memchr(text, '\0', capacity) != NULL;
}

static size_t bounded_length(const char *text, size_t capacity)
{
    const char *end = memchr(text, '\0', capacity);

    return end == NULL ? capacity : (size_t)(end - text);
}

static int transaction_valid(const hdl_transaction_t *transaction)
{
    if (transaction == NULL || transaction->stage < HDL_TRANSACTION_STAGE_PLANNED ||
        transaction->stage > HDL_TRANSACTION_STAGE_ABORTED ||
        transaction->source_bytes == 0 || transaction->total_sectors == 0 ||
        (transaction->source_bytes & 2047u) != 0 ||
        transaction->total_sectors != transaction->source_bytes / 2048u ||
        transaction->completed_sectors > transaction->total_sectors ||
        transaction->partition_count == 0 || transaction->partition_count > 65 ||
        !terminated(transaction->target, sizeof(transaction->target)) ||
        !terminated(transaction->startup, sizeof(transaction->startup)) ||
        !terminated(transaction->source_path, sizeof(transaction->source_path)) ||
        !terminated(transaction->game_title, sizeof(transaction->game_title)) ||
        transaction->target[0] == '\0' || transaction->startup[0] == '\0' ||
        transaction->source_path[0] == '\0' ||
        transaction->game_title[0] == '\0' ||
        (transaction->disc_type != 0x12u && transaction->disc_type != 0x14u))
        return 0;
    if (transaction->source_hash_checkpoint_valid &&
        (transaction->stage != HDL_TRANSACTION_STAGE_COPYING ||
         transaction->completed_sectors == 0))
        return 0;
    if (transaction->allocation_armed &&
        (transaction->stage != HDL_TRANSACTION_STAGE_PLANNED ||
         transaction->completed_sectors != 0))
        return 0;
    if (transaction->stage == HDL_TRANSACTION_STAGE_PLANNED ||
        transaction->stage == HDL_TRANSACTION_STAGE_PARTITIONS_CREATED)
        return transaction->completed_sectors == 0;
    if (transaction->stage >= HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED &&
        transaction->stage <= HDL_TRANSACTION_STAGE_COMPLETE)
        return transaction->completed_sectors == transaction->total_sectors;
    return 1;
}

int hdl_transaction_transition_allowed(hdl_transaction_stage_t from,
                                       hdl_transaction_stage_t to)
{
    if (from == HDL_TRANSACTION_STAGE_COPYING &&
        to == HDL_TRANSACTION_STAGE_COPYING)
        return 1;
    if (to == HDL_TRANSACTION_STAGE_ABORTED &&
        from >= HDL_TRANSACTION_STAGE_PLANNED &&
        from < HDL_TRANSACTION_STAGE_METADATA_COMMITTED)
        return 1;
    return (from == HDL_TRANSACTION_STAGE_PLANNED &&
            to == HDL_TRANSACTION_STAGE_PARTITIONS_CREATED) ||
           (from == HDL_TRANSACTION_STAGE_PARTITIONS_CREATED &&
            to == HDL_TRANSACTION_STAGE_COPYING) ||
           (from == HDL_TRANSACTION_STAGE_COPYING &&
            to == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) ||
           (from == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED &&
            to == HDL_TRANSACTION_STAGE_METADATA_COMMITTED) ||
           (from == HDL_TRANSACTION_STAGE_METADATA_COMMITTED &&
            to == HDL_TRANSACTION_STAGE_COMPLETE);
}

int hdl_transaction_set_stage(hdl_transaction_t *transaction,
                              hdl_transaction_stage_t stage,
                              uint64_t completed_sectors)
{
    hdl_transaction_t updated;

    if (transaction == NULL)
        return HDL_TRANSACTION_INVALID_ARGUMENT;
    if (!hdl_transaction_transition_allowed(transaction->stage, stage))
        return HDL_TRANSACTION_INVALID_STAGE;
    if (completed_sectors < transaction->completed_sectors)
        return HDL_TRANSACTION_PROGRESS_INVALID;
    updated = *transaction;
    updated.stage = stage;
    updated.completed_sectors = completed_sectors;
    if (transaction->stage == HDL_TRANSACTION_STAGE_PLANNED &&
        stage == HDL_TRANSACTION_STAGE_PARTITIONS_CREATED)
        updated.allocation_armed = 0;
    if (!transaction_valid(&updated))
        return HDL_TRANSACTION_PROGRESS_INVALID;
    *transaction = updated;
    return 0;
}

int hdl_transaction_encode(const hdl_transaction_t *transaction,
                           unsigned char record[HDL_TRANSACTION_RECORD_SIZE])
{
    unsigned char digest[32];
    uint32_t record_flags = 0;

    if (record == NULL || !transaction_valid(transaction))
        return HDL_TRANSACTION_INVALID_ARGUMENT;
    /* Every durable v4+ COPY progress record beyond offset zero must carry the
     * matching chaining state. Otherwise a record would claim checkpoint
     * resumability while silently forcing legacy prefix-rehash semantics. */
    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING &&
        transaction->completed_sectors != 0 &&
        !transaction->source_hash_checkpoint_valid)
        return HDL_TRANSACTION_INVALID_ARGUMENT;

    memset(record, 0, HDL_TRANSACTION_RECORD_SIZE);
    memcpy(record, transaction_magic, sizeof(transaction_magic));
    write_le32(record + 8, HDL_TRANSACTION_RECORD_VERSION_CURRENT);
    write_le32(record + 12, (uint32_t)transaction->stage);
    write_le64(record + 16, transaction->source_bytes);
    write_le64(record + 24, transaction->total_sectors);
    write_le64(record + 32, transaction->completed_sectors);
    write_le32(record + 40, transaction->partition_count);
    memcpy(record + 48, transaction->source_fingerprint, 32);
    memcpy(record + 80, transaction->target,
           bounded_length(transaction->target, sizeof(transaction->target)));
    memcpy(record + 116, transaction->startup,
           bounded_length(transaction->startup, sizeof(transaction->startup)));
    memcpy(record + 176, transaction->source_path,
           bounded_length(transaction->source_path,
                          sizeof(transaction->source_path)));
    memcpy(record + 336, transaction->game_title,
           bounded_length(transaction->game_title,
                          sizeof(transaction->game_title)));
    write_le32(record + 464, transaction->disc_type);
    write_le32(record + 468, transaction->layer1_start);
    record[472] = transaction->hdl_compat_flags;
    record[473] = transaction->opl_compat_flags;
    record[474] = transaction->dma_type;
    record[475] = transaction->dma_mode;
    if (transaction->source_hash_checkpoint_valid) {
        record_flags |= HDL_TRANSACTION_FLAG_CHECKPOINT_VALID;
        memcpy(record + HDL_TRANSACTION_CHECKPOINT_OFFSET,
               transaction->source_hash_checkpoint,
               sizeof(transaction->source_hash_checkpoint));
    }
    if (transaction->allocation_armed)
        record_flags |= HDL_TRANSACTION_FLAG_ALLOCATION_ARMED;
    write_le32(record + HDL_TRANSACTION_RECORD_FLAGS_OFFSET, record_flags);
    sha256_buffer(record, HDL_TRANSACTION_CURRENT_HASH_OFFSET, digest);
    memcpy(record + HDL_TRANSACTION_CURRENT_HASH_OFFSET,
           digest, sizeof(digest));
    return 0;
}

int hdl_transaction_decode(const unsigned char *record,
                           unsigned int record_size,
                           hdl_transaction_t *transaction)
{
    unsigned char digest[32];
    hdl_transaction_t decoded;
    uint32_t record_flags = 0;
    uint32_t allowed_flags = 0;
    uint32_t version;
    unsigned int hash_offset;
    unsigned int expected_size;

    if (record == NULL || transaction == NULL || record_size < 12u)
        return HDL_TRANSACTION_INVALID_ARGUMENT;
    if (memcmp(record, transaction_magic, sizeof(transaction_magic)) != 0)
        return HDL_TRANSACTION_INVALID_RECORD;
    version = read_le32(record + 8);
    if (version == HDL_TRANSACTION_RECORD_VERSION_LEGACY ||
        version == HDL_TRANSACTION_RECORD_VERSION_DIGEST) {
        expected_size = HDL_TRANSACTION_RECORD_SIZE_LEGACY;
        hash_offset = HDL_TRANSACTION_LEGACY_HASH_OFFSET;
    } else if (version == HDL_TRANSACTION_RECORD_VERSION_CHECKPOINT ||
               version == HDL_TRANSACTION_RECORD_VERSION_CURRENT) {
        expected_size = HDL_TRANSACTION_RECORD_SIZE;
        hash_offset = HDL_TRANSACTION_CURRENT_HASH_OFFSET;
    } else {
        return HDL_TRANSACTION_INVALID_RECORD;
    }
    if (record_size != expected_size)
        return HDL_TRANSACTION_INVALID_RECORD;

    sha256_buffer(record, hash_offset, digest);
    if (memcmp(digest, record + hash_offset, sizeof(digest)) != 0)
        return HDL_TRANSACTION_HASH_MISMATCH;

    memset(&decoded, 0, sizeof(decoded));
    decoded.stage = (hdl_transaction_stage_t)read_le32(record + 12);
    decoded.source_bytes = read_le64(record + 16);
    decoded.total_sectors = read_le64(record + 24);
    decoded.completed_sectors = read_le64(record + 32);
    decoded.partition_count = read_le32(record + 40);
    memcpy(decoded.source_fingerprint, record + 48, 32);
    memcpy(decoded.target, record + 80, sizeof(decoded.target) - 1);
    memcpy(decoded.startup, record + 116, sizeof(decoded.startup) - 1);
    memcpy(decoded.source_path, record + 176,
           sizeof(decoded.source_path) - 1);
    memcpy(decoded.game_title, record + 336,
           sizeof(decoded.game_title) - 1);
    decoded.disc_type = read_le32(record + 464);
    decoded.layer1_start = read_le32(record + 468);
    decoded.hdl_compat_flags = record[472];
    decoded.opl_compat_flags = record[473];
    decoded.dma_type = record[474];
    decoded.dma_mode = record[475];
    if (version == HDL_TRANSACTION_RECORD_VERSION_CHECKPOINT ||
        version == HDL_TRANSACTION_RECORD_VERSION_CURRENT) {
        record_flags = read_le32(record + HDL_TRANSACTION_RECORD_FLAGS_OFFSET);
        allowed_flags = HDL_TRANSACTION_FLAG_CHECKPOINT_VALID;
        if (version == HDL_TRANSACTION_RECORD_VERSION_CURRENT)
            allowed_flags |= HDL_TRANSACTION_FLAG_ALLOCATION_ARMED;
        if ((record_flags & ~allowed_flags) != 0)
            return HDL_TRANSACTION_INVALID_RECORD;
        if ((record_flags & HDL_TRANSACTION_FLAG_CHECKPOINT_VALID) != 0) {
            memcpy(decoded.source_hash_checkpoint,
                   record + HDL_TRANSACTION_CHECKPOINT_OFFSET,
                   sizeof(decoded.source_hash_checkpoint));
            decoded.source_hash_checkpoint_valid = 1;
        }
        if (version == HDL_TRANSACTION_RECORD_VERSION_CURRENT &&
            (record_flags & HDL_TRANSACTION_FLAG_ALLOCATION_ARMED) != 0)
            decoded.allocation_armed = 1;
        if (decoded.stage == HDL_TRANSACTION_STAGE_COPYING &&
            decoded.completed_sectors != 0 &&
            !decoded.source_hash_checkpoint_valid)
            return HDL_TRANSACTION_INVALID_RECORD;
    }
    decoded.record_version = version;
    if (!transaction_valid(&decoded))
        return HDL_TRANSACTION_INVALID_RECORD;
    *transaction = decoded;
    return 0;
}
