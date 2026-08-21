/*
 * Application-wide 2D frontend rendered through the PlayStation 2 GS.
 *
 * Michishirube no longer shares libdebug's framebuffer or coordinate state.
 * This module owns CRT/framebuffer setup through PS2SDK graph, keeps one ASCII
 * atlas resident in VRAM, and renders menus, messages, compatibility console
 * screens and live HDD telemetry as GS primitives/textured sprites over GIF DMA.
 */

#include <kernel.h>
#include <tamtypes.h>

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
#include "version.h"

#define GS_UI_WIDTH 640
#define GS_UI_HEIGHT 448
#define GS_UI_FONT_SRC_W 8
#define GS_UI_FONT_SRC_H 8
#define GS_UI_GLYPH_W 8
#define GS_UI_GLYPH_H 12
#define GS_UI_ATLAS_W 128
#define GS_UI_ATLAS_H 64
#define GS_UI_PACKET_QWORDS 16384
#define GS_UI_CONTEXT 0
#define GS_UI_CONSOLE_BYTES 8192u
#define GS_UI_MAX_MENU_ITEMS 12u

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
static char console_buffer[GS_UI_CONSOLE_BYTES];
static unsigned int console_used;
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
    glyph_x = ((unsigned int)ch & 15u) * GS_UI_FONT_SRC_W;
    glyph_y = ((unsigned int)ch >> 4) * GS_UI_FONT_SRC_H;

    glyph.v0.x = x;
    glyph.v0.y = y;
    glyph.v0.z = 2;
    glyph.v1.x = x + GS_UI_GLYPH_W;
    glyph.v1.y = y + GS_UI_GLYPH_H;
    glyph.v1.z = 2;
    glyph.t0.u = (float)glyph_x;
    glyph.t0.v = (float)glyph_y;
    glyph.t1.u = (float)(glyph_x + GS_UI_FONT_SRC_W);
    glyph.t1.v = (float)(glyph_y + GS_UI_FONT_SRC_H);
    set_color(&glyph.color, r, g, b, 0x80);

    draw_enable_blending();
    return draw_rect_textured(q, GS_UI_CONTEXT, &glyph);
}

static qword_t *text_string_box(qword_t *q, float x, float y,
                                float max_x, float max_y,
                                const char *text,
                                unsigned int r, unsigned int g,
                                unsigned int b)
{
    float cursor_x = x;
    float cursor_y = y;

    if (text == NULL)
        return q;
    while (*text != '\0' && cursor_y + GS_UI_GLYPH_H <= max_y) {
        unsigned char ch = (unsigned char)*text++;

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += GS_UI_GLYPH_H + 2;
            continue;
        }
        if (cursor_x + GS_UI_GLYPH_W > max_x) {
            cursor_x = x;
            cursor_y += GS_UI_GLYPH_H + 2;
            if (cursor_y + GS_UI_GLYPH_H > max_y)
                break;
        }
        q = text_char(q, cursor_x, cursor_y, ch, r, g, b);
        cursor_x += GS_UI_GLYPH_W;
    }
    return q;
}

