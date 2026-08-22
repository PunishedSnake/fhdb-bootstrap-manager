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
#include <rom0_info.h>
#include <syscallnr.h>
#include <tamtypes.h>
#include <timer.h>

#include <debug.h>
#include <dma.h>
#include <dma_registers.h>
#include <draw.h>
#include <gif_registers.h>
#include <graph.h>
#include <gs_privileged.h>
#include <gs_psm.h>
#include <packet.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_identity.h"
#include "gs_packet_budget.h"
#include "gs_ui_ps2.h"
#include "spleen_font_data.h"
#include "ui_font.h"
#include "ui_layout.h"
#include "ui_theme_ps2.h"
#include "version.h"

#define GS_UI_WIDTH 640
#define GS_UI_HEIGHT 224
#define GS_UI_ALT_STORAGE_WIDTH 640
#define GS_UI_ALT_STORAGE_HEIGHT 1080
#define GS_UI_ALT_STORAGE_PSM GS_PSM_16
#define GS_UI_FRAME_COUNT 2
#define GS_UI_FONT_SLOT_W 8
#define GS_UI_FONT_SLOT_H 16
#define GS_UI_GLYPH_W UI_LOGICAL_CELL_WIDTH
#define GS_UI_GLYPH_H UI_LOGICAL_CELL_HEIGHT
#define GS_UI_LINE_STEP 10
#define GS_UI_ATLAS_W 128
#define GS_UI_ATLAS_H 128
#define GS_UI_FONT_VARIANT_COUNT 2u
#define GS_UI_FONT_NATIVE 0u
#define GS_UI_FONT_SCALED 1u
#define GS_UI_FONT_UPLOAD_QWORDS 32
#define GS_UI_PACKET_QWORDS 16384
#define GS_UI_CONTEXT 0
#define GS_UI_CONSOLE_BYTES 8192u
#define GS_UI_MAX_MENU_ITEMS 12u
#define GS_UI_VSYNC_TIMEOUT_MS 250u
#define GS_UI_GIF_TIMEOUT_MS 250u
#define GS_UI_GIF_CHCR (*(volatile u32 *)0x1000A000)
#define GS_UI_NATIVE_PMODE 0x000000000000FF62ULL
#define GS_UI_NATIVE_DISPFB2 0x0000000000001400ULL
#define GS_UI_NATIVE_DISPLAY2 0x001BF9FF0983227CULL

extern const u8 msx[];

typedef struct {
    video_mode_id_t id;
    int interlace;
    int graph_mode;
    int frame_mode;
    int flicker_filter;
    int screen_x;
    int screen_y;
    unsigned int psm;
    int filtered_presentation;
    int explicit_display;
    int crt_mode;
    int display_x;
    int display_y;
    unsigned int magh;
    unsigned int magv;
    unsigned int display_width;
    unsigned int display_height;
} video_mode_spec_t;

static const video_mode_spec_t video_specs[VIDEO_MODE_COUNT] = {
    {VIDEO_MODE_NATIVE,
     GRAPH_MODE_INTERLACED, GRAPH_MODE_AUTO, GRAPH_MODE_FIELD, GRAPH_ENABLE,
     0, 0, GS_PSM_32, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {VIDEO_MODE_480P,
     GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_480P, GRAPH_MODE_FRAME,
     GRAPH_DISABLE, 0, 0, GS_PSM_32, 0, 0,
     0x50, 232, 35, 1, 0, 1440, 448},
    {VIDEO_MODE_576P,
     GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_576P, GRAPH_MODE_FRAME,
     GRAPH_DISABLE, 0, 0, GS_PSM_32, 0, 1,
     0x53, 255, 44, 1, 0, 1440, 576},
    {VIDEO_MODE_720P,
     GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_720P, GRAPH_MODE_FRAME,
     GRAPH_DISABLE, 0, 0, GS_PSM_32, 0, 1,
     0x52, 306, 26, 1, 0, 1280, 720},
    {VIDEO_MODE_1080I,
     GRAPH_MODE_INTERLACED, GRAPH_MODE_HDTV_1080I, GRAPH_MODE_FRAME,
     GRAPH_DISABLE, 0, 0, GS_PSM_32, 0, 1,
     0x51, 236, 47, 2, 0, 1920, 1080}
};

typedef enum {
    VIDEO_TRANSITION_STABLE = 0,
    VIDEO_TRANSITION_SWITCHING,
    VIDEO_TRANSITION_RESTORING
} video_transition_state_t;

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int first;
    unsigned int count;
    unsigned int bytes_per_glyph;
    const u8 *data;
} ui_font_raster_t;

