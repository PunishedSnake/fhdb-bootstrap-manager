#ifndef PS2_HDD_BOOTSTRAP_MANAGER_PLATFORM_H
#define PS2_HDD_BOOTSTRAP_MANAGER_PLATFORM_H

#include <tamtypes.h>

/*
 * Thin EE/IOP lifecycle boundary.
 *
 * These functions deliberately keep their pre-0.4 names during the first
 * modularization pass so moving code between translation units does not also
 * become an API rewrite. Behavioural cleanup comes after regression coverage.
 */
void reset_iop(void);
int load_modules(void);
int init_pad(void);
u32 wait_for_press(void);
int wait_for_chord(u32 chord);

#endif
