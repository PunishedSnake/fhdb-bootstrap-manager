/*
 * High-frequency 2D HUD rendered through the PS2 Graphics Synthesizer.
 *
 * libdebug's scr_printf() is deliberately retained for low-frequency menus and
 * emergency screens, but it uploads an 8x8 bitmap for every character. That is
 * inappropriate for per-sector live telemetry. This module instead keeps one
 * 8x8 ASCII atlas in VRAM and emits ordinary GS sprites in a single GIF DMA
 * packet per status update.
 *
 * The CRT mode and framebuffer are NOT reinitialized here. init_scr() remains
 * the owner of video-mode setup. We target its framebuffer at VRAM address 0
 * and use the same 640-wide field-mode coordinate system.
 */

#include <kernel.h>
#include <tamtypes.h>

#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <gs_psm.h>
#include <packet.h>

#include <stdio.h>
#include <string.h>

#include "app_identity.h"
#include "gs_ui_ps2.h"
#include "version.h"

#define GS_UI_WIDTH 640
#define GS_UI_LOGICAL_HEIGHT 224
#define GS_UI_DEBUG_FB_HEIGHT 448
#define GS_UI_FONT_W 8
#define GS_UI_FONT_H 8
#define GS_UI_ATLAS_W 128
#define GS_UI_ATLAS_H 64
#define GS_UI_PACKET_QWORDS 4096
#define GS_UI_CONTEXT 0

extern const u8 msx[];

static framebuffer_t frame;
static zbuffer_t zbuffer;
static texbuffer_t font_texture;
static clutbuffer_t no_clut;
static lod_t font_lod;
static blend_t alpha_blend;
static packet_t *render_packets[2];
static unsigned int render_packet_index;
static u32 font_atlas[GS_UI_ATLAS_W * GS_UI_ATLAS_H]
    __attribute__((aligned(64)));
static int renderer_ready;

static void set_color(color_t *color, unsigned int r, unsigned int g,
                      unsigned int b, unsigned int a)
{
    color->r = (u8)r;
    color->g = (u8)g;
    color->b = (u8)b;
    color->a = (u8)a;
    color->q = 1.0f;
}

static qword_t *filled_rect(qword_t *q, float x0, float y0,
                            float x1, float y1,
                            unsigned int r, unsigned int g,
                            unsigned int b)
{
    rect_t rect;

    rect.v0.x = x0;
    rect.v0.y = y0;
    rect.v0.z = 1;
    rect.v1.x = x1;
    rect.v1.y = y1;
    rect.v1.z = 1;
    set_color(&rect.color, r, g, b, 0x80);
    draw_disable_blending();
    return draw_rect_filled(q, GS_UI_CONTEXT, &rect);
}

static qword_t *outline_rect(qword_t *q, float x0, float y0,
                             float x1, float y1,
                             unsigned int r, unsigned int g,
                             unsigned int b)
{
    rect_t rect;

    rect.v0.x = x0;
    rect.v0.y = y0;
    rect.v0.z = 1;
    rect.v1.x = x1;
    rect.v1.y = y1;
    rect.v1.z = 1;
    set_color(&rect.color, r, g, b, 0x80);
    draw_disable_blending();
    return draw_rect_outline(q, GS_UI_CONTEXT, &rect);
}

static qword_t *text_char(qword_t *q, float x, float y, unsigned char ch,
                          unsigned int r, unsigned int g, unsigned int b)
{
    texrect_t glyph;
    unsigned int glyph_x;
    unsigned int glyph_y;

    if (ch >= 128u)
        ch = '?';
    glyph_x = ((unsigned int)ch & 15u) * GS_UI_FONT_W;
    glyph_y = ((unsigned int)ch >> 4) * GS_UI_FONT_H;

    glyph.v0.x = x;
    glyph.v0.y = y;
    glyph.v0.z = 2;
    glyph.v1.x = x + GS_UI_FONT_W;
    glyph.v1.y = y + GS_UI_FONT_H;
    glyph.v1.z = 2;
    glyph.t0.u = (float)glyph_x;
    glyph.t0.v = (float)glyph_y;
    glyph.t1.u = (float)(glyph_x + GS_UI_FONT_W);
    glyph.t1.v = (float)(glyph_y + GS_UI_FONT_H);
    set_color(&glyph.color, r, g, b, 0x80);

    draw_enable_blending();
    return draw_rect_textured(q, GS_UI_CONTEXT, &glyph);
}