static framebuffer_t native_frames[GS_UI_FRAME_COUNT];
static framebuffer_t alternate_frames[GS_UI_FRAME_COUNT];
static framebuffer_t *active_frames = native_frames;
static zbuffer_t zbuffer;
static texbuffer_t font_texture;
static clutbuffer_t no_clut;
static lod_t font_lod;
static blend_t alpha_blend;
static packet_t *render_packet;
static packet_t *font_upload_packet;
static packet_t *native_bootstrap_packet;
static packet_t *frame_clear_packet;
static unsigned int font_texture_addresses[GS_UI_FONT_VARIANT_COUNT];
static unsigned int draw_frame_index;
static unsigned int active_frame_count = GS_UI_FRAME_COUNT;
static ui_layout_t render_layout;
static int render_filtered = 1;
static video_mode_id_t video_mode = VIDEO_MODE_NATIVE;
static ui_font_id_t active_font = UI_FONT_MSX;
static ui_font_raster_t font_raster;
static int draw_state_dirty;
static u32 font_atlas[GS_UI_ATLAS_W * GS_UI_ATLAS_H]
    __attribute__((aligned(64)));
static char console_buffer[GS_UI_CONSOLE_BYTES];
static unsigned int console_used;
static int console_dirty;
static int renderer_ready;
static int blending_enabled = -1;
static int frame_fault_pending;
static video_transition_state_t video_transition_state =
    VIDEO_TRANSITION_STABLE;

static float scaled_x(float value)
{
    return ui_layout_snap_x(&render_layout, value);
}

static float scaled_y(float value)
{
    return ui_layout_snap_y(&render_layout, value);
}

static unsigned int active_visible_width(void)
{
    return render_layout.active_width;
}

static unsigned int font_variant_for_layout(void)
{
    return render_layout.scale_y >= 1.5f ? GS_UI_FONT_SCALED
                                         : GS_UI_FONT_NATIVE;
}

static void describe_font_raster(ui_font_id_t font, unsigned int variant,
                                 ui_font_raster_t *raster)
{
    if (font == UI_FONT_SPLEEN) {
        if (variant == GS_UI_FONT_SCALED) {
            raster->width = 8u;
            raster->height = 16u;
            raster->bytes_per_glyph = 16u;
            raster->data = spleen_8x16_ascii;
        } else {
            raster->width = 5u;
            raster->height = 8u;
            raster->bytes_per_glyph = 8u;
            raster->data = spleen_5x8_ascii;
        }
        raster->first = SPLEEN_ASCII_FIRST;
        raster->count = SPLEEN_ASCII_COUNT;
    } else {
        raster->width = 8u;
        raster->height = 8u;
        raster->first = 0u;
        raster->count = 128u;
        raster->bytes_per_glyph = 8u;
        raster->data = msx;
    }
}

static void select_font_variant(void)
{
    unsigned int variant = font_variant_for_layout();

    describe_font_raster(active_font, variant, &font_raster);
    font_texture.address = font_texture_addresses[variant];
}

