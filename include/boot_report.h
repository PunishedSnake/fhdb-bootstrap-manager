#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_REPORT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_REPORT_H

#include "boot_chain.h"

/*
 * The report buffer is intentionally bounded. The current human-readable
 * BOOTCHAIN.TXT format fits comfortably below this size, while keeping a
 * runaway diagnostic path from consuming arbitrary EE memory.
 */
#define BOOT_REPORT_SIZE 16384u

/*
 * Render one complete BOOTCHAIN.TXT image from already-collected evidence.
 *
 * This function is deliberately device-agnostic: it does not read the HDD,
 * mount PFS, save files, or append to HDDMAN.LOG. `start` and `sectors` are
 * passed explicitly so host fixtures exercise the exact same formatting used
 * by the EE application. The result is the number of bytes written excluding
 * the trailing NUL. A non-zero-capacity buffer is always NUL-terminated.
 */
unsigned int boot_report_render(char *buffer, unsigned int capacity,
                                const boot_chain_info_t *info,
                                unsigned int start, unsigned int sectors,
                                const char *application_name,
                                const char *application_version);

#endif
