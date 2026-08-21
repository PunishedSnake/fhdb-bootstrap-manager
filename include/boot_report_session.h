#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_REPORT_SESSION_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_REPORT_SESSION_H

#include "boot_chain.h"

unsigned int boot_report_session_render(const boot_chain_info_t *info,
                                        unsigned int start,
                                        unsigned int sectors,
                                        const char *application_name,
                                        const char *application_version);
int boot_report_session_save(unsigned int storage);
int boot_report_session_last_save_result(void);

#endif
