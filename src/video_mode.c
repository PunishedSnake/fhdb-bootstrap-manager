#include "video_mode.h"

#include <stddef.h>

typedef struct {
    const char *identifier;
    const char *name;
} video_mode_identity_t;

static const video_mode_identity_t identities[VIDEO_MODE_COUNT] = {
    {"native", "Native automatic (640x224 field)"},
    {"480p", "480p progressive (720x448)"}
};

static const char *const unsafe_041_identifiers[] = {
    "ntsc-480i", "pal-576i", "576p", "720p", "1080i"
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
    for (i = 0; i < sizeof(unsafe_041_identifiers) /
                         sizeof(unsafe_041_identifiers[0]); i++) {
        if (identifier_equal(identifier, unsafe_041_identifiers[i])) {
            *mode_out = VIDEO_MODE_NATIVE;
            return VIDEO_MODE_MIGRATED;
        }
    }
    return -2;
}
