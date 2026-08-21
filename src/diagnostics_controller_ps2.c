/* PS2-only orchestration for refreshing and presenting boot-chain diagnostics. */

#include <debug.h>

#include "app_identity.h"
#include "app_ui_ps2.h"
#include "boot_diagnostics_ps2.h"
#include "boot_report_session.h"
#include "diagnostics_controller_ps2.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

void diagnostics_controller_refresh(
    const unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain,
    int save_to_storage)
{
    unsigned int start = read_le32(header + APA_OSD_START_OFFSET);
    unsigned int sectors = read_le32(header + APA_OSD_SIZE_OFFSET);

    pad_activity_begin();
    boot_diagnostics_scan(boot_chain, start, sectors);
    boot_report_session_render(boot_chain, start, sectors,
                               APP_NAME, APP_VERSION);
    session_log_line(
        "Boot-chain scan: family='%s', confidence=%s, payload_read=%d, kelf=%d",
        boot_chain->family, boot_chain->confidence,
        boot_chain->payload_read_result, boot_chain->payload_kelf_result);

    if (save_to_storage) {
        int result = boot_report_session_save(storage_selected());

        session_log_line("BOOTCHAIN.TXT save to %s returned %d",
                         storage_targets[storage_selected()].name, result);
        session_log_flush(storage_selected());
    }
    pad_activity_end();
}

void diagnostics_controller_screen(
    const unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain)
{
    char path[64];

    app_ui_activity_message("Boot-chain inspection",
                            "Scanning HDD, memory cards and configuration...");
    diagnostics_controller_refresh(header, boot_chain, 1);
    storage_path(path, sizeof(path), storage_selected(), "BOOTCHAIN.TXT");
    scr_clear();
    scr_printf("Boot-chain inspection complete.\n\n");
    scr_printf("Family    : %s\n", boot_chain->family);
    scr_printf("Confidence: %s\n", boot_chain->confidence);
    scr_printf("Next stage: %s\n", boot_chain->next_stage);
    scr_printf("Report    : %s\n", path);
    scr_printf("Save code : %d\n\n",
               boot_report_session_last_save_result());
    scr_printf("The complete report is separate from HDDMAN.LOG.\n");
    app_ui_wait_to_return();
}