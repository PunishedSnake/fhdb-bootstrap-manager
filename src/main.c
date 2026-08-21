/*
 * PS2 HDD Bootstrap Manager application composition root.
 *
 * main.c owns startup, the normal APA admission gate, menu state, and dispatch.
 * Backup/disable/restore/install workflows, diagnostics presentation, and
 * shared UI/lifecycle behavior live in dedicated controllers.
 *
 * Normal Torii-compatible operations never raw-write the APA master sectors:
 * payload writes stay in the reserved __mbr program area and pointer changes
 * go through ps2hdd. Michishirube adds one explicit exception before/around
 * normal admission: planner-approved master-header recovery may rewrite raw
 * sectors 0-1 only after an exact HDDRAW*.BIN snapshot and read-back verify.
 */

#include <tamtypes.h>
#include <kernel.h>
#include <debug.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>

#include "apa.h"
#include "app_identity.h"
#include "app_ui_ps2.h"
#include "bootstrap_controller_ps2.h"
#include "bootstrap_signing.h"
#include "boot_chain.h"
#include "boot_report_session.h"
#include "diagnostics_controller_ps2.h"
#include "hdd_read.h"
#include "platform.h"
#include "repair_controller_ps2.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static boot_chain_info_t boot_chain;

static int read_header(unsigned char *destination)
{
    return hdd_read_raw_sectors(0, 2, destination);
}

static void run_health_repair(void)
{
    int result = repair_controller_health(header_buffer, &boot_chain);

    if (result == REPAIR_CONTROLLER_REQUEST_DISABLE) {
        bootstrap_controller_disable(header_buffer, &boot_chain);
        return;
    }
    if (result == REPAIR_CONTROLLER_RESTART_REQUIRED) {
        app_ui_restart_to_browser();
        return;
    }
    if (result == REPAIR_CONTROLLER_BLOCKED)
        app_ui_fatal_screen("HDD health analysis failed closed.", result);
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

    /*
     * hdd_recovery_wrap intercepts only this first HDIOC_STATUS. A readable
     * but damaged master header may enter the guarded recovery controller
     * before the unchanged normal admission gate below rejects it.
     */
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
    diagnostics_controller_refresh(header_buffer, &boot_chain, 1);

    for (;;) {
        unsigned int start = read_le32(header_buffer + APA_OSD_START_OFFSET);
        unsigned int size = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("APA header: valid\n");
        scr_printf("osdStart : 0x%08x\n", start);
        scr_printf("osdSize  : 0x%08x\n", size);
        scr_printf("Storage  : %s\n", storage_targets[storage_selected()].name);
        scr_printf("Detected : %s\n", boot_chain.family);
        scr_printf("Report   : %s\n\n",
                   boot_report_session_last_save_result() == 0
                       ? "saved" : "not saved");

        if (start != 0 || size != 0) {
            scr_printf("HDD bootstrap is ENABLED.\n\n");
            scr_printf("X        Back up and disable\n");
        } else {
            scr_printf("HDD bootstrap is DISABLED.\n\n");
            scr_printf("SQUARE   Restore rescue / legacy pointer\n");
            scr_printf("CIRCLE   Sign and install MBR.XIN/XLF\n");
        }
        scr_printf("START    Create full rescue backup\n");
        scr_printf("R1       Inspect boot chain / save reports\n");
        scr_printf("L2       HDD structure health / repair\n");
        scr_printf("SELECT   Change storage device\n");
        scr_printf("TRIANGLE Power / restart menu\n");

        pressed = wait_for_press();
        if (pressed & PAD_START) {
            bootstrap_controller_backup_current(header_buffer, &boot_chain);
            continue;
        }
        if (pressed & PAD_R1) {
            diagnostics_controller_screen(header_buffer, &boot_chain);
            continue;
        }
        if (pressed & PAD_L2) {
            run_health_repair();
            continue;
        }
        if (pressed & PAD_SELECT) {
            app_ui_choose_storage();
            continue;
        }
        if (pressed & PAD_TRIANGLE) {
            app_ui_power_menu();
            continue;
        }
        if ((pressed & PAD_CROSS) && (start != 0 || size != 0)) {
            bootstrap_controller_disable(header_buffer, &boot_chain);
            continue;
        }
        if ((pressed & PAD_SQUARE) && start == 0 && size == 0) {
            bootstrap_controller_restore(header_buffer, &boot_chain);
            continue;
        }
        if ((pressed & PAD_CIRCLE) && start == 0 && size == 0) {
            bootstrap_controller_install(header_buffer, &boot_chain);
            continue;
        }
    }
}
