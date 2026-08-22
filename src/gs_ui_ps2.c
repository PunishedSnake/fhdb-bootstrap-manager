/*
 * Application-wide 2D frontend rendered through the PlayStation 2 GS.
 *
 * Physical testing proved the existing PS2SDK/libdebug CRT bootstrap on the
 * target console. It establishes the interlaced FIELD output, read circuit and
 * framebuffer at VRAM 0. This module owns every application pixel afterward.
 *
 * IMPORTANT: the hardware-proven drawing space is 640x224. Earlier Michishirube
 * builds designed a 640x448 UI and divided Y by two at submission time. That
 * fractional transformation damaged the 8x8 font raster on real hardware.
 * Everything below is now authored directly in native 640x224 coordinates.
 */

#include <kernel.h>
#include <tamtypes.h>

#include <debug.h>
#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <gs_psm.h>
#include <packet.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_identity.h"
#include "gs_ui_ps2.h"
#include "ui_theme_ps2.h"
#include "version.h"

#define GS_UI_WIDTH 640
#define GS_UI_HEIGHT 224
#define GS_UI_ALT_STORAGE_WIDTH 768
#define GS_UI_ALT_STORAGE_HEIGHT 448
#define GS_UI_FRAME_COUNT 2
#define GS_UI_FONT_SRC_W 8
#define GS_UI_FONT_SRC_H 8
#define GS_UI_GLYPH_W 8
#define GS_UI_GLYPH_H 8
#define GS_UI_LINE_STEP 10
#define GS_UI_ATLAS_W 128
#define GS_UI_ATLAS_H 64
#define GS_UI_PACKET_QWORDS 16384
#define GS_UI_CONTEXT 0
#define GS_UI_CONSOLE_BYTES 8192u
#define GS_UI_MAX_MENU_ITEMS 12u

extern const u8 msx[];

typedef struct {
    int interlace;
    int graph_mode;
    int frame_mode;
    int flicker_filter;
    int screen_x;
    int screen_y;
    unsigned int visible_width;
    unsigned int visible_height;
    unsigned int frame_width;
    unsigned int frame_height;
    unsigned int psm;
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;
    int filtered_presentation;
} video_mode_spec_t;

static const video_mode_spec_t video_specs[VIDEO_MODE_COUNT] = {
    {GRAPH_MODE_INTERLACED, GRAPH_MODE_AUTO, GRAPH_MODE_FIELD, GRAPH_ENABLE,
     0, 0, 640, 224, 640, 224, GS_PSM_32,
     1.0f, 1.0f, 0.0f, 0.0f, 1},
    {GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_480P, GRAPH_MODE_FRAME,
     GRAPH_DISABLE, 0, 0, 720, 448, 768, 448, GS_PSM_32,
     1.125f, 2.0f, 0.0f, 0.0f, 0}
};

static framebuffer_t native_frames[GS_UI_FRAME_COUNT];
static framebuffer_t alternate_frames[GS_UI_FRAME_COUNT];
static framebuffer_t *active_frames = native_frames;
static zbuffer_t zbuffer;
static texbuffer_t font_texture;
static clutbuffer_t no_clut;
static lod_t font_lod;
static blend_t alpha_blend;
static packet_t *render_packet;
static unsigned int draw_frame_index;
static float render_scale_x = 1.0f;
static float render_scale_y = 1.0f;
static float render_offset_x;
static float render_offset_y;
static unsigned int render_visible_width = GS_UI_WIDTH;
static unsigned int render_visible_height = GS_UI_HEIGHT;
static int render_filtered = 1;
static video_mode_id_t video_mode = VIDEO_MODE_NATIVE;
static int draw_state_dirty;
static u32 font_atlas[GS_UI_ATLAS_W * GS_UI_ATLAS_H]
    __attribute__((aligned(64)));
static char console_buffer[GS_UI_CONSOLE_BYTES];
static unsigned int console_used;
static int console_dirty;
static int renderer_ready;
static int blending_enabled = -1;

static float scaled_x(float value)
{
    if (render_scale_x == 1.0f)
        return value + render_offset_x;
    return value * render_scale_x + render_offset_x;
}

static float scaled_y(float value)
{
    if (render_scale_y == 1.0f)
        return value + render_offset_y;
    if (render_scale_y == 2.0f)
        return value + value + render_offset_y;
    return value * render_scale_y + render_offset_y;
}

static unsigned int active_visible_width(void)
{
    return render_visible_width;
}

