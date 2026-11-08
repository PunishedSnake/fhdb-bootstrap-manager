/*
 * Console-side sampling for dev9_fingerprint.
 *
 * Safety contract (stricter than the PSBBN-DEV9-Lab probe it came from):
 *
 * - The bus is never touched unless the embedded ps2dev9 driver answers
 *   DDIOC_MODEL, i.e. DEV9 is loaded and powered. An unpowered SPEED block
 *   must not be read from the EE.
 * - Only identity registers are read: REV, REV_1, REV_3 (capabilities),
 *   REV_8 and the 0x20 compatibility word that PS2SDK's ATAD itself uses.
 *   PIO data, interrupt status, DMA and transfer control are never read.
 * - This runs only from user-requested diagnostics, on the synchronous EE
 *   thread, when no HDD transfer is in flight.
 *
 * SPD_REGBASE for the EE is 0xb4000000 in PS2SDK's speedregs.h.
 */

#include <tamtypes.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include <string.h>

#include "dev9_fingerprint_ps2.h"

#define DEV9_FP_EE_SPEED_BASE 0xb4000000u

static inline u8 speed_read8(u32 offset)
{
    return *(volatile u8 *)(DEV9_FP_EE_SPEED_BASE + offset);
}

static inline u16 speed_read16(u32 offset)
{
    return *(volatile u16 *)(DEV9_FP_EE_SPEED_BASE + offset);
}

void dev9_fingerprint_sample(dev9_fingerprint_t *fingerprint)
{
    int model;

    if (fingerprint == NULL)
        return;
    memset(fingerprint, 0, sizeof(*fingerprint));
    fingerprint->model = DEV9_FP_MODEL_UNKNOWN;
    model = fileXioDevctl("dev9x:", DDIOC_MODEL, NULL, 0, NULL, 0);
    if (model < 0)
        return;
    fingerprint->model = model;
    fingerprint->spd_rev = speed_read8(0x00u);
    fingerprint->spd_rev1 = speed_read16(0x02u);
    fingerprint->spd_rev3 = speed_read16(0x04u);
    fingerprint->spd_rev8 = speed_read16(0x0eu);
    fingerprint->compat20 = speed_read16(0x20u);
    fingerprint->sampled = 1;
}
