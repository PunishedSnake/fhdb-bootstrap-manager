#include <stdio.h>
#include <string.h>

#include "video_mode.h"

int main(void)
{
    unsigned int i;

    for (i = 0; i < VIDEO_MODE_COUNT; i++) {
        video_mode_id_t parsed = VIDEO_MODE_NATIVE;
        const char *identifier = video_mode_identifier((video_mode_id_t)i);

        if (identifier == NULL || identifier[0] == '\0' ||
            strcmp(identifier, "unknown") == 0 ||
            video_mode_name((video_mode_id_t)i)[0] == '\0' ||
            video_mode_from_identifier(identifier, &parsed) < 0 ||
            parsed != (video_mode_id_t)i) {
            fprintf(stderr, "Video-mode round trip failed at %u.\n", i);
            return 1;
        }
    }

    {
        static const char *const legacy_modes[] = {
            "ntsc-480i", "PAL-576I", "576p", "720P", "1080i"
        };
        video_mode_id_t parsed = VIDEO_MODE_NATIVE;
        unsigned int legacy;

        for (legacy = 0;
             legacy < sizeof(legacy_modes) / sizeof(legacy_modes[0]);
             legacy++) {
            parsed = VIDEO_MODE_480P;
            if (video_mode_from_identifier(legacy_modes[legacy], &parsed) !=
                    VIDEO_MODE_MIGRATED ||
                parsed != VIDEO_MODE_NATIVE) {
                fprintf(stderr, "Legacy mode was not migrated: %s.\n",
                        legacy_modes[legacy]);
                return 1;
            }
        }
        if (video_mode_from_identifier("not-a-mode", &parsed) >= 0 ||
            video_mode_from_identifier(NULL, &parsed) >= 0 ||
            video_mode_from_identifier("native", NULL) >= 0) {
            fprintf(stderr, "Video-mode migration/rejection failed.\n");
            return 1;
        }
    }

    puts("All video-mode identifier tests passed.");
    return 0;
}
