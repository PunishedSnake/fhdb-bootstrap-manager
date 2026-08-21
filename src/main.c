/*
 * PS2 HDD Bootstrap Manager application composition root.
 *
 * main.c now owns only startup, normal APA admission and composition. All
 * post-admission navigation lives in manager_menu_ps2; storage/bootstrap,
 * diagnostics and recovery policies remain in their dedicated controllers.
 *
 * Normal Torii-compatible operations never raw-write APA master sectors.
 * Michishirube's exceptional recovery writers are separately planner-gated,
 * externally snapshotted, read-back verified and followed by a mandatory
 * restart before normal ps2hdd use continues.
 */

#include <kernel.h>
#include <debug.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>

#include "apa.h"
#include "app_identity.h"
#include "app_ui_ps2.h"
#include "bootstrap_signing.h"
#include "boot_chain.h"
#include "diagnostics_controller_ps2.h"
#include "hdd_read.h"
#include "manager_menu_ps2.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static boot_chain_info_t boot_chain;

static int read_header(unsigned char *destination)
{
    return hdd_read_raw_sectors(0, 2, destination);
}

int main(int argc, char **argv)
{
    int result;
    int hdd_status;

    select_launch_storage(argc, argv);

    init_scr();
    scr_printf(APP_NAME " v%s\n", APP_VERSION);
    scr_printf("Initializing...\n");

    reset_iop();
    result = load_modules();
    if (result < 0) {
        scr_printf("ERROR: Could not load required IOP modules.\n");
        scr_printf("Code: %d\nPower off with the console button.\n", result);
        SleepThread();
    }

    fileXioInit();
    poweroffInit();
    bootstrap_signing_init();
    if (init_pad() < 0) {
        scr_printf("ERROR: Controller 1 is not available.\n");
        scr_printf("Power off with the console button.\n");
        SleepThread();
    }

    /* hdd_recovery_wrap intercepts only this first HDIOC_STATUS. A readable
       but damaged master can enter the narrowly guarded startup recovery path
       before normal admission rejects it. */
    hdd_status = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    if (hdd_status != 0)
        app_ui_fatal_screen(
            "HDD is missing, locked, or not valid APA.", hdd_status);

    result = read_header(header_buffer);
    if (result < 0)
        app_ui_fatal_screen("Could not read sectors 0-1.", result);
    if (!is_standard_apa_header(header_buffer))
        app_ui_fatal_screen("Invalid APA __mbr header or checksum.", -101);
    if (is_hybrid_gpt(header_buffer))
        app_ui_fatal_screen("Hybrid APA/GPT layout is not supported.", -102);

    session_log_line("Session started: %s v%s; launch storage=%s", APP_NAME,
                     APP_VERSION,
                     storage_targets[storage_selected()].name);
    session_log_line("APA header valid; osdStart=0x%08x; osdSize=0x%08x",
                     (unsigned int)read_le32(
                         header_buffer + APA_OSD_START_OFFSET),
                     (unsigned int)read_le32(
                         header_buffer + APA_OSD_SIZE_OFFSET));

    pad_activity_begin();
    diagnostics_controller_refresh(header_buffer, &boot_chain, 1);
    pad_activity_end();

    manager_menu_run(header_buffer, &boot_chain);
    return 0;
}