/*
 * Compatibility surface for controller screens that still build text through
 * the historical libdebug API.
 *
 * libdebug is now allowed to provide init_scr() as the hardware-proven CRT /
 * read-circuit bootstrap. The linker wraps its drawing entry points, however,
 * so application text and clears still go exclusively through gs_ui_ps2.
 *
 * This translation unit also supplies the only draw2d primitives used by the
 * frontend. Current PS2SDK keeps arcs, rounded rectangles and the basic sprite
 * helpers in one draw2d.o archive member; referencing one basic primitive then
 * drags sinf/cosf and their libm support into the EE ELF. The narrow subset
 * below preserves current PS2SDK packet semantics for the operations we use so
 * the monolithic archive member can remain unlinked.
 */

#include <debug.h>
#include <draw.h>
#include <draw2d.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <stdarg.h>

#include "gs_ui_ps2.h"

#define GS_UI_DRAW_START_OFFSET 2047.5625f
#define GS_UI_DRAW_END_OFFSET 2048.5625f

#define GS_UI_DRAW_RECT_OUT_NREG 8
#define GS_UI_DRAW_RECT_OUT_REGLIST \
    (((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 | \
     ((u64)GIF_REG_XYZ2) << 8 | ((u64)GIF_REG_XYZ2) << 12 | \
     ((u64)GIF_REG_XYZ2) << 16 | ((u64)GIF_REG_XYZ2) << 20 | \
     ((u64)GIF_REG_XYZ2) << 24 | ((u64)GIF_REG_NOP) << 28)

#define GS_UI_DRAW_SPRITE_NREG 4
#define GS_UI_DRAW_SPRITE_REGLIST \
    (((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 | \
     ((u64)GIF_REG_XYZ2) << 8 | ((u64)GIF_REG_XYZ2) << 12)

#define GS_UI_DRAW_TEX_NREG 6
#define GS_UI_DRAW_TEX_REGLIST \
    (((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 | \
     ((u64)GIF_REG_UV) << 8 | ((u64)GIF_REG_XYZ2) << 12 | \
     ((u64)GIF_REG_UV) << 16 | ((u64)GIF_REG_XYZ2) << 20)

static int gs_ui_draw_blending;

void __wrap_scr_clear(void)
{
    gs_ui_console_clear();
}

void __wrap_scr_vprintf(const char *format, va_list arguments)
{
    gs_ui_console_vprintf(format, arguments);
}

void __wrap_scr_printf(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    gs_ui_console_vprintf(format, arguments);
    va_end(arguments);
}

void draw_enable_blending(void)
{
    gs_ui_draw_blending = 1;
}

void draw_disable_blending(void)
{
    gs_ui_draw_blending = 0;
}

qword_t *draw_rect_outline(qword_t *q, int context, rect_t *rect)
{
    int x0 = ftoi4(rect->v0.x + GS_UI_DRAW_START_OFFSET);
    int y0 = ftoi4(rect->v0.y + GS_UI_DRAW_START_OFFSET);
    int x1 = ftoi4(rect->v1.x + GS_UI_DRAW_END_OFFSET);
    int y1 = ftoi4(rect->v1.y + GS_UI_DRAW_END_OFFSET);

    PACK_GIFTAG(q,
                GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST,
                            GS_UI_DRAW_RECT_OUT_NREG),
                GS_UI_DRAW_RECT_OUT_REGLIST);
    q++;

    q->dw[0] = GIF_SET_PRIM(PRIM_LINE_STRIP, 0, 0, 0, gs_ui_draw_blending,
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
                GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST,
                            GS_UI_DRAW_SPRITE_NREG),
                GS_UI_DRAW_SPRITE_REGLIST);
    q++;

    q->dw[0] = GIF_SET_PRIM(PRIM_SPRITE, 0, 0, 0, gs_ui_draw_blending,
                            0, 0, context, 0);
    q->dw[1] = rect->color.rgbaq;
    q++;

    q->dw[0] = GIF_SET_XYZ(ftoi4(rect->v0.x + GS_UI_DRAW_START_OFFSET),
                           ftoi4(rect->v0.y + GS_UI_DRAW_START_OFFSET),
                           rect->v0.z);
    q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v1.x + GS_UI_DRAW_END_OFFSET),
                           ftoi4(rect->v1.y + GS_UI_DRAW_END_OFFSET),
                           rect->v0.z);
    q++;

    return q;
}

qword_t *draw_rect_textured(qword_t *q, int context, texrect_t *rect)
{
    PACK_GIFTAG(q,
                GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST,
                            GS_UI_DRAW_TEX_NREG),
                GS_UI_DRAW_TEX_REGLIST);
    q++;

    q->dw[0] = GIF_SET_PRIM(PRIM_SPRITE, 0, DRAW_ENABLE, 0,
                            gs_ui_draw_blending, 0, PRIM_MAP_UV, context, 0);
    q->dw[1] = rect->color.rgbaq;
    q++;

    q->dw[0] = GIF_SET_UV(ftoi4(rect->t0.u), ftoi4(rect->t0.v));
    q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v0.x + GS_UI_DRAW_START_OFFSET),
                           ftoi4(rect->v0.y + GS_UI_DRAW_START_OFFSET),
                           rect->v0.z);
    q++;

    q->dw[0] = GIF_SET_UV(ftoi4(rect->t1.u), ftoi4(rect->t1.v));
    q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v1.x + GS_UI_DRAW_END_OFFSET),
                           ftoi4(rect->v1.y + GS_UI_DRAW_END_OFFSET),
                           rect->v0.z);
    q++;

    return q;
}
