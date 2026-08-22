/* Hierarchical post-0.4 UI controller: categories replace button exhaustion. */

#include <debug.h>
#include <libpad.h>

#include <stdio.h>

#include "app_config.h"
#include "app_ui_ps2.h"
#include "bootstrap_controller_ps2.h"
#include "boot_report_session.h"
#include "diagnostics_controller_ps2.h"
#include "forensic_controller_ps2.h"
#include "gs_ui_ps2.h"
#include "manager_menu_ps2.h"
#include "platform.h"
#include "repair_controller_ps2.h"
#include "session_log.h"
#include "storage.h"
#include "ui_theme_ps2.h"
#include "video_mode.h"

static void build_dashboard_status(char *buffer, unsigned int capacity,
                                   const unsigned char header[APA_HEADER_SIZE],
                                   const boot_chain_info_t *boot_chain)
{
    unsigned int start = read_le32(header + APA_OSD_START_OFFSET);
    unsigned int size = read_le32(header + APA_OSD_SIZE_OFFSET);

    snprintf(buffer, capacity,
             "APA OK | bootstrap %s | %s | storage %s | report %s",
             (start != 0 || size != 0) ? "ON" : "OFF",
             boot_chain->family,
             storage_targets[storage_selected()].name,
             boot_report_session_last_save_result() == 0 ? "saved" : "pending");
}

static void bootstrap_menu(unsigned char header[APA_HEADER_SIZE],
                           boot_chain_info_t *boot_chain)
{
    unsigned int selected = 0;

    for (;;) {
        unsigned int start = read_le32(header + APA_OSD_START_OFFSET);
        unsigned int size = read_le32(header + APA_OSD_SIZE_OFFSET);
        int enabled = start != 0 || size != 0;
        app_ui_menu_item_t items[4] = {
            {"Create full rescue backup", "Read-only backup of current header/payload", 1},
            {"Disable active bootstrap",
             enabled ? "Back up, then clear osdStart/osdSize" :
                       "Unavailable: bootstrap pointer is already disabled",
             enabled},
            {"Restore rescue / legacy pointer",
             !enabled ? "Payload-first restore or guarded legacy pointer restore" :
                        "Unavailable: disable the current bootstrap first",
             !enabled},
            {"Sign and install MBR.XIN/XLF",
             !enabled ? "MagicGate sign, write/verify payload, enable pointer last" :
                        "Unavailable: disable the current bootstrap first",
             !enabled}
        };
        int choice = app_ui_menu_select("Bootstrap management", NULL,
                                        items, 4, &selected);
        if (choice < 0)
            return;
        switch (choice) {
            case 0:
                bootstrap_controller_backup_current(header, boot_chain);
                break;
            case 1:
                bootstrap_controller_disable(header, boot_chain);
                break;
            case 2:
                bootstrap_controller_restore(header, boot_chain);
                break;
            case 3:
                bootstrap_controller_install(header, boot_chain);
                break;
            default:
                break;
        }
    }
}

static void diagnostics_menu(unsigned char header[APA_HEADER_SIZE],
                             boot_chain_info_t *boot_chain)
{
    static const app_ui_menu_item_t items[] = {
        {"Boot-chain inspection", "Refresh evidence and save BOOTCHAIN.TXT/HDDMAN.LOG", 1},
        {"Refresh evidence silently", "Re-scan and persist without the summary screen", 1}
    };
    unsigned int selected = 0;

    for (;;) {
        int choice = app_ui_menu_select("Diagnostics", NULL, items, 2, &selected);
        if (choice < 0)
            return;
        if (choice == 0)
            diagnostics_controller_screen(header, boot_chain);
        if (choice == 1) {
            pad_activity_begin();
            diagnostics_controller_refresh(header, boot_chain, 1);
            pad_activity_end();
            scr_clear();
            scr_printf("Diagnostics refreshed and persisted.\n");
            app_ui_wait_to_return();
        }
    }
}

static void run_deterministic_repair(unsigned char header[APA_HEADER_SIZE],
                                     boot_chain_info_t *boot_chain)
{
    int result = repair_controller_health(header, boot_chain);

    if (result == REPAIR_CONTROLLER_REQUEST_DISABLE) {
        bootstrap_controller_disable(header, boot_chain);
        return;
    }
    if (result == REPAIR_CONTROLLER_RESTART_REQUIRED)
        app_ui_restart_to_browser();
    if (result == REPAIR_CONTROLLER_BLOCKED)
        app_ui_fatal_screen("HDD health analysis failed closed.", result);
}

