#include <assert.h>
#include <stdio.h>

#include "ui_layout.h"

static void test_native(void)
{
    ui_layout_t layout;
    unsigned int x, y, width, height;

    assert(ui_layout_configure(&layout, 640, 224, 640, 224,
                               0, 0, 640, 224) == 0);
    assert(ui_layout_snap_x(&layout, 640.0f) == 640.0f);
    assert(ui_layout_snap_y(&layout, 224.0f) == 224.0f);
    ui_layout_text_cell(&layout, 16.0f, 8.0f, &x, &y, &width, &height);
    assert(x == 16u && y == 8u && width == 8u && height == 8u);
}

static void test_480p_fractional_horizontal_scale(void)
{
    ui_layout_t layout;
    unsigned int x, y, width, height;

    assert(ui_layout_configure(&layout, 720, 448, 768, 448,
                               0, 0, 720, 448) == 0);
    assert(ui_layout_snap_x(&layout, 8.0f) == 9.0f);
    assert(ui_layout_snap_y(&layout, 8.0f) == 16.0f);
    ui_layout_text_cell(&layout, 8.0f, 8.0f, &x, &y, &width, &height);
    assert(x == 9u && y == 16u && width == 9u && height == 16u);
}

static void test_576p_letterbox(void)
{
    ui_layout_t layout;

    assert(ui_layout_configure(&layout, 720, 576, 768, 576,
                               40, 64, 640, 448) == 0);
    assert(ui_layout_snap_x(&layout, 0.0f) == 40.0f);
    assert(ui_layout_snap_x(&layout, 640.0f) == 680.0f);
    assert(ui_layout_snap_y(&layout, 0.0f) == 64.0f);
    assert(ui_layout_snap_y(&layout, 224.0f) == 512.0f);
}

static void test_720p_magnified_surface(void)
{
    ui_layout_t layout;

    assert(ui_layout_configure(&layout, 640, 720, 640, 720,
                               0, 136, 640, 448) == 0);
    assert(ui_layout_snap_x(&layout, 640.0f) == 640.0f);
    assert(ui_layout_snap_y(&layout, 0.0f) == 136.0f);
    assert(ui_layout_snap_y(&layout, 224.0f) == 584.0f);
}

static void test_1080i_full_height_frame(void)
{
    ui_layout_t layout;
    unsigned int x, y, width, height;

    assert(ui_layout_configure(&layout, 640, 1080, 640, 1080,
                               0, 92, 640, 896) == 0);
    assert(ui_layout_snap_y(&layout, 0.0f) == 92.0f);
    assert(ui_layout_snap_y(&layout, 224.0f) == 988.0f);
    ui_layout_text_cell(&layout, 8.0f, 8.0f, &x, &y, &width, &height);
    assert(x == 8u && y == 124u && width == 8u && height == 32u);
}

static void test_invalid_geometry(void)
{
    ui_layout_t layout;

    assert(ui_layout_configure(NULL, 640, 224, 640, 224,
                               0, 0, 640, 224) < 0);
    assert(ui_layout_configure(&layout, 720, 448, 640, 448,
                               0, 0, 640, 448) < 0);
    assert(ui_layout_configure(&layout, 720, 448, 768, 448,
                               100, 0, 640, 448) < 0);
}

int main(void)
{
    test_native();
    test_480p_fractional_horizontal_scale();
    test_576p_letterbox();
    test_720p_magnified_surface();
    test_1080i_full_height_frame();
    test_invalid_geometry();
    puts("All resolution-independent UI layout tests passed.");
    return 0;
}
