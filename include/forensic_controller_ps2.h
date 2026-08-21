#ifndef PS2_HDD_BOOTSTRAP_MANAGER_FORENSIC_CONTROLLER_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_FORENSIC_CONTROLLER_PS2_H

/* Open the raw read-only APA forensic workspace. A successful topology repair
 * returns 1 so the caller can restart and force ps2hdd to discard stale state. */
int forensic_controller_screen(void);

#endif