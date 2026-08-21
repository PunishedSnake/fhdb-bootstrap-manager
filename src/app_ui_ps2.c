/* Shared PS2-only presentation and lifecycle helpers for application controllers. */

#include <tamtypes.h>
#include <kernel.h>
#include <debug.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>

#include "app_identity.h"
#include "app_ui_ps2.h"
#include "boot_report_session.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

void app_ui_restart_to_browser(void)
{
    static char *browser_args[] = {"BootBrowser", NULL};

    session_log_line("Restart to PS2 Browser requested");
    session_log_flush(storage_selected());
    scr_clear();
    scr_printf("Restarting to the PS2 Browser...\n");
    fileXioDevctl("hdd0:", HDIOC_DEV9OFF, NULL, 0, NULL, 0);
    ExecOSD(1, browser_args);
}

static void app_ui_shutdown_console(void)
{
    session_log_line("Controlled power-off requested");
    session_log_flush(storage_selected());
    poweroffShutdown();
    SleepThread();
}

void app_ui_power_menu(void)
{
    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("X        Restart to PS2 Browser\n");
        scr_printf("TRIANGLE Power off\n");
        scr_printf("CIRCLE   Return to manager\n");
        pressed = wait_for_press();
        if (pressed & PAD_CROSS)
            app_ui_restart_to_browser();
        if (pressed & PAD_TRIANGLE)
            app_ui_shutdown_console();
        if (pressed & PAD_CIRCLE)
            return;
    }
}

void app_ui_fatal_screen(const char *message, int code)
{
    session_log_line("FATAL: %s (code %d)", message, code);
    session_log_flush(storage_selected());
    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("ERROR: %s\n", message);
        scr_printf("Code: %d\n\n", code);
        scr_printf("X = restart   TRIANGLE = power off\n");
        pressed = wait_for_press();
        if (pressed & PAD_CROSS)
            app_ui_restart_to_browser();
        if (pressed & PAD_TRIANGLE)
            app_ui_shutdown_console();
    }
}

void app_ui_wait_to_return(void)
{
    scr_printf("\nPress X to return to the manager.\n");
    while (!(wait_for_press() & PAD_CROSS)) {}
}

void app_ui_choose_storage(void)
{
    unsigned int choice = storage_selected();

    for (;;) {
        u32 pressed;
        unsigned int i;

        scr_clear();
        scr_printf("Select storage device\n\n");
        for (i = 0; i < STORAGE_TARGET_COUNT; i++)
            scr_printf("%s %s\n", i == choice ? ">" : " ",
                       storage_targets[i].name);
        scr_printf("\nUP/DOWN Select   X Confirm\n");
        scr_printf("TRIANGLE Cancel\n");
        pressed = wait_for_press();
        if (pressed & PAD_UP)
            choice = (choice + STORAGE_TARGET_COUNT - 1) % STORAGE_TARGET_COUNT;
        if (pressed & PAD_DOWN)
            choice = (choice + 1) % STORAGE_TARGET_COUNT;
        if (pressed & PAD_CROSS) {
            storage_set_selected(choice);
            session_log_line("Selected storage device: %s",
                             storage_targets[storage_selected()].name);
            boot_report_session_save(storage_selected());
            session_log_flush(storage_selected());
            return;
        }
        if (pressed & PAD_TRIANGLE)
            return;
    }
}

int app_ui_choose_signing_card(void)
{
    unsigned int choice = 0;

    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf("Select MagicGate signing card\n\n");
        scr_printf("%s mc0\n", choice == 0 ? ">" : " ");
        scr_printf("%s mc1\n", choice == 1 ? ">" : " ");
        scr_printf("\nA PS2 memory card must be present.\n");
        scr_printf("UP/DOWN Select   X Confirm\n");
        scr_printf("TRIANGLE Cancel\n");
        pressed = wait_for_press();
        if (pressed & (PAD_UP | PAD_DOWN))
            choice ^= 1;
        if (pressed & PAD_CROSS)
            return (int)choice;
        if (pressed & PAD_TRIANGLE)
            return -1;
    }
}