static void apply_render_spec(const video_mode_spec_t *spec)
{
    render_scale_x = spec->scale_x;
    render_scale_y = spec->scale_y;
    render_offset_x = spec->offset_x;
    render_offset_y = spec->offset_y;
    render_visible_width = spec->visible_width;
    render_visible_height = spec->visible_height;
    render_filtered = spec->filtered_presentation;
}

static void present_framebuffer(const framebuffer_t *frame)
{
    if (!render_filtered) {
        /* Non-interlaced output uses read circuit 2. Do not use the filtered
           helper here: it offsets DISPFB2 by one row for interlaced flicker
           filtering and would silently discard the first progressive row. */
        graph_set_framebuffer(1, frame->address, frame->width,
                              frame->psm, 0, 0);
        return;
    }
    graph_set_framebuffer_filtered(frame->address, frame->width,
                                   frame->psm, 0, 0);
}

static void select_blending(int enabled)
{
    if (blending_enabled == enabled)
        return;
    if (enabled)
        draw_enable_blending();
    else
        draw_disable_blending();
    blending_enabled = enabled;
}

static void set_color(color_t *color, ui_rgb_t rgb)
{
    color->r = rgb.r;
    color->g = rgb.g;
    color->b = rgb.b;
    color->a = 0x80;
    color->q = 1.0f;
}

static qword_t *filled_rect_rgb(qword_t *q, float x0, float y0,
                                float x1, float y1, ui_rgb_t rgb)
{
    rect_t rect;

    rect.v0.x = scaled_x(x0);
    rect.v0.y = scaled_y(y0);
    rect.v0.z = 1;
    rect.v1.x = scaled_x(x1);
    rect.v1.y = scaled_y(y1);
    rect.v1.z = 1;
    set_color(&rect.color, rgb);
    select_blending(0);
    return draw_rect_filled(q, GS_UI_CONTEXT, &rect);
}

static qword_t *outline_rect_rgb(qword_t *q, float x0, float y0,
                                 float x1, float y1, ui_rgb_t rgb)
{
    rect_t rect;

    rect.v0.x = scaled_x(x0);
    rect.v0.y = scaled_y(y0);
    rect.v0.z = 1;
    rect.v1.x = scaled_x(x1);
    rect.v1.y = scaled_y(y1);
    rect.v1.z = 1;
    set_color(&rect.color, rgb);
    select_blending(0);
    return draw_rect_outline(q, GS_UI_CONTEXT, &rect);
}

static qword_t *text_char(qword_t *q, float x, float y, unsigned char ch,
                          const color_t *color)
{
    texrect_t glyph;
    unsigned int glyph_x;
    unsigned int glyph_y;

    if (ch >= 128u)
        ch = '?';
    glyph_x = ((unsigned int)ch & 15u) * GS_UI_FONT_SRC_W;
    glyph_y = ((unsigned int)ch >> 4) * GS_UI_FONT_SRC_H;

    glyph.v0.x = scaled_x(x);
    glyph.v0.y = scaled_y(y);
    glyph.v0.z = 2;
    glyph.v1.x = scaled_x(x + GS_UI_GLYPH_W);
    glyph.v1.y = scaled_y(y + GS_UI_GLYPH_H);
    glyph.v1.z = 2;
    glyph.t0.u = (float)glyph_x;
    glyph.t0.v = (float)glyph_y;
    glyph.t1.u = (float)(glyph_x + GS_UI_FONT_SRC_W);
    glyph.t1.v = (float)(glyph_y + GS_UI_FONT_SRC_H);
    glyph.color = *color;

    select_blending(1);
    return draw_rect_textured(q, GS_UI_CONTEXT, &glyph);
}

static qword_t *text_string_box(qword_t *q, float x, float y,
                                float max_x, float max_y,
                                const char *text, ui_rgb_t rgb)
{
    float cursor_x = x;
    float cursor_y = y;
    color_t color;

    if (text == NULL)
        return q;
    set_color(&color, rgb);
    while (*text != '\0' && cursor_y + GS_UI_GLYPH_H <= max_y) {
        unsigned char ch = (unsigned char)*text++;

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += GS_UI_LINE_STEP;
            continue;
        }
        if (cursor_x + GS_UI_GLYPH_W > max_x) {
            cursor_x = x;
            cursor_y += GS_UI_LINE_STEP;
            if (cursor_y + GS_UI_GLYPH_H > max_y)
                break;
        }
        q = text_char(q, cursor_x, cursor_y, ch, &color);
        cursor_x += GS_UI_GLYPH_W;
    }
    return q;
}

