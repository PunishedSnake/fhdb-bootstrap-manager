#ifndef PS2_HDD_BOOTSTRAP_MANAGER_UI_LAYOUT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_UI_LAYOUT_H

#define UI_LOGICAL_WIDTH 640u
#define UI_LOGICAL_HEIGHT 224u
#define UI_LOGICAL_CELL_WIDTH 8u
#define UI_LOGICAL_CELL_HEIGHT 8u

typedef struct {
    unsigned int active_width;
    unsigned int active_height;
    unsigned int frame_width;
    unsigned int frame_height;
    unsigned int viewport_x;
    unsigned int viewport_y;
    unsigned int viewport_width;
    unsigned int viewport_height;
    float scale_x;
    float scale_y;
} ui_layout_t;

/* Signal geometry, backing storage and the UI viewport are deliberately
 * independent. This prevents a letterboxed UI band from being mistaken for
 * the complete GS output surface. */
int ui_layout_configure(ui_layout_t *layout,
                        unsigned int active_width,
                        unsigned int active_height,
                        unsigned int frame_width,
                        unsigned int frame_height,
                        unsigned int viewport_x,
                        unsigned int viewport_y,
                        unsigned int viewport_width,
                        unsigned int viewport_height);

float ui_layout_map_x(const ui_layout_t *layout, float logical_x);
float ui_layout_map_y(const ui_layout_t *layout, float logical_y);
float ui_layout_snap_x(const ui_layout_t *layout, float logical_x);
float ui_layout_snap_y(const ui_layout_t *layout, float logical_y);

/* Returns a pixel-snapped output cell. Mapping both edges independently keeps
 * fractional horizontal scales from accumulating drift across a text row. */
void ui_layout_text_cell(const ui_layout_t *layout,
                         float logical_x,
                         float logical_y,
                         unsigned int *x,
                         unsigned int *y,
                         unsigned int *width,
                         unsigned int *height);

#endif