static int apply_render_spec(const video_mode_spec_t *spec)
{
    const video_mode_geometry_t *geometry = video_mode_geometry(spec->id);
    int result = ui_layout_configure(
        &render_layout, geometry->surface_width, geometry->surface_height,
        geometry->frame_width, geometry->frame_height,
        geometry->viewport_x, geometry->viewport_y,
        geometry->viewport_width, geometry->viewport_height);

    if (result < 0)
        return result;
    render_filtered = spec->filtered_presentation;
    select_font_variant();
    return 0;
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
    unsigned int cell_x;
    unsigned int cell_y;
    unsigned int cell_width;
    unsigned int cell_height;
    unsigned int output_width;
    unsigned int output_height;
    unsigned int output_x;
    unsigned int output_y;

    if (ch >= 128u)
        ch = '?';
    glyph_x = ((unsigned int)ch & 15u) * GS_UI_FONT_SLOT_W;
    glyph_y = ((unsigned int)ch >> 4) * GS_UI_FONT_SLOT_H;

    ui_layout_text_cell(&render_layout, x, y, &cell_x, &cell_y,
                        &cell_width, &cell_height);
    output_width = font_raster.width;
    output_height = font_raster.height;
    /* The viewport is snapped cell by cell, so its scaled height need not be
       an exact multiple of the source bitmap. Following the snapped cell
       keeps text and panels at the same apparent scale in calibrated HDTV
       modes; nearest GS sampling remains deterministic for 16 -> 19/25 rows. */
    if (cell_height >= output_height)
        output_height = cell_height;
    if (output_width > cell_width)
        output_width = cell_width;
    if (output_height > cell_height)
        output_height = cell_height;
    output_x = cell_x + (cell_width - output_width) / 2u;
    output_y = cell_y + (cell_height - output_height) / 2u;

    glyph.v0.x = (float)output_x;
    glyph.v0.y = (float)output_y;
    glyph.v0.z = 2;
    glyph.v1.x = (float)(output_x + output_width);
    glyph.v1.y = (float)(output_y + output_height);
    glyph.v1.z = 2;
    glyph.t0.u = (float)glyph_x;
    glyph.t0.v = (float)glyph_y;
    glyph.t1.u = (float)(glyph_x + font_raster.width);
    glyph.t1.v = (float)(glyph_y + font_raster.height);
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

static void build_font_atlas(const ui_font_raster_t *raster)
{
    unsigned int ch;

    memset(font_atlas, 0, sizeof(font_atlas));
    for (ch = 0; ch < 128u; ch++) {
        unsigned int source_ch = ch;
        unsigned int gx = (ch & 15u) * GS_UI_FONT_SLOT_W;
        unsigned int gy = (ch >> 4) * GS_UI_FONT_SLOT_H;
        const u8 *source;
        unsigned int row;

        if (source_ch < raster->first ||
            source_ch >= raster->first + raster->count)
            source_ch = '?';
        source = raster->data +
                 (source_ch - raster->first) * raster->bytes_per_glyph;

        for (row = 0; row < raster->height; row++) {
            unsigned char bits = source[row];
            unsigned int col;

            for (col = 0; col < raster->width; col++) {
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
    /* The two page-aligned 640x1080x16 allocations form one contiguous
       32-bit reservation. Modes may use it as two smaller buffers or as one
       complete 32-bit HDTV surface. */
    const unsigned int reserved_words =
        GS_UI_ALT_STORAGE_WIDTH * GS_UI_ALT_STORAGE_HEIGHT;
    unsigned int i;

    for (i = 0u; i < VIDEO_MODE_COUNT; i++) {
        const video_mode_spec_t *spec = &video_specs[i];
        const video_mode_geometry_t *geometry =
            video_mode_geometry((video_mode_id_t)i);
        ui_layout_t candidate;
        unsigned int words;

        if (spec->id != (video_mode_id_t)i || geometry == NULL ||
            geometry->frame_count == 0u ||
            geometry->frame_count > GS_UI_FRAME_COUNT ||
            (geometry->frame_width & 63u) != 0u ||
            ui_layout_configure(&candidate,
                                geometry->surface_width,
                                geometry->surface_height,
                                geometry->frame_width,
                                geometry->frame_height,
                                geometry->viewport_x,
                                geometry->viewport_y,
                                geometry->viewport_width,
                                geometry->viewport_height) < 0)
            return 0;
        if (i == 0u)
            continue;
        if (spec->psm == GS_PSM_32)
            words = geometry->frame_width * geometry->frame_height;
        else if (spec->psm == GS_PSM_16 &&
                 (geometry->frame_height & 1u) == 0u)
            words = geometry->frame_width *
                    (geometry->frame_height >> 1);
        else
            return 0;
        if (words > reserved_words / geometry->frame_count)
            return 0;
    }
    return 1;
}

static void configure_alternate_frames(const video_mode_spec_t *spec)
{
    const video_mode_geometry_t *geometry = video_mode_geometry(spec->id);
    unsigned int i;

    /* Each mode supplies its stride, height and format without moving either
       base address. In single-buffer modes frame zero may span across the
       unused frame-one base; active_frame_count ensures it is never selected. */
    for (i = 0; i < GS_UI_FRAME_COUNT; i++) {
        alternate_frames[i].width = geometry->frame_width;
        alternate_frames[i].height = geometry->frame_height;
        alternate_frames[i].psm = spec->psm;
        alternate_frames[i].mask = 0;
    }
}

static int wait_gif_idle_bounded(unsigned int timeout_ms)
{
    u64 start = GetTimerSystemTime();
    u64 timeout = MSec2TimerBusClock(timeout_ms);

    while ((GS_UI_GIF_CHCR & 0x100u) != 0u) {
        if (GetTimerSystemTime() - start >= timeout)
            return -1;
    }
    return 0;
}

static int wait_finish_bounded(unsigned int timeout_ms)
{
    u64 start = GetTimerSystemTime();
    u64 timeout = MSec2TimerBusClock(timeout_ms);

    while ((*GS_REG_CSR & 2u) == 0u) {
        if (GetTimerSystemTime() - start >= timeout)
            return -1;
    }
    *GS_REG_CSR = 2u;
    return 0;
}

static void reset_gif_path(void)
{
    /* Stop only PATH3/GIF. libdebug's init_scr() resets every DMA channel and
       eventually damages unrelated live SIF/IOP state after repeated video
       tests. A mode transaction owns only the channel it actually uses. */
    GS_UI_GIF_CHCR = 0u;
    GIF_REG_CTRL = GIF_SET_CTRL(1, 0);
    __asm__ __volatile__("sync.l; sync.p");
    GIF_REG_CTRL = GIF_SET_CTRL(0, 0);
    *DMA_REG_STAT = DMA_SET_STAT(1u << DMA_CHANNEL_GIF, 0, 0, 0, 0, 0, 0);
    (void)dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
}

static int submit_normal_and_wait(packet_t *packet, qword_t *q)
{
    int result;

    if (wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        return -1;
    *GS_REG_CSR = 2u;
    __asm__ __volatile__("sync.l; sync.p");
    result = dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                                     (int)(q - packet->data), 0, 0);
    __asm__ __volatile__("sync.l; sync.p");
    if (result < 0 ||
        wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0 ||
        wait_finish_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        return -1;
    return 0;
}

static int clear_frames(framebuffer_t pair[GS_UI_FRAME_COUNT],
                        unsigned int frame_count)
{
    qword_t *q;
    unsigned int required_qwords;
    unsigned int i;

    if (frame_clear_packet == NULL || frame_count == 0u ||
        frame_count > GS_UI_FRAME_COUNT)
        return -1;
    required_qwords = gs_ui_clear_packet_required_qwords(
        pair[0].width, frame_count);
    if (required_qwords == 0u ||
        required_qwords > (unsigned int)frame_clear_packet->qwords)
        return -2;

    packet_reset(frame_clear_packet);
    q = frame_clear_packet->data;
    for (i = 0; i < frame_count; i++) {
        if (pair[i].width != pair[0].width)
            return -3;
        q = draw_setup_environment(q, GS_UI_CONTEXT, &pair[i], &zbuffer);
        q = draw_clear(q, GS_UI_CONTEXT, 0, 0,
                       pair[i].width, pair[i].height, 0, 0, 0);
    }
    q = draw_finish(q);
    if ((unsigned int)(q - frame_clear_packet->data) >
            (unsigned int)frame_clear_packet->qwords)
        return -4;
    if (submit_normal_and_wait(frame_clear_packet, q) < 0)
        return -5;
    return 0;
}

static int setup_environment(void)
{
    packet_t *packet;
    qword_t *q;
    unsigned int i;

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    /* Native frame zero remains at VRAM 0 for startup libdebug compatibility.
       The alternate reservation is physically two 640x1080x16-bit regions.
       It can instead hold two 768x448x32 480p frames, one full 32-bit 576p or
       720p surface, or two correct 640x540x32 1080i FRAME buffers. Two small
       fixed atlases follow it and leave about 144 KiB of VRAM unallocated. */
    graph_vram_clear();
    if (!video_specs_fit_reserved_vram() ||
        allocate_frame_pair(native_frames, GS_UI_WIDTH, GS_UI_HEIGHT,
                            GS_PSM_32, 1) < 0 ||
        allocate_frame_pair(alternate_frames, GS_UI_ALT_STORAGE_WIDTH,
                            GS_UI_ALT_STORAGE_HEIGHT,
                            GS_UI_ALT_STORAGE_PSM, 0) < 0)
        return -1;

    for (i = 0u; i < GS_UI_FONT_VARIANT_COUNT; i++) {
        int texture_address = graph_vram_allocate(
            GS_UI_ATLAS_W, GS_UI_ATLAS_H, GS_PSM_32, GRAPH_ALIGN_BLOCK);

        if (texture_address < 0)
            return -2;
        font_texture_addresses[i] = (unsigned int)texture_address;
    }

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
    if (apply_render_spec(&video_specs[VIDEO_MODE_NATIVE]) < 0) {
        packet_free(packet);
        return -4;
    }
    q = draw_setup_environment(q, GS_UI_CONTEXT,
                               &active_frames[draw_frame_index], &zbuffer);
    /* libdraw draw2d primitives add the GS +2048 bias themselves. */
    q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f, 2048.0f);
    q = draw_scissor_area(q, GS_UI_CONTEXT, 0, GS_UI_WIDTH - 1,
                          0, GS_UI_HEIGHT - 1);
    q = draw_finish(q);
    if (submit_normal_and_wait(packet, q) < 0) {
        packet_free(packet);
        return -5;
    }
    packet_free(packet);
    return 0;
}

static int upload_font_variant(unsigned int variant)
{
    ui_font_raster_t raster;
    qword_t *q;
    int result;

    if (variant >= GS_UI_FONT_VARIANT_COUNT || font_upload_packet == NULL)
        return -1;
    describe_font_raster(active_font, variant, &raster);
    build_font_atlas(&raster);
    q = font_upload_packet->data;
    q = draw_texture_transfer(q, font_atlas, GS_UI_ATLAS_W, GS_UI_ATLAS_H,
                              GS_PSM_32, font_texture_addresses[variant],
                              GS_UI_ATLAS_W);
    q = draw_texture_flush(q);
    if (wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        return -2;
    result = dma_channel_send_chain(DMA_CHANNEL_GIF,
                                    font_upload_packet->data,
                                    (int)(q - font_upload_packet->data),
                                    0, 0);
    if (result < 0 ||
        wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        return -3;
    /* DMA completion only proves that PATH3 consumed the chain. Queue FINISH
       behind TEXFLUSH and wait for the GS as well before the shared atlas is
       rebuilt for the second variant. */
    q = font_upload_packet->data;
    q = draw_finish(q);
    if (submit_normal_and_wait(font_upload_packet, q) < 0)
        return -4;
    return 0;
}

static int upload_font_textures(void)
{
    unsigned int variant;

    for (variant = 0u; variant < GS_UI_FONT_VARIANT_COUNT; variant++) {
        if (upload_font_variant(variant) < 0)
            return -1;
    }
    select_font_variant();
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
    if (submit_normal_and_wait(packet, q) < 0) {
        packet_free(packet);
        return -2;
    }
    packet_free(packet);
    return 0;
}

static int wait_vsync_bounded(unsigned int timeout_ms)
{
    u64 start = GetTimerSystemTime();
    u64 timeout = MSec2TimerBusClock(timeout_ms);

    *GS_REG_CSR = 8u;
    while ((*GS_REG_CSR & 8u) == 0u) {
        if (GetTimerSystemTime() - start >= timeout)
            return -1;
    }
    return 0;
}

static int rom_version_number(void)
{
    char romname[16];

    memset(romname, 0, sizeof(romname));
    GetRomName(romname);
    return (int)strtol(romname, NULL, 10);
}

static int setup_legacy_576p(void)
{
    u64 gcont = ((u64)GetGsVParam() & 1u) << 25;

    /* Older retail ROMs do not implement SetGsCrt(0x53); PS2SDK silently
       substitutes PAL. The DVE parameters for 576p are identical to 480p, so
       use the kernel's 480p setup and change only the established GS timing.
       This deliberately avoids raw DVE/DEV9 bus access while HDD is active.
       Timing values are adapted from Open PS2 Loader GSM under AFL-2.0; see
       THIRD_PARTY_NOTICES.md. */
    if (graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_480P,
                       GRAPH_MODE_FRAME, GRAPH_DISABLE) < 0)
        return -1;

    *GS_REG_SMODE1 = 0x00000017404B0504ULL | gcont;
    *GS_REG_SYNCH1 = 0x000402E02003C827ULL;
    *GS_REG_SYNCH2 = 0x000000000019CA67ULL;
    *GS_REG_SYNCHV = 0x00A9000002700005ULL;
    *GS_REG_SMODE2 = 0u;
    *GS_REG_SRFSH = 4u;
    *GS_REG_SMODE1 = 0x0000001740490504ULL | gcont;
    __asm__ __volatile__("sync.l; sync.p");
    return 0;
}

static void program_explicit_display(const video_mode_spec_t *spec)
{
    int dx = spec->display_x;
    int dy = spec->display_y;
    u64 display;

    if (GetSyscallHandler(__NR__GetGsDxDyOffset) != NULL) {
        int offset_x;
        int offset_y;
        int ignored_width;
        int ignored_height;

        _GetGsDxDyOffset(spec->crt_mode, &offset_x, &offset_y,
                         &ignored_width, &ignored_height);
        dx += offset_x;
        dy += offset_y;
    }
    if (spec->id == VIDEO_MODE_1080I)
        *GS_REG_SMODE2 = GS_SET_SMODE2(1, 1, 0);
    display = GS_SET_DISPLAY(
        dx, dy, spec->magh, spec->magv,
        spec->display_width - 1u, spec->display_height - 1u);
    /* GS privileged display registers are write-only from the EE's point of
       view. Reading DISPLAY1 back does not reproduce the value just written;
       real hardware and GS dumps instead expose undefined bus data. Since
       PMODE selects read circuit 2 for these modes, such a read-back copy can
       leave DISPLAY2 with a zero-sized window and an otherwise valid signal
       will remain black. Write the locally assembled value to both circuits. */
    *GS_REG_DISPLAY1 = display;
    *GS_REG_DISPLAY2 = display;
}

static int restore_native_video(int hard_recovery);

static qword_t *begin_frame(packet_t **packet_out)
{
    packet_t *packet;
    qword_t *q;

    if (wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        frame_fault_pending = 1;
    packet = render_packet;
    q = packet->data;

    if (draw_state_dirty) {
        q = draw_setup_environment(q, GS_UI_CONTEXT,
                                   &active_frames[draw_frame_index], &zbuffer);
        q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f, 2048.0f);
        q = draw_scissor_area(q, GS_UI_CONTEXT, 0,
                              active_visible_width() - 1,
                              0, render_layout.active_height - 1);
        q = draw_texture_sampling(q, GS_UI_CONTEXT, &font_lod);
        q = draw_texturebuffer(q, GS_UI_CONTEXT, &font_texture, &no_clut);
        q = draw_alpha_blending(q, GS_UI_CONTEXT, &alpha_blend);
        draw_state_dirty = 0;
    } else {
        q = draw_framebuffer(q, GS_UI_CONTEXT,
                             &active_frames[draw_frame_index]);
    }
    /* The complete output is cleared before the logical viewport is drawn.
       Letterboxed layouts therefore have deterministic black bars instead of
       pixels left behind by an earlier frame. */
    q = draw_clear(q, GS_UI_CONTEXT, 0, 0,
                   active_visible_width(), render_layout.active_height,
                   0, 0, 0);

    *packet_out = packet;
    return q;
}

static void end_frame(packet_t *packet, qword_t *q)
{
    unsigned int completed_frame = draw_frame_index;

    if (frame_fault_pending) {
        frame_fault_pending = 0;
        (void)restore_native_video(1);
        console_dirty = 0;
        return;
    }
    /* A single full-resolution 32-bit surface deliberately trades flipping
       for color correctness and VRAM safety. Start its draw immediately after
       VBlank so the GS follows the scanout beam instead of racing it. */
    if (active_frame_count == 1u &&
        wait_vsync_bounded(GS_UI_VSYNC_TIMEOUT_MS) < 0) {
        (void)restore_native_video(1);
        console_dirty = 0;
        return;
    }
    q = draw_finish(q);
    if (submit_normal_and_wait(packet, q) < 0) {
        (void)restore_native_video(1);
        console_dirty = 0;
        return;
    }
    if (active_frame_count > 1u &&
        wait_vsync_bounded(GS_UI_VSYNC_TIMEOUT_MS) < 0) {
        (void)restore_native_video(1);
        console_dirty = 0;
        return;
    }
    /* 1080i FRAME buffers contain both fields. Keep each completed buffer on
       the read circuit for two VBlanks so odd and even fields are never taken
       from different UI frames. */
    if (video_mode == VIDEO_MODE_1080I &&
        wait_vsync_bounded(GS_UI_VSYNC_TIMEOUT_MS) < 0) {
        (void)restore_native_video(1);
        console_dirty = 0;
        return;
    }
    present_framebuffer(&active_frames[completed_frame]);
    draw_frame_index = (draw_frame_index + 1u) % active_frame_count;
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
    font_upload_packet = packet_init(GS_UI_FONT_UPLOAD_QWORDS,
                                     PACKET_NORMAL);
    if (font_upload_packet == NULL)
        return -2;
    if (upload_font_textures() < 0)
        return -3;
    if (setup_texture_state() < 0)
        return -4;
    native_bootstrap_packet = packet_init(64, PACKET_NORMAL);
    if (native_bootstrap_packet == NULL)
        return -5;
    frame_clear_packet = packet_init(GS_UI_CLEAR_PACKET_QWORDS,
                                     PACKET_NORMAL);
    if (frame_clear_packet == NULL)
        return -6;

    /* end_frame() waits for GS FINISH before returning, so a second 256 KiB
       packet cannot overlap useful work. Reuse one packet and leave that EE
       memory available to the forensic workspace. */
    render_packet = packet_init(GS_UI_PACKET_QWORDS, PACKET_NORMAL);
    if (render_packet == NULL)
        return -7;

    console_buffer[0] = '\0';
    console_used = 0;
    console_dirty = 0;
    draw_state_dirty = 0;
    blending_enabled = -1;
    frame_fault_pending = 0;
    video_transition_state = VIDEO_TRANSITION_STABLE;
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

ui_font_id_t gs_ui_font_current(void)
{
    return active_font;
}

int gs_ui_font_apply(ui_font_id_t font)
{
    ui_font_id_t previous;

    if ((unsigned int)font >= UI_FONT_COUNT)
        return -1;
    if (font == active_font)
        return 0;

    previous = active_font;
    active_font = font;
    select_font_variant();
    if (renderer_ready && upload_font_textures() < 0) {
        active_font = previous;
        select_font_variant();
        (void)upload_font_textures();
        return -2;
    }
    blending_enabled = -1;
    draw_state_dirty = 1;
    return 0;
}

static int bootstrap_native_gs(void)
{
    qword_t *q;

    if (native_bootstrap_packet == NULL)
        return -1;

    /* This is init_scr()'s hardware-proven GS bootstrap without its DmaReset(),
       which resets unrelated SIF/IOP channels. Reset the GS itself, ask the
       kernel for the console's native timing, then reproduce libdebug's exact
       read circuit before output is enabled. */
    graph_disable_output();
    *GS_REG_CSR = 0x200u;
    GsPutIMR(0xff00u);
    SetGsCrt(GRAPH_MODE_INTERLACED, graph_get_region(), GRAPH_MODE_FIELD);
    __asm__ __volatile__("sync.l; sync.p");

    active_frames = native_frames;
    active_frame_count = GS_UI_FRAME_COUNT;
    if (apply_render_spec(&video_specs[VIDEO_MODE_NATIVE]) < 0)
        return -2;
    draw_frame_index = 1u;

    *GS_REG_DISPFB1 = 0u;
    *GS_REG_DISPFB2 = GS_UI_NATIVE_DISPFB2;
    *GS_REG_DISPLAY1 = 0u;
    *GS_REG_DISPLAY2 = GS_UI_NATIVE_DISPLAY2;
    *GS_REG_BGCOLOR = 0u;

    /* GS reset clears every general drawing register. Rebuild the complete
       libdraw context while PMODE is still disabled, targeting the hidden
       native back buffer; frame zero remains the immediately visible image. */
    q = native_bootstrap_packet->data;
    q = draw_setup_environment(q, GS_UI_CONTEXT,
                               &native_frames[draw_frame_index], &zbuffer);
    q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f, 2048.0f);
    q = draw_scissor_area(q, GS_UI_CONTEXT, 0, GS_UI_WIDTH - 1,
                          0, GS_UI_HEIGHT - 1);
    q = draw_texture_sampling(q, GS_UI_CONTEXT, &font_lod);
    q = draw_texturebuffer(q, GS_UI_CONTEXT, &font_texture, &no_clut);
    q = draw_alpha_blending(q, GS_UI_CONTEXT, &alpha_blend);
    q = draw_finish(q);
    if (submit_normal_and_wait(native_bootstrap_packet, q) < 0)
        return -3;

    *GS_REG_PMODE = GS_UI_NATIVE_PMODE;
    __asm__ __volatile__("sync.l; sync.p");
    return 0;
}

static int restore_native_video(int hard_recovery)
{
    int result;

    video_transition_state = VIDEO_TRANSITION_RESTORING;
    if (hard_recovery ||
        wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        reset_gif_path();

    result = bootstrap_native_gs();
    if (result < 0 && !hard_recovery) {
        /* One controlled PATH3 recovery is allowed if the otherwise healthy
           transaction could not submit its post-reset environment packet. */
        reset_gif_path();
        result = bootstrap_native_gs();
    }
    if (result < 0)
        goto fail;

    video_mode = VIDEO_MODE_NATIVE;
    blending_enabled = -1;
    draw_state_dirty = 0;
    frame_fault_pending = 0;
    video_transition_state = VIDEO_TRANSITION_STABLE;
    return 0;

fail:
    video_mode = VIDEO_MODE_NATIVE;
    active_frames = native_frames;
    active_frame_count = GS_UI_FRAME_COUNT;
    draw_frame_index = 1u;
    blending_enabled = -1;
    draw_state_dirty = 1;
    frame_fault_pending = 0;
    video_transition_state = VIDEO_TRANSITION_STABLE;
    return -2;
}

static int fail_video_switch(int error)
{
    (void)restore_native_video(1);
    return error;
}

int gs_ui_video_mode_apply(video_mode_id_t mode)
{
    const video_mode_spec_t *spec;
    const video_mode_geometry_t *geometry;
    int mode_result;

    if (!renderer_ready)
        return -1;
    if ((unsigned int)mode >= VIDEO_MODE_COUNT)
        return -2;
    if (!gs_ui_video_mode_supported(mode))
        return -3;
    if (mode == video_mode)
        return 0;
    if (video_transition_state != VIDEO_TRANSITION_STABLE)
        return -9;

    video_transition_state = VIDEO_TRANSITION_SWITCHING;
    if (wait_gif_idle_bounded(GS_UI_GIF_TIMEOUT_MS) < 0)
        return fail_video_switch(-10);
    if (wait_vsync_bounded(GS_UI_VSYNC_TIMEOUT_MS) < 0)
        return fail_video_switch(-11);
    if (mode == VIDEO_MODE_NATIVE)
        return restore_native_video(0);

    spec = &video_specs[(unsigned int)mode];
    geometry = video_mode_geometry(mode);
    graph_disable_output();
    if (mode == VIDEO_MODE_576P && rom_version_number() < 220)
        mode_result = setup_legacy_576p();
    else
        mode_result = graph_set_mode(spec->interlace, spec->graph_mode,
                                     spec->frame_mode,
                                     spec->flicker_filter);
    if (mode_result < 0) {
        return fail_video_switch(-4);
    }
    if (spec->explicit_display) {
        program_explicit_display(spec);
    } else if (graph_set_screen(spec->screen_x, spec->screen_y,
                                geometry->surface_width,
                                geometry->surface_height) < 0) {
        return fail_video_switch(-4);
    }

    configure_alternate_frames(spec);
    if (clear_frames(alternate_frames, geometry->frame_count) < 0)
        return fail_video_switch(-5);

    active_frames = alternate_frames;
    active_frame_count = geometry->frame_count;
    if (apply_render_spec(spec) < 0)
        return fail_video_switch(-6);
    video_mode = mode;
    draw_frame_index = 0u;
    graph_set_bgcolor(0, 0, 0);
    present_framebuffer(&alternate_frames[
        active_frame_count > 1u ? 1u : 0u]);
    blending_enabled = -1;
    draw_state_dirty = 1;
    frame_fault_pending = 0;
    graph_enable_output();
    if (wait_vsync_bounded(GS_UI_VSYNC_TIMEOUT_MS) < 0) {
        return fail_video_switch(-8);
    }
    video_transition_state = VIDEO_TRANSITION_STABLE;
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