static qword_t *text_string(qword_t *q, float x, float y, const char *text,
                            unsigned int r, unsigned int g, unsigned int b)
{
    return text_string_box(q, x, y, GS_UI_WIDTH - 18.0f,
                           GS_UI_HEIGHT - 18.0f, text, r, g, b);
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

static int setup_environment(void)
{
    packet_t *packet;
    qword_t *q;
    int frame_address;
    int texture_address;

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    graph_vram_clear();
    frame_address = graph_vram_allocate(GS_UI_WIDTH, GS_UI_HEIGHT,
                                        GS_PSM_32, GRAPH_ALIGN_PAGE);
    if (frame_address < 0)
        return -1;

    texture_address = graph_vram_allocate(GS_UI_ATLAS_W, GS_UI_ATLAS_H,
                                          GS_PSM_32, GRAPH_ALIGN_BLOCK);
    if (texture_address < 0)
        return -2;

    frame.address = (unsigned int)frame_address;
    frame.width = GS_UI_WIDTH;
    frame.height = GS_UI_HEIGHT;
    frame.psm = GS_PSM_32;
    frame.mask = 0;

    font_texture.address = (unsigned int)texture_address;

    zbuffer.enable = DRAW_DISABLE;
    zbuffer.method = ZTEST_METHOD_ALLPASS;
    zbuffer.address = 0;
    zbuffer.zsm = GS_ZBUF_32;
    zbuffer.mask = 1;

    /* graph_initialize owns SetGsCrt/read-circuit setup. From this point onward
       there is one application framebuffer and one full 640x448 coordinate
       space. draw2d already adds the GS +2048 primitive bias internally. */
    graph_initialize(frame.address, frame.width, frame.height, frame.psm, 0, 0);

    packet = packet_init(64, PACKET_NORMAL);
    if (packet == NULL)
        return -3;
    q = packet->data;
    q = draw_setup_environment(q, GS_UI_CONTEXT, &frame, &zbuffer);
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
    packet = render_packets[render_packet_index];
    q = packet->data;

    q = draw_framebuffer(q, GS_UI_CONTEXT, &frame);
    /* libdraw's draw2d primitives add +2048 themselves. The correct GS offset
       for ordinary application coordinates is therefore exactly 2048,2048. */
    q = draw_primitive_xyoffset(q, GS_UI_CONTEXT, 2048.0f, 2048.0f);
    q = draw_scissor_area(q, GS_UI_CONTEXT, 0, GS_UI_WIDTH - 1,
                          0, GS_UI_HEIGHT - 1);
    q = draw_texture_sampling(q, GS_UI_CONTEXT, &font_lod);
    q = draw_texturebuffer(q, GS_UI_CONTEXT, &font_texture, &no_clut);
    q = draw_alpha_blending(q, GS_UI_CONTEXT, &alpha_blend);

    *packet_out = packet;
    return q;
}

static void end_frame(packet_t *packet, qword_t *q)
{
    q = draw_finish(q);
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data,
                            (int)(q - packet->data), 0, 0);
    render_packet_index ^= 1u;
}

static void tone_rgb(gs_ui_tone_t tone,
                     unsigned int *r, unsigned int *g, unsigned int *b)
{
    switch (tone) {
        case GS_UI_TONE_SUCCESS:
            *r = 74u; *g = 190u; *b = 132u;
            break;
        case GS_UI_TONE_WARNING:
            *r = 224u; *g = 170u; *b = 72u;
            break;
        case GS_UI_TONE_DANGER:
            *r = 226u; *g = 92u; *b = 92u;
            break;
        case GS_UI_TONE_INFO:
        default:
            *r = 74u; *g = 184u; *b = 224u;
            break;
    }
}

static qword_t *draw_shell(qword_t *q, const char *section,
                           gs_ui_tone_t tone)
{
    unsigned int r, g, b;

    tone_rgb(tone, &r, &g, &b);
    q = filled_rect(q, 0, 0, GS_UI_WIDTH, GS_UI_HEIGHT, 8, 11, 18);
    q = filled_rect(q, 0, 0, GS_UI_WIDTH, 7, r, g, b);
    q = filled_rect(q, 18, 18, 622, 58, 16, 22, 32);
    q = outline_rect(q, 18, 18, 622, 58, 44, 58, 76);
    q = text_string(q, 30, 29, APP_NAME "  v" APP_VERSION,
                    230, 235, 242);
    if (section != NULL && section[0] != '\0')
        q = text_string_box(q, 360, 29, 610, 52, section, r, g, b);
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

    render_packets[0] = packet_init(GS_UI_PACKET_QWORDS, PACKET_NORMAL);
    render_packets[1] = packet_init(GS_UI_PACKET_QWORDS, PACKET_NORMAL);
    if (render_packets[0] == NULL || render_packets[1] == NULL)
        return -4;

    console_buffer[0] = '\0';
    console_used = 0;
    render_packet_index = 0;
    renderer_ready = 1;
    gs_ui_render_message("Starting", "Graphics Synthesizer frontend ready.",
                         NULL, GS_UI_TONE_INFO);
    return 0;
}