static qword_t *text_string(qword_t *q, float x, float y, const char *text,
                            unsigned int r, unsigned int g, unsigned int b)
{
    float cursor = x;

    if (text == NULL)
        return q;
    while (*text != '\0' && cursor + GS_UI_FONT_W <= GS_UI_WIDTH - 16) {
        unsigned char ch = (unsigned char)*text++;

        if (ch == '\n') {
            cursor = x;
            y += GS_UI_FONT_H + 2;
            continue;
        }
        q = text_char(q, cursor, y, ch, r, g, b);
        cursor += GS_UI_FONT_W;
    }
    return q;
}

static void build_font_atlas(void)
{
    unsigned int ch;

    memset(font_atlas, 0, sizeof(font_atlas));
    for (ch = 0; ch < 128u; ch++) {
        unsigned int gx = (ch & 15u) * GS_UI_FONT_W;
        unsigned int gy = (ch >> 4) * GS_UI_FONT_H;
        unsigned int row;

        for (row = 0; row < GS_UI_FONT_H; row++) {
            unsigned char bits = msx[ch * GS_UI_FONT_H + row];
            unsigned int col;

            for (col = 0; col < GS_UI_FONT_W; col++) {
                if ((bits & (0x80u >> col)) != 0u)
                    font_atlas[(gy + row) * GS_UI_ATLAS_W + gx + col] =
                        0x80ffffffu;
            }
        }
    }
    FlushCache(0);
}

static int setup_environment(void)
{
    packet_t *packet;
    qword_t *q;
    int reserved_frame;
    int texture_address;

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    graph_vram_clear();
    reserved_frame = graph_vram_allocate(GS_UI_WIDTH, GS_UI_DEBUG_FB_HEIGHT,
                                         GS_PSM_32, GRAPH_ALIGN_PAGE);
    if (reserved_frame != 0)
        return -1;

    texture_address = graph_vram_allocate(GS_UI_ATLAS_W, GS_UI_ATLAS_H,
                                          GS_PSM_32, GRAPH_ALIGN_BLOCK);
    if (texture_address < 0)
        return -2;
    font_texture.address = (unsigned int)texture_address;

    frame.address = 0;
    frame.width = GS_UI_WIDTH;
    frame.height = GS_UI_DEBUG_FB_HEIGHT;
    frame.psm = GS_PSM_32;
    frame.mask = 0;

    zbuffer.enable = DRAW_DISABLE;
    zbuffer.method = ZTEST_METHOD_ALLPASS;
    zbuffer.address = 0;
    zbuffer.zsm = GS_ZBUF_32;
    zbuffer.mask = 1;

    packet = packet_init(64, PACKET_NORMAL);
    if (packet == NULL)
        return -3;
    q = packet->data;
    q = draw_setup_environment(q, GS_UI_CONTEXT, &frame, &zbuffer);
    /* Match libdebug's field-mode primitive origin: 640x224 logical pixels. */
    q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f - 320.0f,
                                2048.0f - 112.0f);
    q = draw_scissor_area(q, GS_UI_CONTEXT, 0, GS_UI_WIDTH - 1,
                          0, GS_UI_LOGICAL_HEIGHT - 1);
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

    render_packets[0] = packet_init(GS_UI_PACKET_QWORDS, PACKET_NORMAL);
    render_packets[1] = packet_init(GS_UI_PACKET_QWORDS, PACKET_NORMAL);
    if (render_packets[0] == NULL || render_packets[1] == NULL)
        return -4;

    render_packet_index = 0;
    renderer_ready = 1;
    return 0;
}

int gs_ui_is_ready(void)
{
    return renderer_ready;
}