static qword_t *text_string(qword_t *q, float x, float y,
                            const char *text, ui_rgb_t rgb)
{
    return text_string_box(q, x, y, GS_UI_WIDTH - 14.0f,
                           GS_UI_HEIGHT - 4.0f, text, rgb);
}

static void build_font_atlas(void)
{
    unsigned int ch;

    memset(font_atlas, 0, sizeof(font_atlas));
    for (ch = 0; ch < 128u; ch++) {
        unsigned int gx = (ch & 15u) * GS_UI_FONT_SRC_W;
        unsigned int gy = (ch >> 4) * GS_UI_FONT_SRC_H;
        unsigned int row;

        for (row = 0; row < GS_UI_FONT_SRC_H; row++) {
            unsigned char bits = msx[ch * GS_UI_FONT_SRC_H + row];
            unsigned int col;

            for (col = 0; col < GS_UI_FONT_SRC_W; col++) {
                if ((bits & (0x80u >> col)) != 0u)
                    font_atlas[(gy + row) * GS_UI_ATLAS_W + gx + col] =
                        0x80ffffffu;
            }
        }
    }
    FlushCache(0);
}

static int allocate_frame_pair(framebuffer_t pair[GS_UI_FRAME_COUNT],
                               unsigned int width, unsigned int height,
                               unsigned int psm,
                               int require_zero_address)
{
    unsigned int i;

    for (i = 0; i < GS_UI_FRAME_COUNT; i++) {
        int address = graph_vram_allocate(width, height, psm,
                                          GRAPH_ALIGN_PAGE);

        if (address < 0 ||
            (require_zero_address && i == 0u && address != 0))
            return -1;
        pair[i].address = (unsigned int)address;
        pair[i].width = width;
        pair[i].height = height;
        pair[i].psm = psm;
        pair[i].mask = 0;
    }
    return 0;
}

static int video_specs_fit_reserved_vram(void)
{
    const unsigned int reserved_words =
        GS_UI_ALT_STORAGE_WIDTH * GS_UI_ALT_STORAGE_HEIGHT;
    unsigned int i;

    for (i = 1u; i < VIDEO_MODE_COUNT; i++) {
        const video_mode_spec_t *spec = &video_specs[i];
        unsigned int words;

        if ((spec->frame_width & 63u) != 0u ||
            spec->visible_width > spec->frame_width ||
            spec->visible_height > spec->frame_height)
            return 0;
        if (spec->psm == GS_PSM_32)
            words = spec->frame_width * spec->frame_height;
        else if (spec->psm == GS_PSM_16 &&
                 (spec->frame_height & 1u) == 0u)
            words = spec->frame_width * (spec->frame_height >> 1);
        else
            return 0;
        if (words > reserved_words)
            return 0;
    }
    return 1;
}

static void configure_alternate_frames(const video_mode_spec_t *spec)
{
    unsigned int i;

    /* The addresses describe the hardware-tested 768x448x32-bit 480p pair. */
    for (i = 0; i < GS_UI_FRAME_COUNT; i++) {
        alternate_frames[i].width = spec->frame_width;
        alternate_frames[i].height = spec->frame_height;
        alternate_frames[i].psm = spec->psm;
        alternate_frames[i].mask = 0;
    }
}

static int clear_frame_pair(framebuffer_t pair[GS_UI_FRAME_COUNT])
{
    packet_t *packet;
    qword_t *q;
    unsigned int i;

    packet = packet_init(64, PACKET_NORMAL);
    if (packet == NULL)
        return -1;
    q = packet->data;
    for (i = 0; i < GS_UI_FRAME_COUNT; i++) {
        q = draw_setup_environment(q, GS_UI_CONTEXT, &pair[i], &zbuffer);
        q = draw_clear(q, GS_UI_CONTEXT, 0, 0,
                       pair[i].width, pair[i].height, 0, 0, 0);
    }
    q = draw_finish(q);
    dma_wait_fast();
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                            (int)(q - packet->data), 0, 0);
    draw_wait_finish();
    packet_free(packet);
    return 0;
}

