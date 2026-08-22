#include "video_mode.h"

#include <stddef.h>

typedef struct {
    const char *identifier;
    const char *name;
} video_mode_identity_t;

static const video_mode_identity_t identities[VIDEO_MODE_COUNT] = {
    {"native", "Native automatic (640x224 field)"},
    {"ntsc-480i", "NTSC 480i frame (640x448)"},
    {"pal-576i", "PAL 576i frame (640x512)"},
    {"480p", "480p progressive (720x448)"},
    {"576p", "576p progressive (656x512)"},
    {"720p", "720p progressive (1280x448 UI)"},
    {"1080i", "1080i interlaced (960x448 UI)"}
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

const char *video_mode_name(video_mode_id_t mode)
{
    if ((unsigned int)mode >= VIDEO_MODE_COUNT)
        return "Unknown video mode";
    return identities[(unsigned int)mode].name;
}

const char *video_mode_identifier(video_mode_id_t mode)
{
    if ((unsigned int)mode >= VIDEO_MODE_COUNT)
        return "unknown";
    return identities[(unsigned int)mode].identifier;
}

int video_mode_from_identifier(const char *identifier,
                               video_mode_id_t *mode_out)
{
    unsigned int i;

    if (identifier == NULL || mode_out == NULL)
        return -1;
    for (i = 0; i < VIDEO_MODE_COUNT; i++) {
        if (identifier_equal(identifier, identities[i].identifier)) {
            *mode_out = (video_mode_id_t)i;
            return 0;
        }
    }
    return -2;
}
