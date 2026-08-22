#ifndef PS2_HDD_BOOTSTRAP_MANAGER_VIDEO_MODE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_VIDEO_MODE_H

#define VIDEO_MODE_COUNT 2u

/* video_mode_from_identifier() returns this value when it accepts a legacy
 * v0.4.1 identifier but safely maps it to native output. */
#define VIDEO_MODE_MIGRATED 1

typedef enum {
    VIDEO_MODE_NATIVE = 0,
    VIDEO_MODE_480P
} video_mode_id_t;

const char *video_mode_name(video_mode_id_t mode);
const char *video_mode_identifier(video_mode_id_t mode);
int video_mode_from_identifier(const char *identifier,
                               video_mode_id_t *mode_out);

#endif
