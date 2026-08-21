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
    /* The ANALOG lamp is tied to controller main mode, not an independent LED.
       Restore the user's initial mode before leaving this application's pad
       ownership rather than leaking our activity-indicator mode into OSD. */
    pad_activity_restore();
    scr_clear();
    scr_printf("Restarting to the PS2 Browser...\n");
    fileXioDevctl("hdd0:", HDIOC_DEV9OFF, NULL, 0, NULL, 0);
    ExecOSD(1, browser_args);
}

static void app_ui_shutdown_console(void)
{
    session_log_line("Controlled power-off requested");
    session_log_flush(storage_selected());
    pad_activity_restore();
    poweroffShutdown();
    SleepThread();
}

void app_ui_power_menu(void)
{
    static const app_ui_menu_item_t items[] = {
        {"Restart to PS2 Browser", "Return through ExecOSD", 1},
        {"Power off console", "Controlled poweroff RPC shutdown", 1}
    };
    unsigned int selected = 0;

    for (;;) {
        int choice = app_ui_menu_select("System / power", APP_NAME,
                                        items, 2, &selected);
        if (choice < 0)
            return;
        if (choice == 0)
            app_ui_restart_to_browser();
        if (choice == 1)
            app_ui_shutdown_console();
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
    scr_printf("\nPress X to return.\n");
    while (!(wait_for_press() & PAD_CROSS)) {}
}

int app_ui_menu_select(const char *title, const char *status,
                       const app_ui_menu_item_t *items,
                       unsigned int item_count,
                       unsigned int *selection)
{
    unsigned int current;

    if (items == NULL || item_count == 0 || selection == NULL)
        return -1;
    current = *selection < item_count ? *selection : 0;

    for (;;) {
        u32 pressed;
        unsigned int i;

        scr_clear();
        scr_printf(APP_NAME " v%s\n", APP_VERSION);
        scr_printf("%s\n", title != NULL ? title : "Menu");
        if (status != NULL && status[0] != '\0')
            scr_printf("%s\n", status);
        scr_printf("\n");
        for (i = 0; i < item_count; i++) {
            scr_printf("%s %s%s\n", i == current ? ">" : " ",
                       items[i].enabled ? "" : "[--] ", items[i].label);
            if (i == current && items[i].hint != NULL)
                scr_printf("    %s\n", items[i].hint);
        }
        scr_printf("\nUP/DOWN Select   X Open   TRIANGLE Back\n");
        pressed = wait_for_press();
        if (pressed & PAD_UP)
            current = (current + item_count - 1u) % item_count;
        if (pressed & PAD_DOWN)
            current = (current + 1u) % item_count;
        if ((pressed & PAD_CROSS) && items[current].enabled) {
            *selection = current;
            return (int)current;
        }
        if (pressed & PAD_TRIANGLE) {
            *selection = current;
            return -1;
        }
    }
}

void app_ui_activity_message(const char *title, const char *message)
{
    scr_clear();
    scr_printf("%s\n\n", title != NULL ? title : "Working");
    if (message != NULL)
        scr_printf("%s\n", message);
    scr_printf("\nActivity: screen + ANALOG lamp when supported.\n");
}

void app_ui_choose_storage(void)
{
    unsigned int choice = storage_selected();

    for (;;) {
        app_ui_menu_item_t items[STORAGE_TARGET_COUNT];
        unsigned int i;
        int selected;

        for (i = 0; i < STORAGE_TARGET_COUNT; i++) {
            items[i].label = storage_targets[i].name;
            items[i].hint = storage_targets[i].memory_card_port >= 0
                                ? "Memory-card storage"
                                : "USB mass storage";
            items[i].enabled = 1;
        }
        selected = app_ui_menu_select("Backup / report storage",
                                      "Choose destination", items,
                                      STORAGE_TARGET_COUNT, &choice);
        if (selected < 0)
            return;
        storage_set_selected((unsigned int)selected);
        session_log_line("Selected storage device: %s",
                         storage_targets[storage_selected()].name);
        pad_activity_begin();
        boot_report_session_save(storage_selected());
        session_log_flush(storage_selected());
        pad_activity_end();
        return;
    }
}

int app_ui_choose_signing_card(void)
{
    static const app_ui_menu_item_t items[] = {
        {"mc0", "Use PS2 memory card in slot 1", 1},
        {"mc1", "Use PS2 memory card in slot 2", 1}
    };
    unsigned int choice = 0;

    return app_ui_menu_select("MagicGate signing card",
                              "A compatible PS2 memory card is required",
                              items, 2, &choice);
}