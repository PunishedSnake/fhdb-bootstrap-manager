/* Session-owned state connecting portable report rendering to PS2 persistence. */

#include "boot_report.h"
#include "boot_report_ps2.h"
#include "boot_report_session.h"

#define REPORT_SAVE_NOT_TRIED 999999

static char report_buffer[BOOT_REPORT_SIZE];
static unsigned int report_length;
static int last_save_result = REPORT_SAVE_NOT_TRIED;

unsigned int boot_report_session_render(const boot_chain_info_t *info,
                                        unsigned int start,
                                        unsigned int sectors,
                                        const char *application_name,
                                        const char *application_version)
{
    report_length = boot_report_render(
        report_buffer, sizeof(report_buffer), info, start, sectors,
        application_name, application_version);
    return report_length;
}

int boot_report_session_save(unsigned int storage)
{
    int result;

    if (report_length == 0)
        return -1;
    result = boot_report_save(storage, report_buffer, report_length);
    last_save_result = result;
    return result;
}

int boot_report_session_last_save_result(void)
{
    return last_save_result;
}