static int setup_environment(void)
{
    packet_t *packet;
    qword_t *q;
    unsigned int i;
    int texture_address;

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    /* Native frame zero remains at VRAM 0 for init_scr()/emergency libdebug
       compatibility. The alternate pair is the exact hardware-tested 480p
       backing layout. Four buffers plus the font atlas use 3.75 MiB of GS
       VRAM and leave 256 KiB free. */
    graph_vram_clear();
    if (!video_specs_fit_reserved_vram() ||
        allocate_frame_pair(native_frames, GS_UI_WIDTH, GS_UI_HEIGHT,
                            GS_PSM_32, 1) < 0 ||
        allocate_frame_pair(alternate_frames, GS_UI_ALT_STORAGE_WIDTH,
                            GS_UI_ALT_STORAGE_HEIGHT, GS_PSM_32, 0) < 0)
        return -1;

    texture_address = graph_vram_allocate(GS_UI_ATLAS_W, GS_UI_ATLAS_H,
                                          GS_PSM_32, GRAPH_ALIGN_BLOCK);
    if (texture_address < 0)
        return -2;

    font_texture.address = (unsigned int)texture_address;

    zbuffer.enable = DRAW_DISABLE;
    zbuffer.method = ZTEST_METHOD_ALLPASS;
    zbuffer.address = 0;
    zbuffer.zsm = GS_ZBUF_32;
    zbuffer.mask = 1;

    packet = packet_init(256, PACKET_NORMAL);
    if (packet == NULL)
        return -3;
    q = packet->data;
    /* Avoid exposing uninitialized VRAM during the short interval between a
       read-circuit change and the first fully rendered frame. */
    for (i = 0; i < GS_UI_FRAME_COUNT; i++) {
        q = draw_setup_environment(q, GS_UI_CONTEXT, &native_frames[i],
                                   &zbuffer);
        q = draw_clear(q, GS_UI_CONTEXT, 0, 0, GS_UI_WIDTH, GS_UI_HEIGHT,
                       0, 0, 0);
    }
    for (i = 0; i < GS_UI_FRAME_COUNT; i++) {
        q = draw_setup_environment(q, GS_UI_CONTEXT, &alternate_frames[i],
                                   &zbuffer);
        q = draw_clear(q, GS_UI_CONTEXT, 0, 0,
                       GS_UI_ALT_STORAGE_WIDTH, GS_UI_ALT_STORAGE_HEIGHT,
                       0, 0, 0);
    }
    active_frames = native_frames;
    draw_frame_index = 1u;
    q = draw_setup_environment(q, GS_UI_CONTEXT,
                               &active_frames[draw_frame_index], &zbuffer);
    /* libdraw draw2d primitives add the GS +2048 bias themselves. */
    q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f, 2048.0f);
    q = draw_scissor_area(q, GS_UI_CONTEXT, 0, GS_UI_WIDTH - 1,
                          0, GS_UI_HEIGHT - 1);
    q = draw_finish(q);
    dma_wait_fast();
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                            (int)(q - packet->data), 0, 0);
    draw_wait_finish();
    packet_free(packet);
    return 0;
}

static int upload_font_texture(void)
{
    packet_t *packet;
    qword_t *q;

    build_font_atlas();
    packet = packet_init(4096, PACKET_NORMAL);
    if (packet == NULL)
        return -1;
    q = packet->data;
    q = draw_texture_transfer(q, font_atlas, GS_UI_ATLAS_W, GS_UI_ATLAS_H,
                              GS_PSM_32, font_texture.address,
                              GS_UI_ATLAS_W);
    q = draw_texture_flush(q);
    dma_wait_fast();
    dma_channel_send_chain(DMA_CHANNEL_GIF, packet->data,
                           (int)(q - packet->data), 0, 0);
    dma_wait_fast();
    packet_free(packet);
    return 0;
}

static int setup_texture_state(void)
{
    packet_t *packet;
    qword_t *q;

    font_texture.width = GS_UI_ATLAS_W;
    font_texture.psm = GS_PSM_32;
    font_texture.info.width = draw_log2(GS_UI_ATLAS_W);
    font_texture.info.height = draw_log2(GS_UI_ATLAS_H);
    font_texture.info.components = TEXTURE_COMPONENTS_RGBA;
    font_texture.info.function = TEXTURE_FUNCTION_MODULATE;

    memset(&no_clut, 0, sizeof(no_clut));
    no_clut.storage_mode = CLUT_STORAGE_MODE1;
    no_clut.load_method = CLUT_NO_LOAD;

    memset(&font_lod, 0, sizeof(font_lod));
    font_lod.calculation = LOD_USE_K;
    font_lod.max_level = 0;
    font_lod.mag_filter = LOD_MAG_NEAREST;
    font_lod.min_filter = LOD_MIN_NEAREST;

    alpha_blend.color1 = BLEND_COLOR_SOURCE;
    alpha_blend.color2 = BLEND_COLOR_DEST;
    alpha_blend.alpha = BLEND_ALPHA_SOURCE;
    alpha_blend.color3 = BLEND_COLOR_DEST;
    alpha_blend.fixed_alpha = 0x80;

    packet = packet_init(64, PACKET_NORMAL);
    if (packet == NULL)
        return -1;
    q = packet->data;
    q = draw_texture_sampling(q, GS_UI_CONTEXT, &font_lod);
    q = draw_texturebuffer(q, GS_UI_CONTEXT, &font_texture, &no_clut);
    q = draw_alpha_blending(q, GS_UI_CONTEXT, &alpha_blend);
    q = draw_finish(q);
    dma_wait_fast();
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                            (int)(q - packet->data), 0, 0);
    draw_wait_finish();
    packet_free(packet);
    return 0;
}

