#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_SOURCE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_SOURCE_H

#define BOOTSTRAP_SOURCE_PATH_SIZE 64u

typedef enum {
    BOOTSTRAP_SOURCE_STAGE_NONE = 0,
    BOOTSTRAP_SOURCE_STAGE_LOAD,
    BOOTSTRAP_SOURCE_STAGE_KELF,
    BOOTSTRAP_SOURCE_STAGE_CAPACITY
} bootstrap_source_stage_t;

typedef struct {
    bootstrap_source_stage_t stage;
    int code;
    int getstat_result;
    unsigned int mbr_start;
    unsigned int mbr_size;
} bootstrap_source_result_t;

typedef struct {
    char path[BOOTSTRAP_SOURCE_PATH_SIZE];
    unsigned char *payload;
    unsigned int payload_size;
    unsigned int sectors;
} bootstrap_source_t;

/* Initialize the writable source path used by the MBR.XIN/XLF compatibility shim. */
void bootstrap_source_init(bootstrap_source_t *source, unsigned int storage);

/*
 * Load, bound-check, structurally validate, and capacity-check an installable
 * MBR KELF before MagicGate signing. No signing, confirmation, or HDD write is
 * performed here. Torii's -120..-123 load diagnostics and KELF result codes are
 * preserved in result->code.
 */
int bootstrap_source_prepare(unsigned int storage, bootstrap_source_t *source,
                             bootstrap_source_result_t *result);

void bootstrap_source_release(bootstrap_source_t *source);

#endif
