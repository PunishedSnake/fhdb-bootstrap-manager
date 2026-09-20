/* Shared PS2-only presentation and lifecycle helpers for application controllers. */

#include <tamtypes.h>
#include <kernel.h>
#include <debug.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>

#include "app_error.h"
#include "app_identity.h"
#include "app_ui_ps2.h"
#include "boot_report_session.h"
#include "gs_ui_ps2.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

#define APP_UI_MAX_MENU_ITEMS 12u
#define APP_UI_DASHBOARD_COLUMNS 3u
#define APP_UI_DASHBOARD_MAX_ITEMS 6u

/* Menu selectors spend almost all of their lifetime waiting for a controller
 * edge. LTO inlining them into every controller duplicates sizeable render and
 * navigation loops without reducing any meaningful latency. One canonical
 * implementation is better for the R5900's 16 KiB I-cache. */
#if defined(__GNUC__)
#define UI_NOINLINE __attribute__((noinline))
#else
#define UI_NOINLINE
#endif

static void print_error_details(app_error_domain_t fallback_domain,
                                int code, int consume)
{
    app_error_record_t record;
    app_error_info_t info;
    app_error_domain_t domain = fallback_domain;
    const char *context = NULL;

    if (app_error_get(&record) && record.code == code) {
        domain = record.domain;
        context = record.context[0] != '\0' ? record.context : NULL;
    }
    app_error_describe(domain, code, &info);

    scr_printf("\nError ID : %s\n", info.symbol);
    if (context != NULL)
        scr_printf("Stage    : %s\n", context);
    scr_printf("Summary  : %s\n", info.summary);
    scr_printf("Reason   : %s\n", info.detail);
    scr_printf("Next step: %s\n", info.action);
    scr_printf("Raw code : %d\n", code);

    session_log_line("Error detail: id=%s code=%d context=%s summary=%s",
                     info.symbol, code,
                     context != NULL ? context : "(none)", info.summary);
    if (consume)
        app_error_clear();
}

UI_NOINLINE void app_ui_restart_to_browser(void)
{
    static char *browser_args[] = {"BootBrowser", NULL};

    session_log_line("Restart to PS2 Browser requested");
    session_log_flush(storage_selected());
    pad_activity_restore();
    gs_ui_render_message("Restarting",
                         "Returning control to the PS2 Browser.",
                         "DEV9 will be shut down before ExecOSD.",
                         GS_UI_TONE_INFO);
    fileXioDevctl("hdd0:", HDIOC_DEV9OFF, NULL, 0, NULL, 0);
    ExecOSD(1, browser_args);
}

static UI_NOINLINE void app_ui_shutdown_console(void)
{
    session_log_line("Controlled power-off requested");
    session_log_flush(storage_selected());
    pad_activity_restore();
    gs_ui_render_message("Power off",
                         "Shutting down the console through the poweroff RPC.",
                         NULL, GS_UI_TONE_WARNING);
    poweroffShutdown();
    SleepThread();
}

UI_NOINLINE void app_ui_power_menu(void)
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

UI_NOINLINE void app_ui_fatal_screen(const char *message, int code)
{
    session_log_line("FATAL: %s (code %d)", message, code);
    session_log_flush(storage_selected());
    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("ERROR: %s\n", message);
        print_error_details(APP_ERROR_DOMAIN_STARTUP, code, 0);
        scr_printf("\nX = restart   TRIANGLE = power off\n");
        pressed = wait_for_press();
        if (pressed & PAD_CROSS)
            app_ui_restart_to_browser();
        if (pressed & PAD_TRIANGLE)
            app_ui_shutdown_console();
    }
}

UI_NOINLINE void app_ui_wait_to_return(void)
{
    app_error_record_t record;

    if (app_error_get(&record))
        print_error_details(record.domain, record.code, 1);
    scr_printf("\nPress X to return.\n");
    while (!(wait_for_press() & PAD_CROSS)) {}
    gs_ui_console_discard();
}