static qword_t *begin_frame(packet_t **packet_out)
{
    packet_t *packet;
    qword_t *q;

    dma_wait_fast();
    packet = render_packet;
    q = packet->data;

    if (draw_state_dirty) {
        q = draw_setup_environment(q, GS_UI_CONTEXT,
                                   &active_frames[draw_frame_index], &zbuffer);
        q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f, 2048.0f);
        q = draw_scissor_area(q, GS_UI_CONTEXT, 0,
                              active_visible_width() - 1,
                              0, render_visible_height - 1);
        q = draw_texture_sampling(q, GS_UI_CONTEXT, &font_lod);
        q = draw_texturebuffer(q, GS_UI_CONTEXT, &font_texture, &no_clut);
        q = draw_alpha_blending(q, GS_UI_CONTEXT, &alpha_blend);
        draw_state_dirty = 0;
    } else {
        q = draw_framebuffer(q, GS_UI_CONTEXT,
                             &active_frames[draw_frame_index]);
    }

    *packet_out = packet;
    return q;
}

static void end_frame(packet_t *packet, qword_t *q)
{
    unsigned int completed_frame = draw_frame_index;

    q = draw_finish(q);
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                            (int)(q - packet->data), 0, 0);
    draw_wait_finish();
    graph_wait_vsync();
    present_framebuffer(&active_frames[completed_frame]);
    draw_frame_index ^= 1u;
    console_dirty = 0;
}

static ui_rgb_t tone_color(gs_ui_tone_t tone,
                           const ui_theme_palette_t *theme)
{
    switch (tone) {
        case GS_UI_TONE_SUCCESS: return theme->success;
        case GS_UI_TONE_WARNING: return theme->warning;
        case GS_UI_TONE_DANGER: return theme->danger;
        case GS_UI_TONE_INFO:
        default: return theme->accent;
    }
}

static qword_t *draw_shell(qword_t *q, const char *section,
                           gs_ui_tone_t tone)
{
    const ui_theme_palette_t *theme = ui_theme_current();
    ui_rgb_t accent = tone_color(tone, theme);

    q = filled_rect_rgb(q, 0, 0, GS_UI_WIDTH, GS_UI_HEIGHT,
                        theme->background);
    q = filled_rect_rgb(q, 0, 0, GS_UI_WIDTH, 4, accent);
    q = filled_rect_rgb(q, 12, 8, 628, 29, theme->panel);
    q = outline_rect_rgb(q, 12, 8, 628, 29, theme->border);
    q = text_string(q, 22, 14, APP_NAME "  v" APP_VERSION, theme->text);
    if (section != NULL && section[0] != '\0')
        q = text_string_box(q, 500, 14, 618, 23, section, accent);
    return q;
}

int gs_ui_initialize(void)
{
    if (renderer_ready)
        return 0;
    if (setup_environment() < 0)
        return -1;
    if (upload_font_texture() < 0)
        return -2;
    if (setup_texture_state() < 0)
        return -3;

    /* end_frame() waits for GS FINISH before returning, so a second 256 KiB
       packet cannot overlap useful work. Reuse one packet and leave that EE
       memory available to the forensic workspace. */
    render_packet = packet_init(GS_UI_PACKET_QWORDS, PACKET_NORMAL);
    if (render_packet == NULL)
        return -4;

    console_buffer[0] = '\0';
    console_used = 0;
    console_dirty = 0;
    draw_state_dirty = 0;
    blending_enabled = -1;
    renderer_ready = 1;
    gs_ui_render_message("Starting",
                         "Graphics Synthesizer frontend ready.",
                         NULL, GS_UI_TONE_INFO);
    return 0;
}

int gs_ui_is_ready(void)
{
    return renderer_ready;
}

video_mode_id_t gs_ui_video_mode_current(void)
{
    return video_mode;
}

int gs_ui_video_mode_supported(video_mode_id_t mode)
{
    return (unsigned int)mode < VIDEO_MODE_COUNT;
}

