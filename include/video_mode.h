#ifndef PS2_HDD_BOOTSTRAP_MANAGER_VIDEO_MODE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_VIDEO_MODE_H

#define VIDEO_MODE_COUNT 7u

typedef enum {
    VIDEO_MODE_NATIVE = 0,
    VIDEO_MODE_NTSC_480I,
    VIDEO_MODE_PAL_576I,
    VIDEO_MODE_480P,
    VIDEO_MODE_576P,
    VIDEO_MODE_720P,
    VIDEO_MODE_1080I
} video_mode_id_t;

const char *video_mode_name(video_mode_id_t mode);
const char *video_mode_identifier(video_mode_id_t mode);
int video_mode_from_identifier(const char *identifier,
                               video_mode_id_t *mode_out);

#endif