static void recovery_menu(unsigned char header[APA_HEADER_SIZE],
                          boot_chain_info_t *boot_chain)
{
    static const app_ui_menu_item_t items[] = {
        {"Deterministic structure health / repair",
         "Canonical master/pointer checks using normal safety gates", 1},
        {"APA forensic / degraded read-only",
         "Raw scan, candidate partition maps, report and explicit repair plans", 1}
    };
    unsigned int selected = 0;

    for (;;) {
        int choice = app_ui_menu_select("Recovery", NULL, items, 2, &selected);
        if (choice < 0)
            return;
        if (choice == 0)
            run_deterministic_repair(header, boot_chain);
        if (choice == 1 && forensic_controller_screen())
            app_ui_restart_to_browser();
    }
}

static void backup_storage_menu(unsigned char header[APA_HEADER_SIZE],
                                boot_chain_info_t *boot_chain)
{
    static const app_ui_menu_item_t items[] = {
        {"Create full rescue backup", "Save header plus active payload when enabled", 1},
        {"Change backup/report storage", "Choose mc0, mc1 or mass", 1}
    };
    unsigned int selected = 0;

    for (;;) {
        int choice = app_ui_menu_select("Backup & storage", NULL,
                                        items, 2, &selected);
        if (choice < 0)
            return;
        if (choice == 0)
            bootstrap_controller_backup_current(header, boot_chain);
        if (choice == 1)
            app_ui_choose_storage();
    }
}

static void controller_status_screen(void)
{
    scr_clear();
    scr_printf("Controller activity indication\n\n");
    scr_printf("ANALOG lamp support: %s\n\n",
               pad_activity_is_supported() ? "available" : "not available");
    scr_printf("The DualShock ANALOG lamp is tied to main input mode.\n");
    scr_printf("Michishirube therefore uses steady ON only while an\n");
    scr_printf("operation is active and OFF while idle; it never blinks\n");
    scr_printf("the lamp rapidly or treats it as an independent LED.\n\n");
    scr_printf("All operations also show activity on screen.\n");
    app_ui_wait_to_return();
}

static void ui_theme_menu(void)
{
    static const char *const hints[UI_THEME_COUNT] = {
        "Cool cyan / blue default with high-contrast status colors",
        "Warm amber / brown service-console palette",
        "Pink-violet accent with the same recovery warning colors",
        "Neutral grayscale palette for maximum display compatibility"
    };
    app_ui_menu_item_t items[UI_THEME_COUNT];
    unsigned int selected = (unsigned int)ui_theme_current_id();
    unsigned int i;
    int choice;
    int save_result;
    char status[96];
    char config_path[STORAGE_LAUNCH_PATH_SIZE];

    for (i = 0; i < UI_THEME_COUNT; i++) {
        items[i].label = ui_theme_name((ui_theme_id_t)i);
        items[i].hint = hints[i];
        items[i].enabled = 1;
    }
    snprintf(status, sizeof(status), "Current theme: %s",
             ui_theme_name(ui_theme_current_id()));
    choice = app_ui_menu_select("UI theme", status,
                                items, UI_THEME_COUNT, &selected);
    if (choice < 0)
        return;

    ui_theme_set((ui_theme_id_t)choice);
    save_result = app_config_save();
    app_config_path(config_path, sizeof(config_path));
    session_log_line("UI theme changed to %s; config=%s; save=%d",
                     ui_theme_name(ui_theme_current_id()),
                     config_path, save_result);

    scr_clear();
    scr_printf("UI theme: %s\n\n", ui_theme_name(ui_theme_current_id()));
    scr_printf("Config: %s\n", config_path);
    if (save_result == 0)
        scr_printf("Preference saved beside the ELF.\n");
    else
        scr_printf("Theme is active for this session; config save code: %d\n",
                   save_result);
    scr_printf("\nNo restart is required.\n");
    app_ui_wait_to_return();
}

