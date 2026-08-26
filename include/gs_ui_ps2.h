#ifndef PS2_HDD_BOOTSTRAP_MANAGER_GS_UI_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_GS_UI_PS2_H

#include <stdarg.h>

#include "video_mode.h"
#include "ui_font.h"

typedef enum {
    GS_UI_TONE_INFO = 0,
    GS_UI_TONE_SUCCESS,
    GS_UI_TONE_WARNING,
    GS_UI_TONE_DANGER
} gs_ui_tone_t;

/*
 * Michishirube's application-wide GS frontend.
 *
 * Physical hardware uses the proven libdebug CRT bootstrap for native output.
 * Callers always author the UI in logical 640x224 coordinates; the renderer
 * maps them into an explicit output viewport without confusing that viewport
 * with the complete signal or framebuffer geometry.
 */
int gs_ui_initialize(void);
int gs_ui_is_ready(void);

/* Video-mode changes affect this application only. Native mode replays the
 * complete hardware-proven init_scr() GS bootstrap without repeating its
 * global DMA reset. Every alternate output is guarded by bounded
 * synchronization and timed confirmation in the manager. */
video_mode_id_t gs_ui_video_mode_current(void);
int gs_ui_video_mode_supported(video_mode_id_t mode);
int gs_ui_video_mode_apply(video_mode_id_t mode);

ui_font_id_t gs_ui_font_current(void);
int gs_ui_font_apply(ui_font_id_t font);

#if defined(__GNUC__)
#define GS_UI_MENU_SIZE_OPT __attribute__((optimize("Os")))
#else
#define GS_UI_MENU_SIZE_OPT
#endif

/* Human-speed menu presentation is VBlank/controller paced. Keep these two
 * ordinary navigation renderers compact; disk-status telemetry and lower-level
 * GS primitives retain their independent optimization policy. */
void GS_UI_MENU_SIZE_OPT gs_ui_render_menu(
    const char *title,
    const char *status,
    const char *const *labels,
    const char *const *hints,
    const unsigned char *enabled,
    unsigned int item_count,
    unsigned int selected);

/* Root-only card dashboard. It keeps the short section descriptions without
 * forcing six two-line entries into the ordinary vertical-menu geometry. */
void GS_UI_MENU_SIZE_OPT gs_ui_render_dashboard(
    const char *title,
    const char *status,
    const char *const *labels,
    const char *const *hints,
    const unsigned char *enabled,
    unsigned int item_count,
    unsigned int selected);

#undef GS_UI_MENU_SIZE_OPT

void gs_ui_render_message(const char *title,
                          const char *body,
                          const char *footer,
                          gs_ui_tone_t tone);

/* Compatibility surface for controller screens that still construct text
 * incrementally. Linker wrappers route historical scr_* calls here, so the
 * real libdebug renderer is used only as an initialization-failure fallback. */
void gs_ui_console_clear(void);
/* Drop an already-presented compatibility screen without scheduling a blank
 * replacement frame. This prevents prompts from surviving behind GS menus. */
void gs_ui_console_discard(void);
void gs_ui_console_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
void gs_ui_console_vprintf(const char *format, va_list arguments);
/* Present one complete compatibility-console screen. Historical controller
 * code often emits a screen through several scr_printf() calls; batching those
 * calls avoids rebuilding and submitting the same frame after every line. */
void gs_ui_console_present(void);

void gs_ui_render_disk_status(const char *operation,
                              const char *phase,
                              const char *location,
                              const char *io_kind,
                              unsigned int percent,
                              unsigned int progress_current,
                              unsigned int progress_total,
                              unsigned int lba,
                              unsigned int sectors,
                              int write_sensitive);

#endif
