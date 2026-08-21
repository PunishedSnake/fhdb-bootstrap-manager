#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_SIGNING_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOTSTRAP_SIGNING_H

typedef enum {
    BOOTSTRAP_SIGNING_STAGE_NONE = 0,
    BOOTSTRAP_SIGNING_STAGE_MAGICGATE,
    BOOTSTRAP_SIGNING_STAGE_KELF
} bootstrap_signing_stage_t;

typedef struct {
    bootstrap_signing_stage_t stage;
    int code;
} bootstrap_signing_result_t;

/* Preserve Torii's one-time secrman initialization timing. */
void bootstrap_signing_init(void);

/*
 * Sign one already validated MBR KELF through the selected PS2 memory-card
 * port, then structurally validate the mutated KELF before it can be written.
 * This module owns no UI, card-selection policy, HDD access, or transaction.
 * A MagicGate NULL result is reported by stage with code 0, matching the old
 * call site which had no numeric secrman error to preserve.
 */
int bootstrap_signing_sign(int memory_card_port, unsigned char *payload,
                           unsigned int payload_size,
                           bootstrap_signing_result_t *result);

#endif