static void ui_font_menu(void)
{
    static const char *const hints[UI_FONT_COUNT] = {
        "Original PS2SDK 8x8 bitmap raster",
        "BSD-licensed Spleen 5x8 native / 8x16 progressive raster"
    };
    app_ui_menu_item_t items[UI_FONT_COUNT];
    ui_font_id_t previous_preference = app_config_font();
    unsigned int selected = (unsigned int)gs_ui_font_current();
    unsigned int i;
    int choice;
    int apply_result;
    int save_result = 0;
    char config_path[STORAGE_LAUNCH_PATH_SIZE];

    for (i = 0; i < UI_FONT_COUNT; i++) {
        items[i].label = ui_font_name((ui_font_id_t)i);
        items[i].hint = hints[i];
        items[i].enabled = 1;
    }
    choice = app_ui_menu_select("UI font", "Pixel-perfect bitmap fonts",
                                items, UI_FONT_COUNT, &selected);
    if (choice < 0)
        return;
    if ((ui_font_id_t)choice == gs_ui_font_current() &&
        (ui_font_id_t)choice == app_config_font())
        return;

    apply_result = gs_ui_font_apply((ui_font_id_t)choice);
    if (apply_result == 0) {
        (void)app_config_set_font((ui_font_id_t)choice);
        save_result = app_config_save();
        if (save_result < 0)
            (void)app_config_set_font(previous_preference);
    }
    app_config_path(config_path, sizeof(config_path));
    session_log_line("UI font request: %s; apply=%d; config=%s; save=%d",
                     ui_font_name((ui_font_id_t)choice), apply_result,
                     config_path, save_result);

    scr_clear();
    if (apply_result < 0) {
        scr_printf("Font change failed (code %d).\n", apply_result);
    } else {
        scr_printf("UI font: %s\n\n", ui_font_name((ui_font_id_t)choice));
        scr_printf("Config: %s\n", config_path);
        if (save_result == 0)
            scr_printf("Preference saved beside the ELF.\n");
        else
            scr_printf("Font is active for this session; save code: %d\n",
                       save_result);
        scr_printf("\nNo restart is required.\n");
    }
    app_ui_wait_to_return();
}

static const char *const video_mode_hints[VIDEO_MODE_COUNT] = {
    "Hardware-proven automatic 640x224 FIELD output",
    "Hardware-proven 720x448 progressive output in 32-bit color",
    "Guarded 720x576 progressive output; single 32-bit surface",
    "Guarded 1280x720 signal from a single 640x720x32 surface",
    "Guarded 1920x1080 FRAME output from two 640x540x32 buffers"
};

static int confirm_video_mode(video_mode_id_t mode, int startup)
{
    unsigned int remaining;
    char title[96];
    char body[320];

    snprintf(title, sizeof(title), "Confirm %s", video_mode_identifier(mode));
    for (remaining = 10u; remaining != 0u; remaining--) {
        u32 pressed = 0;

        snprintf(body, sizeof(body),
                 "%s is active.\n\n"
                 "Press X to keep this output%s.\n"
                 "Press TRIANGLE to restore native output.\n\n"
                 "Automatic restore in %u second%s.",
                 video_mode_name(mode),
                 startup ? " as the startup preference" : " and save it",
                 remaining, remaining == 1u ? "" : "s");
        gs_ui_render_message(title, body,
                             "No confirmation means no permanent black screen.",
                             GS_UI_TONE_WARNING);
        if (gs_ui_video_mode_current() != mode)
            return 0;
        if (wait_for_press_timeout(1000u, &pressed)) {
            if (pressed & PAD_CROSS)
                return 1;
            if (pressed & PAD_TRIANGLE)
                return 0;
        }
    }
    return 0;
}

static int save_video_preference(video_mode_id_t mode)
{
    video_mode_id_t previous = app_config_video_mode();
    int result;

    if (app_config_set_video_mode(mode) < 0)
        return -1;
    result = app_config_save();
    if (result < 0)
        (void)app_config_set_video_mode(previous);
    return result;
}

static void show_video_preference_result(video_mode_id_t mode, int save_result)
{
    char config_path[STORAGE_LAUNCH_PATH_SIZE];

    app_config_path(config_path, sizeof(config_path));
    scr_clear();
    scr_printf("Video mode: %s\n\n", video_mode_name(mode));
    scr_printf("Config: %s\n", config_path);
    if (save_result == 0)
        scr_printf("Preference saved beside the ELF.\n");
    else
        scr_printf("Mode is active for this session; config save code: %d\n",
                   save_result);
    scr_printf("\nEvery non-native startup still requires confirmation.\n");
    app_ui_wait_to_return();
}

