#ifndef PS2_HDD_BOOTSTRAP_MANAGER_GS_UI_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_GS_UI_PS2_H

/*
 * Fast GS-backed 2D overlay used by high-frequency status presentation.
 * It intentionally reuses the framebuffer/mode established by libdebug's
 * init_scr() instead of changing CRT mode. Static font glyphs are uploaded to
 * VRAM once; each frame is one GIF DMA packet containing ordinary 2D sprites.
 */
int gs_ui_initialize(void);
int gs_ui_is_ready(void);

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
