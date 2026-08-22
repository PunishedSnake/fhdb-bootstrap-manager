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

static void test_centered_high_resolution_viewport(void)
{
    ui_layout_t layout;

    assert(ui_layout_configure(&layout, 1280, 720, 1280, 720,
                               320, 136, 640, 448) == 0);
    assert(ui_layout_snap_x(&layout, 0.0f) == 320.0f);
    assert(ui_layout_snap_x(&layout, 640.0f) == 960.0f);
    assert(ui_layout_snap_y(&layout, 0.0f) == 136.0f);
    assert(ui_layout_snap_y(&layout, 224.0f) == 584.0f);
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
    test_centered_high_resolution_viewport();
    test_invalid_geometry();
    puts("All resolution-independent UI layout tests passed.");
    return 0;
}
