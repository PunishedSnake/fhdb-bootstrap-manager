/*
 * EE/IOP platform lifecycle for PS2 HDD Bootstrap Manager.
 *
 * This module owns IOP reset/module startup and controller initialization.
 * It intentionally contains no HDD policy: storage safety and APA ordering stay
 * outside the platform layer.
 */

#include <tamtypes.h>
#include <kernel.h>
#include <delaythread.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <libpad.h>

#include "platform.h"

/* Controller DMA memory must remain aligned and alive for padPortOpen(). */
static unsigned char pad_buffer[256] __attribute__((aligned(64)));

/* IRX modules embedded into the ELF by the Makefile's bin2c rules. */
extern unsigned char iomanX_irx[];
extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];
extern unsigned int size_fileXio_irx;
extern unsigned char secrman_irx[];
extern unsigned int size_secrman_irx;
extern unsigned char freesio2_irx[];
extern unsigned int size_freesio2_irx;
extern unsigned char freepad_irx[];
extern unsigned int size_freepad_irx;
extern unsigned char mcman_irx[];
extern unsigned int size_mcman_irx;
extern unsigned char mcserv_irx[];
extern unsigned int size_mcserv_irx;
extern unsigned char secrsif_irx[];
extern unsigned int size_secrsif_irx;
extern unsigned char poweroff_irx[];
extern unsigned int size_poweroff_irx;
extern unsigned char bdm_irx[];
extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];
extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;
extern unsigned char usbmass_bd_irx[];
extern unsigned int size_usbmass_bd_irx;
extern unsigned char ps2dev9_irx[];
extern unsigned int size_ps2dev9_irx;
extern unsigned char ps2atad_irx[];
extern unsigned int size_ps2atad_irx;
extern unsigned char ps2hdd_irx[];
extern unsigned int size_ps2hdd_irx;
extern unsigned char ps2fs_irx[];
extern unsigned int size_ps2fs_irx;

/* Reset the IOP and enable loading embedded homebrew IRX modules. */
void reset_iop(void)
{
    sceSifInitRpc(0);
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
    sceSifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

/* Execute an embedded IRX and treat either RPC or module-start failure as fatal. */
static int exec_irx(void *buffer, unsigned int size)
{
    int module_result = 0;
    int module_id = SifExecModuleBuffer(buffer, size, 0, NULL, &module_result);

    if (module_id < 0)
        return module_id;
    return module_result < 0 ? module_result : 0;
}

/* Load modules in the exact dependency order used by the stable Torii line. */
int load_modules(void)
{
    if (exec_irx(iomanX_irx, size_iomanX_irx) < 0) return -1;
    if (exec_irx(fileXio_irx, size_fileXio_irx) < 0) return -2;
    if (exec_irx(secrman_irx, size_secrman_irx) < 0) return -3;
    if (exec_irx(freesio2_irx, size_freesio2_irx) < 0) return -4;
    if (exec_irx(freepad_irx, size_freepad_irx) < 0) return -5;
    if (exec_irx(mcman_irx, size_mcman_irx) < 0) return -6;
    if (exec_irx(mcserv_irx, size_mcserv_irx) < 0) return -7;
    if (exec_irx(secrsif_irx, size_secrsif_irx) < 0) return -8;
    if (exec_irx(poweroff_irx, size_poweroff_irx) < 0) return -9;
    if (exec_irx(bdm_irx, size_bdm_irx) < 0) return -10;
    if (exec_irx(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx) < 0) return -11;
    if (exec_irx(usbd_irx, size_usbd_irx) < 0) return -12;
    if (exec_irx(usbmass_bd_irx, size_usbmass_bd_irx) < 0) return -13;
    if (exec_irx(ps2dev9_irx, size_ps2dev9_irx) < 0) return -14;
    if (exec_irx(ps2atad_irx, size_ps2atad_irx) < 0) return -15;
    if (exec_irx(ps2hdd_irx, size_ps2hdd_irx) < 0) return -16;
    if (exec_irx(ps2fs_irx, size_ps2fs_irx) < 0) return -17;
    return 0;
}

/* Wait for controller port 0, slot 0 to reach a readable state. */
static int wait_pad_ready(void)
{
    int state;
    int timeout = 500000;

    while (timeout-- > 0) {
        state = padGetState(0, 0);
        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1)
            return 0;
        DelayThread(10);
    }
    return -1;
}

/* Initialize the first controller using statically allocated DMA memory. */
int init_pad(void)
{
    padInit(0);
    if (!padPortOpen(0, 0, pad_buffer))
        return -1;
    return wait_pad_ready();
}

/* Block until a new button edge is observed, avoiding repeated menu actions. */
u32 wait_for_press(void)
{
    struct padButtonStatus buttons;
    static u32 previous = 0;

    for (;;) {
        if (wait_pad_ready() == 0 && padRead(0, 0, &buttons) != 0) {
            u32 current = 0xffffu ^ buttons.btns;
            u32 pressed = current & ~previous;
            previous = current;
            if (pressed)
                return pressed;
        }
        DelayThread(16000);
    }
}

/* Require a multi-button hold; TRIANGLE always cancels safely. */
int wait_for_chord(u32 chord)
{
    struct padButtonStatus buttons;

    for (;;) {
        if (wait_pad_ready() == 0 && padRead(0, 0, &buttons) != 0) {
            u32 held = 0xffffu ^ buttons.btns;
            if (held & PAD_TRIANGLE)
                return 0;
            if ((held & chord) == chord)
                return 1;
        }
        DelayThread(16000);
    }
}