void gs_ui_render_disk_status(const char *operation,
                              const char *phase,
                              const char *io_kind,
                              unsigned int percent,
                              unsigned int progress_current,
                              unsigned int progress_total,
                              unsigned int lba,
                              unsigned int sectors,
                              int write_sensitive)
{
    packet_t *packet;
    qword_t *q;
    char line[160];
    float progress_width;
    unsigned int accent_r = write_sensitive ? 224u : 70u;
    unsigned int accent_g = write_sensitive ? 118u : 176u;
    unsigned int accent_b = write_sensitive ? 70u : 220u;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    if (percent > 100u)
        percent = 100u;

    packet = render_packets[render_packet_index];
    q = packet->data;

    /* Reassert the small amount of GS state that debug text/image transfers do
       not own. There is deliberately no VSync wait in this event-driven HUD. */
    q = draw_framebuffer(q, GS_UI_CONTEXT, &frame);
    q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f - 320.0f,
                                2048.0f - 112.0f);
    q = draw_scissor_area(q, GS_UI_CONTEXT, 0, GS_UI_WIDTH - 1,
                          0, GS_UI_LOGICAL_HEIGHT - 1);
    q = draw_texture_sampling(q, GS_UI_CONTEXT, &font_lod);
    q = draw_texturebuffer(q, GS_UI_CONTEXT, &font_texture, &no_clut);
    q = draw_alpha_blending(q, GS_UI_CONTEXT, &alpha_blend);

    q = filled_rect(q, 0, 0, 640, 224, 10, 13, 20);
    q = filled_rect(q, 0, 0, 640, 4, accent_r, accent_g, accent_b);
    q = filled_rect(q, 14, 10, 626, 30, 18, 24, 34);
    q = outline_rect(q, 14, 10, 626, 30, 44, 58, 76);
    q = filled_rect(q, 14, 38, 626, 91, 15, 20, 29);
    q = outline_rect(q, 14, 38, 626, 91, 40, 52, 68);
    q = filled_rect(q, 14, 102, 626, 126, 15, 20, 29);
    q = outline_rect(q, 14, 102, 626, 126, 40, 52, 68);
    q = filled_rect(q, 14, 137, 626, 199, 15, 20, 29);
    q = outline_rect(q, 14, 137, 626, 199, 40, 52, 68);

    q = text_string(q, 24, 16, APP_NAME "  v" APP_VERSION,
                    224, 230, 238);
    q = text_string(q, 470, 16, "LIVE HDD", accent_r, accent_g, accent_b);

    q = text_string(q, 24, 46, "OPERATION", 116, 132, 154);
    q = text_string(q, 120, 46,
                    operation != NULL ? operation : "HDD activity",
                    232, 236, 242);
    q = text_string(q, 24, 62, "ACTION", 116, 132, 154);
    q = text_string(q, 120, 62,
                    phase != NULL && phase[0] != '\0' ? phase : io_kind,
                    232, 236, 242);
    q = text_string(q, 24, 78, "I/O", 116, 132, 154);
    q = text_string(q, 120, 78,
                    io_kind != NULL ? io_kind : "HDD I/O",
                    accent_r, accent_g, accent_b);

    q = filled_rect(q, 24, 109, 548, 119, 31, 39, 52);
    progress_width = 524.0f * ((float)percent / 100.0f);
    if (progress_width > 0.0f)
        q = filled_rect(q, 24, 109, 24.0f + progress_width, 119,
                        accent_r, accent_g, accent_b);
    snprintf(line, sizeof(line), "%3u%%", percent);
    q = text_string(q, 564, 110, line, 236, 239, 244);

    if (progress_total != 0u) {
        snprintf(line, sizeof(line),
                 "POSITION  0x%08x / 0x%08x sectors",
                 progress_current, progress_total);
    } else {
        snprintf(line, sizeof(line),
                 "POSITION  disk size unavailable");
    }
    q = text_string(q, 24, 146, line, 196, 205, 218);

    if (sectors != 0u) {
        if (sectors > 1u)
            snprintf(line, sizeof(line),
                     "SECTOR    0x%08x .. 0x%08x   (%u sectors)",
                     lba, lba + sectors - 1u, sectors);
        else
            snprintf(line, sizeof(line),
                     "SECTOR    0x%08x   (1 sector)", lba);
        q = text_string(q, 24, 164, line, 232, 236, 242);
    } else {
        q = text_string(q, 24, 164, "SECTOR    waiting for next HDD command",
                        164, 176, 194);
    }

    q = text_string(q, 24, 182,
                    write_sensitive
                        ? "WRITE PATH ACTIVE - do not reset or remove power"
                        : "Read-only activity - live position updates every event",
                    write_sensitive ? 244u : 150u,
                    write_sensitive ? 174u : 196u,
                    write_sensitive ? 92u : 210u);

    q = text_string(q, 18, 208,
                    "GS/GIF DMA HUD - no per-character framebuffer uploads, no redraw throttle",
                    105, 120, 140);

    dma_wait_fast();
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                            (int)(q - packet->data), 0, 0);
    render_packet_index ^= 1u;
}