static void restore_native_video(void)
{
    /* This is deliberately the same read-circuit bootstrap already proven on
       physical hardware. It is also the timed escape hatch from any unsupported
       display, cable or alternate-mode combination. */
    init_scr();
    /* libdebug's init_scr() resets DMA globally. Re-establish libdma's GIF
       channel state before the next application frame is submitted. */
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    active_frames = native_frames;
    apply_render_spec(&video_specs[VIDEO_MODE_NATIVE]);
    video_mode = VIDEO_MODE_NATIVE;
    draw_frame_index = 1u;
    blending_enabled = -1;
    draw_state_dirty = 1;
}

int gs_ui_video_mode_apply(video_mode_id_t mode)
{
    const video_mode_spec_t *spec;

    if (!renderer_ready)
        return -1;
    if ((unsigned int)mode >= VIDEO_MODE_COUNT)
        return -2;
    if (!gs_ui_video_mode_supported(mode))
        return -3;
    if (mode == video_mode)
        return 0;

    dma_wait_fast();
    graph_wait_vsync();
    if (mode == VIDEO_MODE_NATIVE) {
        restore_native_video();
        return 0;
    }

    spec = &video_specs[(unsigned int)mode];
    graph_disable_output();
    if (graph_set_mode(spec->interlace, spec->graph_mode,
                       spec->frame_mode, spec->flicker_filter) < 0 ||
        graph_set_screen(spec->screen_x, spec->screen_y,
                         spec->visible_width, spec->visible_height) < 0) {
        restore_native_video();
        return -4;
    }

    configure_alternate_frames(spec);
    if (clear_frame_pair(alternate_frames) < 0) {
        restore_native_video();
        return -5;
    }

    active_frames = alternate_frames;
    apply_render_spec(spec);
    video_mode = mode;
    draw_frame_index = 0u;
    graph_set_bgcolor(0, 0, 0);
    present_framebuffer(&alternate_frames[1]);
    blending_enabled = -1;
    draw_state_dirty = 1;
    graph_enable_output();
    return 0;
}

void gs_ui_render_menu(const char *title,
                       const char *status,
                       const char *const *labels,
                       const char *const *hints,
                       const unsigned char *enabled,
                       unsigned int item_count,
                       unsigned int selected)
{
    const ui_theme_palette_t *theme = ui_theme_current();
    packet_t *packet;
    qword_t *q;
    unsigned int i;
    float content_y;
    float available;
    float row_height;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    if (item_count > GS_UI_MAX_MENU_ITEMS)
        item_count = GS_UI_MAX_MENU_ITEMS;

    q = begin_frame(&packet);
    q = draw_shell(q, "MANAGER", GS_UI_TONE_INFO);
    q = text_string_box(q, 20, 35, 620, 44,
                        title != NULL ? title : "Menu", theme->text);

    if (status != NULL && status[0] != '\0') {
        q = filled_rect_rgb(q, 16, 48, 624, 66, theme->panel);
        q = outline_rect_rgb(q, 16, 48, 624, 66, theme->border);
        q = text_string_box(q, 26, 53, 614, 62, status, theme->muted);
        content_y = 70.0f;
    } else {
        content_y = 50.0f;
    }

    available = 203.0f - content_y;
    row_height = item_count != 0u ? available / (float)item_count : available;
    if (row_height > 32.0f)
        row_height = 32.0f;
    if (row_height < 11.0f)
        row_height = 11.0f;

    for (i = 0; i < item_count; i++) {
        float y0 = content_y + (float)i * row_height;
        float y1 = y0 + row_height - 2.0f;
        int is_enabled = enabled == NULL || enabled[i] != 0u;
        int is_selected = i == selected;
        ui_rgb_t row_bg;
        ui_rgb_t border;
        ui_rgb_t bar;
        ui_rgb_t label_color;
        ui_rgb_t hint_color;

        if (y1 > 202.0f)
            break;

        if (!is_enabled) {
            row_bg = theme->disabled_bg;
            border = theme->disabled_border;
            bar = theme->warning;
            label_color = theme->disabled_text;
            hint_color = theme->disabled_text;
        } else if (is_selected) {
            row_bg = theme->panel_alt;
            border = theme->border;
            bar = theme->accent;
            label_color = theme->text;
            hint_color = theme->muted;
        } else {
            row_bg = theme->panel;
            border = theme->panel;
            bar = theme->panel;
            label_color = theme->text;
            hint_color = theme->muted;
        }

        q = filled_rect_rgb(q, 18, y0, 622, y1, row_bg);
        if (is_selected || !is_enabled)
            q = outline_rect_rgb(q, 18, y0, 622, y1, border);
        q = filled_rect_rgb(q, 18, y0, 23, y1, bar);

        if (row_height >= 23.0f) {
            q = text_string_box(q, 34, y0 + 3.0f,
                                is_enabled ? 610.0f : 528.0f,
                                y0 + 12.0f,
                                labels != NULL && labels[i] != NULL
                                    ? labels[i] : "(unnamed)",
                                label_color);
            if (!is_enabled)
                q = text_string_box(q, 545, y0 + 3.0f, 610, y0 + 12.0f,
                                    "LOCKED", theme->warning);
            if (hints != NULL && hints[i] != NULL)
                q = text_string_box(q, 34, y0 + 13.0f, 610, y1 - 1.0f,
                                    hints[i], hint_color);
        } else {
            q = text_string_box(q, 34, y0 + 2.0f,
                                is_enabled ? 610.0f : 528.0f,
                                y1 - 1.0f,
                                labels != NULL && labels[i] != NULL
                                    ? labels[i] : "(unnamed)",
                                label_color);
            if (!is_enabled)
                q = text_string_box(q, 545, y0 + 2.0f, 610, y1 - 1.0f,
                                    "LOCKED", theme->warning);
        }
    }

    q = filled_rect_rgb(q, 0, 205, GS_UI_WIDTH, GS_UI_HEIGHT,
                        theme->panel_alt);
    q = text_string_box(q, 20, 211, 620, 220,
                        "UP/DOWN Select       X Open       TRIANGLE Back",
                        theme->muted);
    end_frame(packet, q);
}

