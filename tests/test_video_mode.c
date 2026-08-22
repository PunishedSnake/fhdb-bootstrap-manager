#include <stdio.h>
#include <string.h>

#include "gs_packet_budget.h"
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
            video_mode_geometry((video_mode_id_t)i) == NULL ||
            video_mode_from_identifier(identifier, &parsed) < 0 ||
            parsed != (video_mode_id_t)i) {
            fprintf(stderr, "Video-mode round trip failed at %u.\n", i);
            return 1;
        }
    }

    {
        const video_mode_geometry_t *mode;
        const unsigned int alternate_reserved_bytes =
            2u * 640u * 1080u * 2u;
        const unsigned int native_bytes = 2u * 640u * 224u * 4u;
        const unsigned int font_bytes = 2u * 128u * 128u * 4u;
        const unsigned int gs_vram_bytes = 4u * 1024u * 1024u;

        for (i = 0; i < VIDEO_MODE_COUNT; i++) {
            unsigned int frame_bytes;

            mode = video_mode_geometry((video_mode_id_t)i);
            frame_bytes = mode->frame_width * mode->frame_height *
                          mode->bits_per_pixel / 8u;
            if (mode->signal_width == 0u || mode->signal_height == 0u ||
                mode->surface_width == 0u || mode->surface_height == 0u ||
                mode->frame_width < mode->surface_width ||
                mode->frame_height < mode->surface_height ||
                (mode->frame_width & 63u) != 0u ||
                mode->viewport_x + mode->viewport_width >
                    mode->surface_width ||
                mode->viewport_y + mode->viewport_height >
                    mode->surface_height ||
                mode->frame_count == 0u || mode->frame_count > 2u ||
                (mode->bits_per_pixel != 16u &&
                 mode->bits_per_pixel != 32u)) {
                fprintf(stderr, "Invalid video geometry at %u.\n", i);
                return 1;
            }
            if (i != VIDEO_MODE_NATIVE &&
                frame_bytes >
                    alternate_reserved_bytes / mode->frame_count) {
                fprintf(stderr, "Video mode %u exceeds reserved VRAM.\n", i);
                return 1;
            }
            if (gs_ui_clear_packet_required_qwords(mode->frame_width,
                                                   mode->frame_count) >
                GS_UI_CLEAR_PACKET_QWORDS) {
                fprintf(stderr,
                        "Video mode %u exceeds the GS clear packet.\n", i);
                return 1;
            }
        }
        if (video_mode_geometry((video_mode_id_t)VIDEO_MODE_COUNT) != NULL) {
            fprintf(stderr, "Out-of-range video geometry was accepted.\n");
            return 1;
        }

        mode = video_mode_geometry(VIDEO_MODE_480P);
        if (gs_ui_clear_packet_required_qwords(mode->frame_width,
                                               mode->frame_count) != 100u) {
            fprintf(stderr, "480p clear-packet budget is incorrect.\n");
            return 1;
        }
        mode = video_mode_geometry(VIDEO_MODE_576P);
        if (mode->bits_per_pixel != 32u || mode->frame_count != 1u ||
            mode->frame_width != 768u || mode->frame_height != 576u) {
            fprintf(stderr, "576p regressed from its 32-bit single surface.\n");
            return 1;
        }
        mode = video_mode_geometry(VIDEO_MODE_720P);
        if (mode->bits_per_pixel != 32u || mode->frame_count != 1u ||
            mode->frame_width != 640u || mode->frame_height != 720u) {
            fprintf(stderr, "720p regressed from its 32-bit single surface.\n");
            return 1;
        }
        mode = video_mode_geometry(VIDEO_MODE_1080I);
        if (mode->bits_per_pixel != 32u || mode->frame_count != 2u ||
            mode->frame_width != 640u || mode->frame_height != 540u) {
            fprintf(stderr, "1080i FRAME storage is not 640x540x32x2.\n");
            return 1;
        }
        if (native_bytes + alternate_reserved_bytes + font_bytes >
            gs_vram_bytes) {
            fprintf(stderr, "Frame and dual-font reservations exceed VRAM.\n");
            return 1;
        }
    }

    {
        static const char *const legacy_modes[] = {
            "ntsc-480i", "PAL-576I"
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
