#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_TRANSACTION_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_TRANSACTION_H

#include <stdint.h>

#define HDL_TRANSACTION_RECORD_SIZE 512u
#define HDL_TRANSACTION_TARGET_MAX 33u
#define HDL_TRANSACTION_STARTUP_MAX 60u
#define HDL_TRANSACTION_SOURCE_PATH_MAX 160u
#define HDL_TRANSACTION_GAME_TITLE_MAX 128u

enum {
    HDL_TRANSACTION_INVALID_ARGUMENT = -540,
    HDL_TRANSACTION_INVALID_RECORD = -541,
    HDL_TRANSACTION_HASH_MISMATCH = -542,
    HDL_TRANSACTION_INVALID_STAGE = -543,
    HDL_TRANSACTION_PROGRESS_INVALID = -544
};

typedef enum {
    HDL_TRANSACTION_STAGE_EMPTY = 0,
    HDL_TRANSACTION_STAGE_PLANNED = 1,
    HDL_TRANSACTION_STAGE_PARTITIONS_CREATED = 2,
    HDL_TRANSACTION_STAGE_COPYING = 3,
    HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED = 4,
    HDL_TRANSACTION_STAGE_METADATA_COMMITTED = 5,
    HDL_TRANSACTION_STAGE_COMPLETE = 6,
    HDL_TRANSACTION_STAGE_ABORTED = 7
} hdl_transaction_stage_t;

typedef struct {
    hdl_transaction_stage_t stage;
    uint64_t source_bytes;
    uint64_t total_sectors;
    uint64_t completed_sectors;
    uint32_t partition_count;
    unsigned char source_fingerprint[32];
    char target[HDL_TRANSACTION_TARGET_MAX];
    char startup[HDL_TRANSACTION_STARTUP_MAX];
    char source_path[HDL_TRANSACTION_SOURCE_PATH_MAX];
    char game_title[HDL_TRANSACTION_GAME_TITLE_MAX];
    uint32_t disc_type;
    uint32_t layer1_start;
    uint8_t hdl_compat_flags;
    uint8_t opl_compat_flags;
    uint8_t dma_type;
    uint8_t dma_mode;
} hdl_transaction_t;

int hdl_transaction_transition_allowed(hdl_transaction_stage_t from,
                                       hdl_transaction_stage_t to);
int hdl_transaction_set_stage(hdl_transaction_t *transaction,
                              hdl_transaction_stage_t stage,
                              uint64_t completed_sectors);
int hdl_transaction_encode(const hdl_transaction_t *transaction,
                           unsigned char record[HDL_TRANSACTION_RECORD_SIZE]);
int hdl_transaction_decode(const unsigned char record[HDL_TRANSACTION_RECORD_SIZE],
                           hdl_transaction_t *transaction);

#endif
