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
        video_mode_id_t parsed = VIDEO_MODE_NATIVE;

        if (video_mode_from_identifier("PAL-576I", &parsed) < 0 ||
            parsed != VIDEO_MODE_PAL_576I ||
            video_mode_from_identifier("not-a-mode", &parsed) >= 0 ||
            video_mode_from_identifier(NULL, &parsed) >= 0 ||
            video_mode_from_identifier("native", NULL) >= 0) {
            fprintf(stderr, "Video-mode parser rejection failed.\n");
            return 1;
        }
    }

    puts("All video-mode identifier tests passed.");
    return 0;
}