int gs_ui_is_ready(void)
{
    return renderer_ready;
}

void gs_ui_render_menu(const char *title,
                       const char *status,
                       const char *const *labels,
                       const char *const *hints,
                       const unsigned char *enabled,
                       unsigned int item_count,
                       unsigned int selected)
{
    packet_t *packet;
    qword_t *q;
    unsigned int i;
    float content_y;
    float row_height;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    if (item_count > GS_UI_MAX_MENU_ITEMS)
        item_count = GS_UI_MAX_MENU_ITEMS;

    q = begin_frame(&packet);
    q = draw_shell(q, "MANAGER", GS_UI_TONE_INFO);
    q = text_string_box(q, 28, 72, 612, 94,
                        title != NULL ? title : "Menu",
                        236, 240, 246);

    if (status != NULL && status[0] != '\0') {
        q = filled_rect(q, 24, 100, 616, 137, 13, 19, 29);
        q = outline_rect(q, 24, 100, 616, 137, 37, 52, 70);
        q = text_string_box(q, 34, 111, 606, 131, status,
                            146, 164, 187);
        content_y = 149.0f;
    } else {
        content_y = 106.0f;
    }

    row_height = item_count <= 5u ? 52.0f : 38.0f;
    for (i = 0; i < item_count; i++) {
        float y0 = content_y + (float)i * row_height;
        float y1 = y0 + row_height - 5.0f;
        int is_enabled = enabled == NULL || enabled[i] != 0u;
        int is_selected = i == selected;

        if (y1 > 407.0f)
            break;
        if (is_selected) {
            q = filled_rect(q, 24, y0, 616, y1, 22, 34, 49);
            q = filled_rect(q, 24, y0, 30, y1,
                            is_enabled ? 74u : 95u,
                            is_enabled ? 184u : 103u,
                            is_enabled ? 224u : 112u);
            q = outline_rect(q, 24, y0, 616, y1, 54, 76, 98);
        } else {
            q = filled_rect(q, 28, y0, 612, y1, 12, 17, 25);
        }

        q = text_string_box(q, 43, y0 + 8.0f, 600, y0 + 23.0f,
                            labels != NULL && labels[i] != NULL
                                ? labels[i] : "(unnamed)",
                            is_enabled ? 231u : 112u,
                            is_enabled ? 236u : 120u,
                            is_enabled ? 243u : 132u);
        if (hints != NULL && hints[i] != NULL && row_height >= 48.0f) {
            q = text_string_box(q, 43, y0 + 27.0f, 600, y1 - 3.0f,
                                hints[i],
                                is_enabled ? 139u : 92u,
                                is_enabled ? 158u : 100u,
                                is_enabled ? 182u : 110u);
        }
    }

    q = filled_rect(q, 0, 414, GS_UI_WIDTH, GS_UI_HEIGHT, 11, 15, 23);
    q = text_string_box(q, 28, 425, 612, 444,
                        "UP/DOWN  Select        X  Open        TRIANGLE  Back",
                        139, 158, 182);
    end_frame(packet, q);
}

