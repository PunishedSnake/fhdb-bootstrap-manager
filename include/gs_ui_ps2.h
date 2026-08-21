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
 * Physical hardware validation showed that the proven PS2SDK/libdebug CRT
 * bootstrap should remain responsible for establishing the interlaced output
 * and framebuffer read circuit. gs_ui_ps2 then owns every application pixel in
 * that framebuffer. The UI uses a virtual 640x448 layout mapped vertically to
 * the bootstrap's 640x224 field coordinate space. PS2SDK libdraw already adds
 * the GS +2048 primitive bias; callers must never compensate for it themselves.
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

/* Compatibility surface for existing controller screens. Their printf-style
 * construction is linker-wrapped and routed through this GS/GIF-DMA frontend;
 * libdebug itself does not draw normal application UI after video bootstrap. */
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
