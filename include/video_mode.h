#ifndef PS2_HDD_BOOTSTRAP_MANAGER_VIDEO_MODE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_VIDEO_MODE_H

#define VIDEO_MODE_COUNT 5u

/* video_mode_from_identifier() returns this value when it accepts a legacy
 * v0.4.1 identifier but safely maps it to native output. */
#define VIDEO_MODE_MIGRATED 1

typedef enum {
    VIDEO_MODE_NATIVE = 0,
    VIDEO_MODE_480P,
    VIDEO_MODE_576P,
    VIDEO_MODE_720P,
    VIDEO_MODE_1080I
} video_mode_id_t;

typedef struct {
    unsigned int signal_width;
    unsigned int signal_height;
    unsigned int surface_width;
    unsigned int surface_height;
    unsigned int frame_width;
    unsigned int frame_height;
    unsigned int viewport_x;
    unsigned int viewport_y;
    unsigned int viewport_width;
    unsigned int viewport_height;
    unsigned int bits_per_pixel;
    unsigned int frame_count;
    int interlaced;
    int hardware_validated;
} video_mode_geometry_t;

const char *video_mode_name(video_mode_id_t mode);
const char *video_mode_identifier(video_mode_id_t mode);
const video_mode_geometry_t *video_mode_geometry(video_mode_id_t mode);
int video_mode_from_identifier(const char *identifier,
                               video_mode_id_t *mode_out);

#endif
