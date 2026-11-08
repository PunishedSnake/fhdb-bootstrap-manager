#ifndef PS2_HDD_BOOTSTRAP_MANAGER_DEV9_FINGERPRINT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_DEV9_FINGERPRINT_H

/*
 * Read-only identity of the expansion-bay / PCMCIA HDD adapter.
 *
 * Ported from PSBBN-DEV9-Lab. Only the SPEED identity registers are sampled;
 * nothing that can change device state (PIO data, interrupt status, DMA) is
 * touched. The PS2SDK ATAD driver distinguishes official Sony adapters from
 * GameStar-style compatible ones by the 16-bit SPEED value at 0x20, which
 * official adapters report as 0x0001.
 *
 * Classification is pure and host-tested; dev9_fingerprint_ps2.c gathers the
 * raw values on the console.
 */

#include <stdint.h>

#define DEV9_FP_MODEL_UNKNOWN (-1)
#define DEV9_FP_MODEL_PCMCIA 0
#define DEV9_FP_MODEL_EXPBAY 1

#define DEV9_FP_CAPS_SMAP 0x0001u
#define DEV9_FP_CAPS_ATA 0x0002u
#define DEV9_FP_CAPS_UART 0x0008u
#define DEV9_FP_CAPS_DVR 0x0010u
#define DEV9_FP_CAPS_FLASH 0x0020u
#define DEV9_FP_OFFICIAL_COMPAT 0x0001u

typedef enum {
    DEV9_ADAPTER_NOT_SAMPLED = 0,
    DEV9_ADAPTER_UNREADABLE,   /* registers float (0x0000 / 0xFFFF) */
    DEV9_ADAPTER_OFFICIAL,     /* SPEED[0x20] == 0x0001 */
    DEV9_ADAPTER_COMPATIBLE    /* any other identity: third-party */
} dev9_adapter_class_t;

typedef struct {
    int sampled;        /* 0 when DEV9 was not available to sample */
    int model;          /* DDIOC_MODEL result or DEV9_FP_MODEL_UNKNOWN */
    uint8_t spd_rev;
    uint16_t spd_rev1;
    uint16_t spd_rev3;  /* capability bits */
    uint16_t spd_rev8;
    uint16_t compat20;
} dev9_fingerprint_t;

dev9_adapter_class_t dev9_fingerprint_classify(const dev9_fingerprint_t *fp);
const char *dev9_adapter_class_name(dev9_adapter_class_t adapter);
const char *dev9_model_name(int model);

/*
 * Full PSBBN freezes at the PlayStation 2 logo on third-party adapters (its
 * Linux SMAP driver assumes Sony Ethernet hardware); HOSDMenu-only installs
 * work. Returns 1 when that warning applies to the collected evidence.
 */
int dev9_psbbn_adapter_risk(const dev9_fingerprint_t *fp, int psbbn_present);

#endif
