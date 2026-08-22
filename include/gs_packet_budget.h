#ifndef PS2_HDD_BOOTSTRAP_MANAGER_GS_PACKET_BUDGET_H
#define PS2_HDD_BOOTSTRAP_MANAGER_GS_PACKET_BUDGET_H

/* PS2SDK v2.0.0 qword costs used by the mode-transition clear packet:
 *
 * draw_setup_environment(): 16
 * draw_clear():              9 + ceil(width / 32)
 * draw_finish():             2
 *
 * Keep the packet deliberately larger than today's maximum (100 qwords for
 * two 768-pixel-wide 480p buffers). The runtime still rejects geometry whose
 * calculated cost does not fit before any GIF packet is written.
 */
#define GS_UI_CLEAR_PACKET_QWORDS 256u

static inline unsigned int gs_ui_clear_packet_required_qwords(
    unsigned int frame_width, unsigned int frame_count)
{
    const unsigned int environment_qwords = 16u;
    const unsigned int clear_fixed_qwords = 9u;
    const unsigned int finish_qwords = 2u;
    unsigned int strips;

    if (frame_width == 0u || frame_count == 0u)
        return 0u;
    strips = frame_width / 32u + ((frame_width & 31u) != 0u);
    return frame_count *
               (environment_qwords + clear_fixed_qwords + strips) +
           finish_qwords;
}

#endif