static void video_mode_menu(void)
{
    app_ui_menu_item_t items[VIDEO_MODE_COUNT];
    unsigned int selected = (unsigned int)gs_ui_video_mode_current();
    unsigned int i;
    char status[128];
    int choice;
    int result;
    int save_result;

    for (i = 0; i < VIDEO_MODE_COUNT; i++) {
        items[i].label = video_mode_name((video_mode_id_t)i);
        items[i].hint = video_mode_hints[i];
        items[i].enabled = gs_ui_video_mode_supported((video_mode_id_t)i);
    }
    snprintf(status, sizeof(status), "Current: %s | startup: %s",
             video_mode_identifier(gs_ui_video_mode_current()),
             video_mode_identifier(app_config_video_mode()));
    choice = app_ui_menu_select("Video mode", status, items,
                                VIDEO_MODE_COUNT, &selected);
    if (choice < 0)
        return;
    if ((video_mode_id_t)choice == gs_ui_video_mode_current() &&
        (video_mode_id_t)choice == app_config_video_mode())
        return;

    result = gs_ui_video_mode_apply((video_mode_id_t)choice);
    session_log_line("Video mode request: %s; result=%d",
                     video_mode_name((video_mode_id_t)choice), result);
    if (result < 0) {
        scr_clear();
        scr_printf("Video mode switch failed (code %d).\n\n", result);
        scr_printf("The native display mode has been restored.\n");
        app_ui_wait_to_return();
        return;
    }

    if (choice == VIDEO_MODE_NATIVE) {
        save_result = save_video_preference(VIDEO_MODE_NATIVE);
        session_log_line("Native video preference save=%d", save_result);
        show_video_preference_result(VIDEO_MODE_NATIVE, save_result);
        return;
    }

    if (confirm_video_mode((video_mode_id_t)choice, 0)) {
        save_result = save_video_preference((video_mode_id_t)choice);
        session_log_line("Video mode confirmed: %s; config save=%d",
                         video_mode_name((video_mode_id_t)choice), save_result);
        show_video_preference_result((video_mode_id_t)choice, save_result);
        return;
    }

    (void)gs_ui_video_mode_apply(VIDEO_MODE_NATIVE);
    session_log_line("Video mode %s was not confirmed; native restored",
                     video_mode_name((video_mode_id_t)choice));
}

static void apply_startup_video_preference(void)
{
    video_mode_id_t mode = app_config_video_mode();
    int result;
    int save_result;

    if (mode == VIDEO_MODE_NATIVE)
        return;
    if (!gs_ui_video_mode_supported(mode)) {
        save_result = save_video_preference(VIDEO_MODE_NATIVE);
        session_log_line("Startup video mode %s unsupported; native saved=%d",
                         video_mode_name(mode), save_result);
        return;
    }

    result = gs_ui_video_mode_apply(mode);
    session_log_line("Startup video mode request: %s; result=%d",
                     video_mode_name(mode), result);
    if (result == 0 && confirm_video_mode(mode, 1)) {
        session_log_line("Startup video mode confirmed: %s",
                         video_mode_name(mode));
        return;
    }

    (void)gs_ui_video_mode_apply(VIDEO_MODE_NATIVE);
    save_result = save_video_preference(VIDEO_MODE_NATIVE);
    session_log_line("Startup video mode rejected; native restored and saved=%d",
                     save_result);
}

static void system_menu(void)
{
    static const app_ui_menu_item_t items[] = {
        {"Controller / activity indicator", "Inspect ANALOG-lamp capability and behavior", 1},
        {"UI theme", "Choose Aqua, Amber, Sakura or Monochrome", 1},
        {"UI font", "Choose the original MSX or adaptive Spleen bitmap", 1},
        {"Video mode", "Native, 480p and guarded full-surface HDTV modes", 1},
        {"Power / restart", "Restart to Browser or shut down", 1}
    };
    unsigned int selected = 0;

    for (;;) {
        int choice = app_ui_menu_select("System", NULL, items, 5, &selected);
        if (choice < 0)
            return;
        if (choice == 0)
            controller_status_screen();
        if (choice == 1)
            ui_theme_menu();
        if (choice == 2)
            ui_font_menu();
        if (choice == 3)
            video_mode_menu();
        if (choice == 4)
            app_ui_power_menu();
    }
}

void manager_menu_run(unsigned char header[APA_HEADER_SIZE],
                      boot_chain_info_t *boot_chain)
{
    static const app_ui_menu_item_t root_items[] = {
        {"Bootstrap", "Install, disable, restore and bootstrap backup", 1},
        {"Diagnostics", "Boot-chain evidence and reports", 1},
        {"Recovery", "Deterministic repair and forensic APA workspace", 1},
        {"Backup & Storage", "Rescue backup destination and storage selection", 1},
        {"System", "Controller, theme, font, video mode, restart and power", 1}
    };
    unsigned int selected = 0;
    char status[192];

    apply_startup_video_preference();

    for (;;) {
        int choice;

        build_dashboard_status(status, sizeof(status), header, boot_chain);
        choice = app_ui_menu_select("Manager dashboard", status,
                                    root_items, 5, &selected);
        if (choice < 0) {
            app_ui_power_menu();
            continue;
        }
        switch (choice) {
            case 0:
                bootstrap_menu(header, boot_chain);
                break;
            case 1:
                diagnostics_menu(header, boot_chain);
                break;
            case 2:
                recovery_menu(header, boot_chain);
                break;
            case 3:
                backup_storage_menu(header, boot_chain);
                break;
            case 4:
                system_menu();
                break;
            default:
                break;
        }
    }
}
