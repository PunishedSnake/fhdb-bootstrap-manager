#ifndef PS2_HDD_BOOTSTRAP_MANAGER_GS_UI_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_GS_UI_PS2_H

#include <stdarg.h>

typedef enum {
    GS_UI_TONE_INFO = 0,
    GS_UI_TONE_SUCCESS,
    GS_UI_TONE_WARNING,
    GS_UI_TONE_DANGER
} gs_ui_tone_t;

/*
 * Michishirube's application-wide GS frontend.
 *
 * This renderer owns CRT/framebuffer setup instead of sharing libdebug's
 * drawing environment. All coordinates are ordinary full-screen 640x448 UI
 * coordinates. PS2SDK libdraw adds the GS 2048 coordinate bias internally;
 * callers must never compensate for it themselves.
 */
int gs_ui_initialize(void);
int gs_ui_is_ready(void);

void gs_ui_render_menu(const char *title,
                       const char *status,
                       const char *const *labels,
                       const char *const *hints,
                       const unsigned char *enabled,
                       unsigned int item_count,
                       unsigned int selected);

void gs_ui_render_message(const char *title,
                          const char *body,
                          const char *footer,
                          gs_ui_tone_t tone);

/* Compatibility surface for existing controller screens. It keeps their
 * printf-style construction semantics but renders the resulting screen with
 * the same GS/GIF-DMA frontend. libdebug no longer draws application UI. */
void gs_ui_console_clear(void);
void gs_ui_console_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
void gs_ui_console_vprintf(const char *format, va_list arguments);

void gs_ui_render_disk_status(const char *operation,
                              const char *phase,
                              const char *io_kind,
                              unsigned int percent,
                              unsigned int progress_current,
                              unsigned int progress_total,
                              unsigned int lba,
                              unsigned int sectors,
                              int write_sensitive);

#endif
