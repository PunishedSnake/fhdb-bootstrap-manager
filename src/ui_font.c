#include "ui_font.h"

#include <stddef.h>

typedef struct {
    const char *identifier;
    const char *name;
} ui_font_identity_t;

static const ui_font_identity_t identities[UI_FONT_COUNT] = {
    {"msx", "PS2SDK MSX 8x8"},
    {"spleen", "Spleen adaptive bitmap"}
};

static int ascii_equal_nocase(char left, char right)
{
    if (left >= 'A' && left <= 'Z')
        left = (char)(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z')
        right = (char)(right - 'A' + 'a');
    return left == right;
}

static int identifier_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *right != '\0') {
        if (!ascii_equal_nocase(*left++, *right++))
            return 0;
    }
    return *left == '\0' && *right == '\0';
}

const char *ui_font_name(ui_font_id_t font)
{
    if ((unsigned int)font >= UI_FONT_COUNT)
        return "Unknown font";
    return identities[(unsigned int)font].name;
}

const char *ui_font_identifier(ui_font_id_t font)
{
    if ((unsigned int)font >= UI_FONT_COUNT)
        return "unknown";
    return identities[(unsigned int)font].identifier;
}

int ui_font_from_identifier(const char *identifier, ui_font_id_t *font_out)
{
    unsigned int i;

    if (identifier == NULL || font_out == NULL)
        return -1;
    for (i = 0; i < UI_FONT_COUNT; i++) {
        if (identifier_equal(identifier, identities[i].identifier)) {
            *font_out = (ui_font_id_t)i;
            return 0;
        }
    }
    return -2;
}
