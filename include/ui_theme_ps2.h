#ifndef PS2_HDD_BOOTSTRAP_MANAGER_UI_THEME_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_UI_THEME_PS2_H

/* Stable application configuration name. Codenames such as Michishirube are
 * release-specific and must not become part of the long-lived user config ABI.
 * 0.4.x still accepts the old development filename as a read-only migration
 * source, but all writes and distributed examples use HDDMAN.CFG. */
#define UI_THEME_CONFIG_FILENAME "HDDMAN.CFG"
#define UI_THEME_LEGACY_CONFIG_FILENAME "MICHISHIRUBE.CFG"
#define UI_THEME_COUNT 4u

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} ui_rgb_t;

typedef enum {
    UI_THEME_AQUA = 0,
    UI_THEME_AMBER,
    UI_THEME_SAKURA,
    UI_THEME_MONO
} ui_theme_id_t;

typedef struct {
    const char *id;
    const char *name;
    ui_rgb_t background;
    ui_rgb_t panel;
    ui_rgb_t panel_alt;
    ui_rgb_t border;
    ui_rgb_t text;
    ui_rgb_t muted;
    ui_rgb_t accent;
    ui_rgb_t accent_soft;
    ui_rgb_t success;
    ui_rgb_t warning;
    ui_rgb_t danger;
    ui_rgb_t disabled_bg;
    ui_rgb_t disabled_border;
    ui_rgb_t disabled_text;
} ui_theme_palette_t;

const ui_theme_palette_t *ui_theme_current(void);
ui_theme_id_t ui_theme_current_id(void);
const char *ui_theme_name(ui_theme_id_t id);
const char *ui_theme_identifier(ui_theme_id_t id);
int ui_theme_set(ui_theme_id_t id);
int ui_theme_set_by_identifier(const char *identifier);

/* Load/save a tiny theme=... configuration beside the ELF when possible.
 * If argv[0] did not expose a usable directory, storage.c falls back to the
 * selected backup/report storage root. Missing config is not a fatal error.
 * Loading may migrate the old development-only MICHISHIRUBE.CFG name;
 * saving always targets HDDMAN.CFG. */
int ui_theme_load_config(void);
int ui_theme_save_config(void);
int ui_theme_config_path(char *destination, unsigned int capacity);

#endif
