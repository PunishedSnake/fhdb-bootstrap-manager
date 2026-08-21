#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "storage.h"
#include "ui_theme_ps2.h"

#define UI_THEME_CONFIG_BYTES 256u

static const ui_theme_palette_t palettes[UI_THEME_COUNT] = {
    {
        "aqua", "Aqua",
        {7, 10, 17}, {14, 20, 30}, {19, 28, 41}, {54, 78, 101},
        {233, 238, 245}, {145, 164, 187}, {49, 205, 235}, {26, 81, 111},
        {76, 196, 137}, {230, 176, 72}, {230, 92, 92},
        {31, 29, 28}, {163, 121, 56}, {153, 145, 132}
    },
    {
        "amber", "Amber",
        {13, 10, 6}, {28, 21, 12}, {38, 29, 16}, {96, 73, 35},
        {245, 238, 220}, {188, 168, 132}, {238, 169, 57}, {108, 66, 17},
        {106, 201, 128}, {245, 186, 62}, {232, 92, 76},
        {38, 28, 23}, {173, 96, 48}, {169, 145, 127}
    },
    {
        "sakura", "Sakura",
        {14, 8, 14}, {28, 17, 29}, {39, 23, 39}, {91, 56, 88},
        {246, 232, 242}, {186, 153, 179}, {232, 103, 177}, {108, 43, 83},
        {92, 199, 153}, {237, 177, 83}, {235, 88, 107},
        {41, 24, 34}, {163, 81, 104}, {172, 139, 158}
    },
    {
        "mono", "Monochrome",
        {8, 8, 8}, {20, 20, 20}, {29, 29, 29}, {76, 76, 76},
        {239, 239, 239}, {165, 165, 165}, {210, 210, 210}, {78, 78, 78},
        {180, 220, 185}, {226, 205, 133}, {230, 140, 140},
        {34, 34, 34}, {106, 91, 72}, {139, 139, 139}
    }
};

static ui_theme_id_t current_theme = UI_THEME_AQUA;

static int identifier_equal(const char *left, const char *right)
{
    unsigned char a;
    unsigned char b;

    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *right != '\0') {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (tolower(a) != tolower(b))
            return 0;
    }
    return *left == '\0' && *right == '\0';
}

const ui_theme_palette_t *ui_theme_current(void)
{
    return &palettes[(unsigned int)current_theme];
}

ui_theme_id_t ui_theme_current_id(void)
{
    return current_theme;
}

const char *ui_theme_name(ui_theme_id_t id)
{
    if ((unsigned int)id >= UI_THEME_COUNT)
        return "Unknown";
    return palettes[(unsigned int)id].name;
}

const char *ui_theme_identifier(ui_theme_id_t id)
{
    if ((unsigned int)id >= UI_THEME_COUNT)
        return "unknown";
    return palettes[(unsigned int)id].id;
}

int ui_theme_set(ui_theme_id_t id)
{
    if ((unsigned int)id >= UI_THEME_COUNT)
        return -1;
    current_theme = id;
    return 0;
}

int ui_theme_set_by_identifier(const char *identifier)
{
    unsigned int i;

    for (i = 0; i < UI_THEME_COUNT; i++) {
        if (identifier_equal(identifier, palettes[i].id)) {
            current_theme = (ui_theme_id_t)i;
            return 0;
        }
    }
    return -1;
}

int ui_theme_config_path(char *destination, unsigned int capacity)
{
    return storage_launch_file_path(destination, capacity,
                                    UI_THEME_CONFIG_FILENAME);
}

int ui_theme_load_config(void)
{
    char path[STORAGE_LAUNCH_PATH_SIZE];
    char buffer[UI_THEME_CONFIG_BYTES];
    char *line;
    int result;

    if (ui_theme_config_path(path, sizeof(path)) < 0)
        return -1;
    result = read_text_file(path, buffer, sizeof(buffer));
    if (result < 0)
        return result;

    line = strtok(buffer, "\r\n");
    while (line != NULL) {
        char *equals;

        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '#' || *line == ';' || *line == '\0') {
            line = strtok(NULL, "\r\n");
            continue;
        }
        equals = strchr(line, '=');
        if (equals != NULL) {
            char *key_end;
            char *value;
            char *value_end;

            *equals = '\0';
            key_end = equals - 1;
            while (key_end >= line && (*key_end == ' ' || *key_end == '\t'))
                *key_end-- = '\0';
            value = equals + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            value_end = value + strlen(value);
            while (value_end > value &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t'))
                *--value_end = '\0';

            if (identifier_equal(line, "theme"))
                return ui_theme_set_by_identifier(value);
        }
        line = strtok(NULL, "\r\n");
    }
    return -2;
}

int ui_theme_save_config(void)
{
    char path[STORAGE_LAUNCH_PATH_SIZE];
    char buffer[128];
    int length;

    if (ui_theme_config_path(path, sizeof(path)) < 0)
        return -1;
    length = snprintf(buffer, sizeof(buffer),
                      "# PS2 HDD Bootstrap Manager UI\n"
                      "theme=%s\n",
                      ui_theme_identifier(current_theme));
    if (length < 0 || (unsigned int)length >= sizeof(buffer))
        return -2;
    return write_whole_file(path, buffer, length);
}