void gs_ui_render_message(const char *title,
                          const char *body,
                          const char *footer,
                          gs_ui_tone_t tone)
{
    const ui_theme_palette_t *theme = ui_theme_current();
    packet_t *packet;
    qword_t *q;
    ui_rgb_t accent;
    float body_bottom = footer != NULL && footer[0] != '\0' ? 194.0f : 211.0f;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    accent = tone_color(tone, theme);

    q = begin_frame(&packet);
    q = draw_shell(q, "STATUS", tone);
    q = text_string_box(q, 20, 36, 620, 45,
                        title != NULL ? title : "Information", accent);
    q = filled_rect_rgb(q, 16, 49, 624, body_bottom, theme->panel);
    q = outline_rect_rgb(q, 16, 49, 624, body_bottom, theme->border);
    q = text_string_box(q, 28, 58, 612, body_bottom - 7.0f,
                        body != NULL ? body : "", theme->text);
    if (footer != NULL && footer[0] != '\0') {
        q = filled_rect_rgb(q, 0, 201, GS_UI_WIDTH, GS_UI_HEIGHT,
                            theme->panel_alt);
        q = text_string_box(q, 20, 209, 620, 219, footer, theme->muted);
    }
    end_frame(packet, q);
}

static void render_console_buffer(void)
{
    char title[96];
    const char *body = console_buffer;
    const char *newline;
    unsigned int title_len;

    if (!renderer_ready)
        return;
    if (console_buffer[0] == '\0') {
        gs_ui_render_message("Ready", "", NULL, GS_UI_TONE_INFO);
        return;
    }

    newline = strchr(console_buffer, '\n');
    if (newline != NULL && newline != console_buffer) {
        title_len = (unsigned int)(newline - console_buffer);
        if (title_len >= sizeof(title))
            title_len = sizeof(title) - 1u;
        memcpy(title, console_buffer, title_len);
        title[title_len] = '\0';
        body = newline + 1;
        if (strncmp(title, APP_NAME, sizeof(APP_NAME) - 1u) == 0)
            snprintf(title, sizeof(title), "Manager status");
    } else {
        snprintf(title, sizeof(title), "Manager status");
    }

    gs_ui_render_message(title, body, NULL, GS_UI_TONE_INFO);
}

void gs_ui_console_clear(void)
{
    console_buffer[0] = '\0';
    console_used = 0;
    console_dirty = 1;
}

void gs_ui_console_vprintf(const char *format, va_list arguments)
{
    int written;

    if (format == NULL)
        return;
    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    if (console_used >= GS_UI_CONSOLE_BYTES - 1u)
        return;

    written = vsnprintf(console_buffer + console_used,
                        GS_UI_CONSOLE_BYTES - console_used,
                        format, arguments);
    if (written < 0)
        return;
    if ((unsigned int)written >= GS_UI_CONSOLE_BYTES - console_used)
        console_used = GS_UI_CONSOLE_BYTES - 1u;
    else
        console_used += (unsigned int)written;
    console_buffer[console_used] = '\0';
    console_dirty = 1;
}

