#ifndef PS2_HDD_BOOTSTRAP_MANAGER_UI_FONT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_UI_FONT_H

#define UI_FONT_COUNT 2u

typedef enum {
    UI_FONT_MSX = 0,
    UI_FONT_SPLEEN
} ui_font_id_t;

const char *ui_font_name(ui_font_id_t font);
const char *ui_font_identifier(ui_font_id_t font);
int ui_font_from_identifier(const char *identifier, ui_font_id_t *font_out);

#endif
