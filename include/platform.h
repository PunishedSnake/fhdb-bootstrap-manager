#ifndef PS2_HDD_BOOTSTRAP_MANAGER_PLATFORM_H
#define PS2_HDD_BOOTSTRAP_MANAGER_PLATFORM_H

#include <tamtypes.h>

/*
 * Thin EE/IOP lifecycle boundary.
 *
 * Controller activity indication deliberately lives here because the DualShock
 * ANALOG lamp is not an independent LED: pad command 0x44 changes both the
 * main input mode and the lamp. The platform layer serializes those mode
 * transitions and exposes only begin/end semantics to higher layers.
 */
void reset_iop(void);
int load_modules(void);
int init_pad(void);
u32 wait_for_press(void);
int wait_for_press_timeout(unsigned int milliseconds, u32 *pressed);
int wait_for_chord(u32 chord);

/*
 * On a compatible DualShock/DualShock 2, idle is locked digital mode (lamp off)
 * and active is locked analog mode (lamp on). Button reads remain available in
 * both modes; the application does not use pressure/stick data. Unsupported
 * controllers silently fall back to screen-only activity indication.
 */
void pad_activity_begin(void);
void pad_activity_end(void);
void pad_activity_restore(void);
int pad_activity_is_supported(void);

#endif