void gs_ui_console_present(void)
{
    if (!console_dirty)
        return;
    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    render_console_buffer();
}

void gs_ui_console_printf(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    gs_ui_console_vprintf(format, arguments);
    va_end(arguments);
}

void gs_ui_render_disk_status(const char *operation,
                              const char *phase,
                              const char *location,
                              const char *io_kind,
                              unsigned int percent,
                              unsigned int progress_current,
                              unsigned int progress_total,
                              unsigned int lba,
                              unsigned int sectors,
                              int write_sensitive)
{
    const ui_theme_palette_t *theme = ui_theme_current();
    packet_t *packet;
    qword_t *q;
    char line[160];
    float progress_width;
    ui_rgb_t accent = write_sensitive ? theme->warning : theme->accent;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    if (percent > 100u)
        percent = 100u;

    q = begin_frame(&packet);
    q = draw_shell(q, "LIVE HDD", write_sensitive
                                  ? GS_UI_TONE_WARNING : GS_UI_TONE_INFO);

    q = filled_rect_rgb(q, 16, 35, 624, 96, theme->panel);
    q = outline_rect_rgb(q, 16, 35, 624, 96, theme->border);
    q = text_string(q, 28, 42, "OPERATION", theme->muted);
    q = text_string_box(q, 124, 42, 612, 51,
                        operation != NULL ? operation : "HDD activity",
                        theme->text);
    q = text_string(q, 28, 56, "ACTION", theme->muted);
    q = text_string_box(q, 124, 56, 612, 65,
                        phase != NULL && phase[0] != '\0'
                            ? phase : io_kind,
                        theme->text);
    q = text_string(q, 28, 70, "LOCATION", theme->muted);
    q = text_string_box(q, 124, 70, 612, 79,
                        location != NULL ? location : "HDD",
                        theme->text);
    q = text_string(q, 28, 84, "I/O", theme->muted);
    q = text_string_box(q, 124, 84, 612, 93,
                        io_kind != NULL ? io_kind : "HDD I/O", accent);

    q = filled_rect_rgb(q, 16, 102, 624, 128, theme->panel);
    q = outline_rect_rgb(q, 16, 102, 624, 128, theme->border);
    q = filled_rect_rgb(q, 28, 111, 546, 119, theme->panel_alt);
    progress_width = 518.0f * ((float)percent / 100.0f);
    if (progress_width > 0.0f)
        q = filled_rect_rgb(q, 28, 111, 28.0f + progress_width, 119, accent);
    snprintf(line, sizeof(line), "%3u%%", percent);
    q = text_string(q, 563, 111, line, theme->text);

    q = filled_rect_rgb(q, 16, 134, 624, 199, theme->panel);
    q = outline_rect_rgb(q, 16, 134, 624, 199, theme->border);
    if (progress_total != 0u) {
        snprintf(line, sizeof(line),
                 "POSITION  0x%08x / 0x%08x sectors",
                 progress_current, progress_total);
    } else {
        snprintf(line, sizeof(line), "POSITION  disk size unavailable");
    }
    q = text_string_box(q, 28, 143, 612, 152, line, theme->muted);

    if (sectors != 0u) {
        if (sectors > 1u)
            snprintf(line, sizeof(line),
                     "SECTOR    0x%08x .. 0x%08x   (%u sectors)",
                     lba, lba + sectors - 1u, sectors);
        else
            snprintf(line, sizeof(line),
                     "SECTOR    0x%08x   (1 sector)", lba);
    } else {
        snprintf(line, sizeof(line),
                 "SECTOR    no raw sector command in this phase");
    }
    q = text_string_box(q, 28, 157, 612, 166, line, theme->text);

    if (progress_total != 0u) {
        snprintf(line, sizeof(line), "STEP      %u / %u",
                 progress_current, progress_total);
        q = text_string_box(q, 28, 171, 612, 180, line, theme->muted);
    }

    q = text_string_box(q, 28, 185, 612, 195,
                        write_sensitive
                            ? "WRITE PATH ACTIVE - do not reset or remove power"
                            : "Read-only activity - live event monitoring",
                        write_sensitive ? theme->warning : theme->success);

    q = filled_rect_rgb(q, 0, 205, GS_UI_WIDTH, GS_UI_HEIGHT,
                        theme->panel_alt);
    q = text_string_box(q, 20, 211, 620, 220,
                        "GS live monitor - operation / location / LBA / phase",
                        theme->muted);
    end_frame(packet, q);
}
