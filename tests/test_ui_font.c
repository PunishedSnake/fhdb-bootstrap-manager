#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_font.h"
#include "spleen_font_data.h"

int main(void)
{
    ui_font_id_t font = UI_FONT_MSX;

    assert(UI_FONT_COUNT == 2u);
    assert(strcmp(ui_font_identifier(UI_FONT_MSX), "msx") == 0);
    assert(strcmp(ui_font_identifier(UI_FONT_SPLEEN), "spleen") == 0);
    assert(ui_font_from_identifier("MSX", &font) == 0);
    assert(font == UI_FONT_MSX);
    assert(ui_font_from_identifier("Spleen", &font) == 0);
    assert(font == UI_FONT_SPLEEN);
    assert(ui_font_from_identifier("unknown", &font) < 0);
    assert(ui_font_from_identifier(NULL, &font) < 0);
    assert(ui_font_from_identifier("msx", NULL) < 0);
    assert(spleen_5x8_ascii[('A' - SPLEEN_ASCII_FIRST) * 8u + 1u] == 0x60u);
    assert(spleen_8x16_ascii[('A' - SPLEEN_ASCII_FIRST) * 16u + 2u] != 0u);
    puts("All UI font identifier tests passed.");
    return 0;
}