void gs_ui_render_message(const char *title,
                          const char *body,
                          const char *footer,
                          gs_ui_tone_t tone)
{
    packet_t *packet;
    qword_t *q;
    unsigned int r, g, b;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    tone_rgb(tone, &r, &g, &b);

    q = begin_frame(&packet);
    q = draw_shell(q, "STATUS", tone);
    q = text_string_box(q, 30, 76, 610, 99,
                        title != NULL ? title : "Information",
                        r, g, b);
    q = filled_rect(q, 24, 108, 616, 397, 13, 18, 27);
    q = outline_rect(q, 24, 108, 616, 397, 38, 52, 69);
    q = text_string_box(q, 38, 124, 602, 382,
                        body != NULL ? body : "",
                        222, 229, 238);
    if (footer != NULL && footer[0] != '\0')
        q = text_string_box(q, 28, 420, 612, 444, footer,
                            139, 158, 182);
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
        if (strncmp(title, APP_NAME, strlen(APP_NAME)) == 0)
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
    if (renderer_ready)
        render_console_buffer();
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
    unsigned int accent_r = write_sensitive ? 226u : 74u;
    unsigned int accent_g = write_sensitive ? 124u : 184u;
    unsigned int accent_b = write_sensitive ? 72u : 224u;

    if (!renderer_ready && gs_ui_initialize() < 0)
        return;
    if (percent > 100u)
        percent = 100u;

    q = begin_frame(&packet);
    q = draw_shell(q, "LIVE HDD", write_sensitive
                                  ? GS_UI_TONE_WARNING : GS_UI_TONE_INFO);

    q = filled_rect(q, 24, 76, 616, 185, 13, 18, 27);
    q = outline_rect(q, 24, 76, 616, 185, 38, 52, 69);
    q = text_string(q, 38, 92, "OPERATION", 126, 145, 170);
    q = text_string_box(q, 142, 92, 600, 108,
                        operation != NULL ? operation : "HDD activity",
                        232, 237, 244);
    q = text_string(q, 38, 124, "ACTION", 126, 145, 170);
    q = text_string_box(q, 142, 124, 600, 140,
                        phase != NULL && phase[0] != '\0'
                            ? phase : io_kind,
                        232, 237, 244);
    q = text_string(q, 38, 156, "I/O", 126, 145, 170);
    q = text_string_box(q, 142, 156, 600, 172,
                        io_kind != NULL ? io_kind : "HDD I/O",
                        accent_r, accent_g, accent_b);

    q = filled_rect(q, 24, 203, 616, 257, 13, 18, 27);
    q = outline_rect(q, 24, 203, 616, 257, 38, 52, 69);
    q = filled_rect(q, 38, 220, 548, 238, 31, 40, 54);
    progress_width = 510.0f * ((float)percent / 100.0f);
    if (progress_width > 0.0f)
        q = filled_rect(q, 38, 220, 38.0f + progress_width, 238,
                        accent_r, accent_g, accent_b);
    snprintf(line, sizeof(line), "%3u%%", percent);
    q = text_string(q, 561, 222, line, 236, 240, 246);

    q = filled_rect(q, 24, 275, 616, 389, 13, 18, 27);
    q = outline_rect(q, 24, 275, 616, 389, 38, 52, 69);
    if (progress_total != 0u) {
        snprintf(line, sizeof(line),
                 "POSITION  0x%08x / 0x%08x sectors",
                 progress_current, progress_total);
    } else {
        snprintf(line, sizeof(line), "POSITION  disk size unavailable");
    }
    q = text_string_box(q, 38, 293, 600, 312, line, 193, 205, 221);

    if (sectors != 0u) {
        if (sectors > 1u)
            snprintf(line, sizeof(line),
                     "SECTOR    0x%08x .. 0x%08x   (%u sectors)",
                     lba, lba + sectors - 1u, sectors);
        else
            snprintf(line, sizeof(line),
                     "SECTOR    0x%08x   (1 sector)", lba);
        q = text_string_box(q, 38, 326, 600, 345, line, 230, 236, 244);
    } else {
        q = text_string_box(q, 38, 326, 600, 345,
                            "SECTOR    waiting for next HDD command",
                            153, 171, 194);
    }

    q = text_string_box(q, 38, 359, 600, 379,
                        write_sensitive
                            ? "WRITE PATH ACTIVE - do not reset or remove power"
                            : "Read-only activity - every published event is rendered",
                        write_sensitive ? 244u : 151u,
                        write_sensitive ? 174u : 198u,
                        write_sensitive ? 92u : 214u);

    q = filled_rect(q, 0, 414, GS_UI_WIDTH, GS_UI_HEIGHT, 11, 15, 23);
    q = text_string_box(q, 28, 425, 612, 444,
                        "GS / GIF DMA frontend - full-screen application renderer",
                        126, 145, 170);
    end_frame(packet, q);
}
