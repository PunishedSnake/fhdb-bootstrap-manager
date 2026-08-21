#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_REPORT_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_REPORT_PS2_H

/* Persist an already-rendered BOOTCHAIN.TXT image on one storage target. */
int boot_report_save(unsigned int storage, const char *report,
                     unsigned int length);

#endif
