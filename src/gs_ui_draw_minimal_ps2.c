/*
 * Minimal libdraw-compatible 2D primitive subset for the manager frontend.
 *
 * Current PS2SDK places basic rectangles/textured sprites and the unrelated
 * arc/rounded-rectangle code in one LTO archive member (draw2d.o). Referencing
 * a basic primitive can therefore retain sinf/cosf and their libm support.
 * This file preserves the current PS2SDK packet layout for the draw2d symbols
 * actually used by fhdb-bootstrap-manager and by retained PS2SDK draw.c helpers.
 * It is deliberately compiled as a normal non-LTO object so the monolithic
 * draw2d archive member can be omitted without copying its unrelated code.
 */

#include <draw.h>
#include <draw2d.h>
#include <gif_tags.h>
#include <gs_gp.h>

#define UI_START_OFFSET 2047.5625f
#define UI_END_OFFSET 2048.5625f

#define UI_RECT_OUT_NREG 8
#define UI_RECT_OUT_REGLIST \
    (((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 | \
     ((u64)GIF_REG_XYZ2) << 8 | ((u64)GIF_REG_XYZ2) << 12 | \
     ((u64)GIF_REG_XYZ2) << 16 | ((u64)GIF_REG_XYZ2) << 20 | \
     ((u64)GIF_REG_XYZ2) << 24 | ((u64)GIF_REG_NOP) << 28)

#define UI_SPRITE_NREG 4
#define UI_SPRITE_REGLIST \
    (((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 | \
     ((u64)GIF_REG_XYZ2) << 8 | ((u64)GIF_REG_XYZ2) << 12)

#define UI_TEX_NREG 6
#define UI_TEX_REGLIST \
    (((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 | \
     ((u64)GIF_REG_UV) << 8 | ((u64)GIF_REG_XYZ2) << 12 | \
     ((u64)GIF_REG_UV) << 16 | ((u64)GIF_REG_XYZ2) << 20)

static int ui_blending;

void draw_enable_blending(void)
{
    ui_blending = 1;
}

void draw_disable_blending(void)
{
    ui_blending = 0;
}

qword_t *draw_rect_outline(qword_t *q, int context, rect_t *rect)
{
    int x0 = ftoi4(rect->v0.x + UI_START_OFFSET);
    int y0 = ftoi4(rect->v0.y + UI_START_OFFSET);
    int x1 = ftoi4(rect->v1.x + UI_END_OFFSET);
    int y1 = ftoi4(rect->v1.y + UI_END_OFFSET);

    PACK_GIFTAG(q,
                GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST,
                            UI_RECT_OUT_NREG),
                UI_RECT_OUT_REGLIST);
    q++;
    q->dw[0] = GIF_SET_PRIM(PRIM_LINE_STRIP, 0, 0, 0, ui_blending,
                            0, 0, context, 0);
    q->dw[1] = rect->color.rgbaq;
    q++;
    q->dw[0] = GIF_SET_XYZ(x0, y0, rect->v0.z);
    q->dw[1] = GIF_SET_XYZ(x1, y0, rect->v0.z);
    q++;
    q->dw[0] = GIF_SET_XYZ(x1, y1, rect->v0.z);
    q->dw[1] = GIF_SET_XYZ(x0, y1, rect->v0.z);
    q++;
    q->dw[0] = GIF_SET_XYZ(x0, y0, rect->v0.z);
    q->dw[1] = 0;
    q++;
    return q;
}

qword_t *draw_rect_filled(qword_t *q, int context, rect_t *rect)
{
    PACK_GIFTAG(q,
                GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST, UI_SPRITE_NREG),
                UI_SPRITE_REGLIST);
    q++;
    q->dw[0] = GIF_SET_PRIM(PRIM_SPRITE, 0, 0, 0, ui_blending,
                            0, 0, context, 0);
    q->dw[1] = rect->color.rgbaq;
    q++;
    q->dw[0] = GIF_SET_XYZ(ftoi4(rect->v0.x + UI_START_OFFSET),
                           ftoi4(rect->v0.y + UI_START_OFFSET), rect->v0.z);
    q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v1.x + UI_END_OFFSET),
                           ftoi4(rect->v1.y + UI_END_OFFSET), rect->v0.z);
    q++;
    return q;
}

/* draw_clear() in current PS2SDK draw.c uses the strip variant internally.
 * Keep its exact 32-pixel strip stepping and packet layout, otherwise trimming
 * draw2d.o would change framebuffer-clear semantics even though our own UI never
 * calls this entry point directly. */
qword_t *draw_rect_filled_strips(qword_t *q, int context, rect_t *rect)
{
    qword_t *giftag;
    int x0 = ftoi4(rect->v0.x);
    int y0 = ftoi4(rect->v0.y + UI_START_OFFSET);
    int x1 = ftoi4(rect->v1.x);
    int y1 = ftoi4(rect->v1.y + UI_END_OFFSET);

    PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    q++;
    PACK_GIFTAG(q,
                GIF_SET_PRIM(PRIM_SPRITE, 0, 0, 0, ui_blending,
                             0, 0, context, 0),
                GIF_REG_PRIM);
    q++;
    PACK_GIFTAG(q, rect->color.rgbaq, GIF_REG_RGBAQ);
    q++;

    giftag = q;
    q++;

    while (x0 < x1) {
        q->dw[0] = GIF_SET_XYZ(x0 + ftoi4(UI_START_OFFSET), y0, rect->v0.z);
        x0 += 496;
        if (x0 >= x1)
            x0 = x1;
        q->dw[1] = GIF_SET_XYZ(x0 + ftoi4(UI_END_OFFSET), y1, rect->v0.z);
        x0 += 16;
        q++;
    }

    PACK_GIFTAG(giftag,
                GIF_SET_TAG(q - giftag - 1, 0, 0, 0, GIF_FLG_REGLIST, 2),
                DRAW_XYZ_REGLIST);
    return q;
}

qword_t *draw_rect_textured(qword_t *q, int context, texrect_t *rect)
{
    PACK_GIFTAG(q,
                GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST, UI_TEX_NREG),
                UI_TEX_REGLIST);
    q++;
    q->dw[0] = GIF_SET_PRIM(PRIM_SPRITE, 0, DRAW_ENABLE, 0, ui_blending,
                            0, PRIM_MAP_UV, context, 0);
    q->dw[1] = rect->color.rgbaq;
    q++;
    q->dw[0] = GIF_SET_UV(ftoi4(rect->t0.u), ftoi4(rect->t0.v));
    q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v0.x + UI_START_OFFSET),
                           ftoi4(rect->v0.y + UI_START_OFFSET), rect->v0.z);
    q++;
    q->dw[0] = GIF_SET_UV(ftoi4(rect->t1.u), ftoi4(rect->t1.v));
    q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v1.x + UI_END_OFFSET),
                           ftoi4(rect->v1.y + UI_END_OFFSET), rect->v0.z);
    q++;
    return q;
}