static unsigned int dashboard_move_horizontal(unsigned int current,
                                               unsigned int item_count,
                                               int direction)
{
    unsigned int row_start =
        (current / APP_UI_DASHBOARD_COLUMNS) * APP_UI_DASHBOARD_COLUMNS;
    unsigned int row_count = item_count - row_start;
    unsigned int column = current - row_start;

    if (row_count > APP_UI_DASHBOARD_COLUMNS)
        row_count = APP_UI_DASHBOARD_COLUMNS;
    if (direction < 0)
        column = (column + row_count - 1u) % row_count;
    else
        column = (column + 1u) % row_count;
    return row_start + column;
}

static unsigned int dashboard_move_vertical(unsigned int current,
                                             unsigned int item_count,
                                             int direction)
{
    unsigned int row_count =
        (item_count + APP_UI_DASHBOARD_COLUMNS - 1u) /
        APP_UI_DASHBOARD_COLUMNS;
    unsigned int row = current / APP_UI_DASHBOARD_COLUMNS;
    unsigned int column = current % APP_UI_DASHBOARD_COLUMNS;
    unsigned int target;

    if (direction < 0)
        row = (row + row_count - 1u) % row_count;
    else
        row = (row + 1u) % row_count;
    target = row * APP_UI_DASHBOARD_COLUMNS + column;
    if (target >= item_count)
        target = item_count - 1u;
    return target;
}

UI_NOINLINE int app_ui_dashboard_select(const char *title, const char *status,
                                        const app_ui_menu_item_t *items,
                                        unsigned int item_count,
                                        unsigned int *selection)
{
    const char *labels[APP_UI_MAX_MENU_ITEMS];
    const char *hints[APP_UI_MAX_MENU_ITEMS];
    unsigned char enabled[APP_UI_MAX_MENU_ITEMS];
    unsigned int current;
    unsigned int i;

    if (items == NULL || item_count == 0 || selection == NULL ||
        item_count > APP_UI_DASHBOARD_MAX_ITEMS)
        return -1;
    current = *selection < item_count ? *selection : 0;

    for (i = 0; i < item_count; i++) {
        labels[i] = items[i].label;
        hints[i] = items[i].hint;
        enabled[i] = items[i].enabled ? 1u : 0u;
    }

    for (;;) {
        u32 pressed;

        gs_ui_render_dashboard(title, status, labels, hints, enabled,
                               item_count, current);
        pressed = wait_for_press();
        if (pressed & PAD_LEFT)
            current = dashboard_move_horizontal(current, item_count, -1);
        if (pressed & PAD_RIGHT)
            current = dashboard_move_horizontal(current, item_count, 1);
        if (pressed & PAD_UP)
            current = dashboard_move_vertical(current, item_count, -1);
        if (pressed & PAD_DOWN)
            current = dashboard_move_vertical(current, item_count, 1);
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

UI_NOINLINE int app_ui_menu_select(const char *title, const char *status,
                                   const app_ui_menu_item_t *items,
                                   unsigned int item_count,
                                   unsigned int *selection)
{
    const char *labels[APP_UI_MAX_MENU_ITEMS];
    const char *hints[APP_UI_MAX_MENU_ITEMS];
    unsigned char enabled[APP_UI_MAX_MENU_ITEMS];
    unsigned int current;
    unsigned int i;

    if (items == NULL || item_count == 0 || selection == NULL ||
        item_count > APP_UI_MAX_MENU_ITEMS)
        return -1;
    current = *selection < item_count ? *selection : 0;

    for (i = 0; i < item_count; i++) {
        labels[i] = items[i].label;
        hints[i] = items[i].hint;
        enabled[i] = items[i].enabled ? 1u : 0u;
    }

    for (;;) {
        u32 pressed;

        gs_ui_render_menu(title, status, labels, hints, enabled,
                          item_count, current);
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
    gs_ui_render_message(title != NULL ? title : "Working",
                         message != NULL ? message : "Operation in progress.",
                         "Activity is also reflected by the ANALOG lamp when supported.",
                         GS_UI_TONE_INFO);
}

UI_NOINLINE void app_ui_choose_storage(void)
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

UI_NOINLINE int app_ui_choose_signing_card(void)
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
