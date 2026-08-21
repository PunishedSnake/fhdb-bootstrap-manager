/* PS2-only persistence for the otherwise portable BOOTCHAIN.TXT image. */

#include <delaythread.h>

#include "boot_report_ps2.h"
#include "storage.h"

#define BOOT_REPORT_PATH_SIZE 64u
#define USB_WRITE_ATTEMPTS 20
#define USB_WRITE_RETRY_DELAY_US 250000

int boot_report_save(unsigned int storage, const char *report,
                     unsigned int length)
{
    char path[BOOT_REPORT_PATH_SIZE];
    int attempts;
    int result;

    if (storage >= STORAGE_TARGET_COUNT || report == NULL || length == 0)
        return -1;
    storage_path(path, sizeof(path), storage, "BOOTCHAIN.TXT");
    attempts = storage == 2 ? USB_WRITE_ATTEMPTS : 1;
    do {
        result = write_whole_file(path, report, (int)length);
        if (result >= 0)
            break;
        DelayThread(USB_WRITE_RETRY_DELAY_US);
    } while (--attempts > 0);
    return result;
}
