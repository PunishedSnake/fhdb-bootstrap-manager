/*
 * High-level PS2 bootstrap workflows.
 *
 * This controller owns user-facing authorization/error flow for backup,
 * disable, restore, and install. Format parsing, storage mechanics, signing,
 * raw transport, and transaction ordering remain delegated to narrow modules.
 */

#include <debug.h>
#include <libpad.h>

#include <stdlib.h>

#include "app_identity.h"
#include "app_ui_ps2.h"
#include "bootstrap_controller_ps2.h"
#include "bootstrap_signing.h"
#include "bootstrap_source.h"
#include "bootstrap_transaction_ps2.h"
#include "diagnostics_controller_ps2.h"
#include "hdd_limits.h"
#include "hdd_read.h"
#include "header_backup.h"
#include "platform.h"
#include "rescue_storage.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

#define MBR_PAYLOAD_START HDD_MBR_PAYLOAD_START

static header_backup_diagnostics_t backup_diagnostics;

static const char *save_backup(unsigned char header[APA_HEADER_SIZE])
{
    return header_backup_save(storage_selected(), header, &backup_diagnostics);
}

static const char *save_rescue_capsule(
    unsigned char header[APA_HEADER_SIZE],
    const boot_chain_info_t *boot_chain)
{
    return rescue_storage_save_current(
        storage_selected(), header, boot_chain->romver,
        boot_chain->family, boot_chain->confidence);
}

static const char *load_backup(unsigned char header[APA_HEADER_SIZE],
                               unsigned int *start_out,
                               unsigned int *size_out)
{
    static char found_path[HEADER_BACKUP_PATH_SIZE];

    if (header_backup_find_enabled(
            storage_selected(), header, found_path,
            sizeof(found_path), start_out, size_out) < 0)
        return NULL;
    return found_path;
}

static void backup_error_screen(void)
{
    unsigned int i;

    session_log_line("Mandatory header backup failed; no HDD write was performed");
    for (i = 0; i < HEADER_BACKUP_SLOT_COUNT; i++)
        session_log_line(
            "Backup slot %u path=%s read=%d write=%d verify=%d", i,
            backup_diagnostics.path[i], backup_diagnostics.read_result[i],
            backup_diagnostics.write_result[i],
            backup_diagnostics.verify_result[i]);
    session_log_flush(storage_selected());

    scr_clear();
    scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
    scr_printf("ERROR: Backup failed. HDD was NOT modified.\n\n");
    for (i = 0; i < HEADER_BACKUP_SLOT_COUNT; i++) {
        scr_printf("%s\n", backup_diagnostics.path[i]);
        scr_printf(" read:%d write:%d verify:%d\n",
                   backup_diagnostics.read_result[i],
                   backup_diagnostics.write_result[i],
                   backup_diagnostics.verify_result[i]);
    }
    scr_printf("\n999998 means an existing file was preserved.\n");
    app_ui_wait_to_return();
}

void bootstrap_controller_backup_current(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain)
{
    const char *backup_path = save_backup(header);
    const char *rescue_path = save_rescue_capsule(header, boot_chain);

    session_log_line("Standalone backup: header=%s rescue=%s",
                     backup_path != NULL ? backup_path : "FAILED",
                     rescue_path != NULL ? rescue_path : "FAILED");
    session_log_flush(storage_selected());

    scr_clear();
    scr_printf("Standalone backup result\n\n");
    scr_printf("Header : %s\n", backup_path != NULL ? backup_path : "FAILED");
    scr_printf("Rescue : %s\n\n", rescue_path != NULL ? rescue_path : "FAILED");
    if (rescue_path != NULL) {
        if (read_le32(header + APA_OSD_START_OFFSET) != 0)
            scr_printf("Header and complete active payload were verified.\n");
        else
            scr_printf("Header-only capsule: bootstrap is disabled.\n");
    }
    if (backup_path == NULL)
        scr_printf("Legacy HDDMBR*.BIN backup was not created.\n");
    if (rescue_path == NULL)
        scr_printf("No rescue capsule slot was available or verified.\n");
    scr_printf("\nNo HDD data was modified.\n");
    app_ui_wait_to_return();
}

void bootstrap_controller_disable(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain)
{
    const char *backup_path = save_backup(header);
    const char *rescue_path;
    bootstrap_transaction_result_t transaction;
    int result;

    if (backup_path == NULL) {
        backup_error_screen();
        return;
    }

    /* Keep emergency pointer clearing independent of an optional large rescue. */
    rescue_path = save_rescue_capsule(header, boot_chain);

    scr_clear();
    scr_printf("Backup saved and verified:\n%s\n\n", backup_path);
    scr_printf("Rescue capsule: %s\n\n",
               rescue_path != NULL ? rescue_path : "not available");
    scr_printf("Only the MBR program pointer will be cleared.\n");
    scr_printf("Partitions and games will not be deleted.\n\n");
    scr_printf("Hold L1+R1 and press X to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_CROSS))
        return;

    result = bootstrap_transaction_ps2_set_pointer(
        header, 0, 0, &transaction);
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            app_ui_fatal_screen("HDIOC_SETOSDMBR failed.", result);
        else
            app_ui_fatal_screen(
                "Disable verification failed. Keep the backup.", result);
    }

    session_log_line(
        "Bootstrap pointer disabled and verified; header backup=%s; rescue=%s",
        backup_path, rescue_path != NULL ? rescue_path : "unavailable");
    diagnostics_controller_refresh(header, boot_chain, 1);

    scr_clear();
    scr_printf("Bootstrap disabled and verified.\n\n");
    scr_printf("Backup: %s\n", backup_path);
    app_ui_wait_to_return();
}

static void restore_legacy_pointer(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain)
{
    const char *backup_path;
    const char *safety_backup;
    bootstrap_transaction_result_t transaction;
    unsigned int start;
    unsigned int size;
    int result;

    backup_path = load_backup(header, &start, &size);
    if (backup_path == NULL) {
        scr_clear();
        scr_printf("No valid enabled backup was found on %s.\n",
                   storage_targets[storage_selected()].name);
        scr_printf("Legacy FHDBMBR*.BIN names were also checked.\n");
        app_ui_wait_to_return();
        return;
    }

    result = hdd_validate_payload_bounds(start, size);
    if (result < 0) {
        scr_clear();
        scr_printf("The saved pointer is outside this __mbr area.\n");
        scr_printf("Validation code: %d\n", result);
        session_log_line(
            "Legacy restore rejected out-of-bounds pointer from %s: %d",
            backup_path, result);
        session_log_flush(storage_selected());
        app_ui_wait_to_return();
        return;
    }

    safety_backup = save_backup(header);
    if (safety_backup == NULL) {
        backup_error_screen();
        return;
    }

    scr_clear();
    scr_printf("Restore from %s\n", backup_path);
    scr_printf("osdStart: 0x%08x\n", start);
    scr_printf("osdSize : 0x%08x\n\n", size);
    scr_printf("Safety backup: %s\n\n", safety_backup);
    scr_printf("Only the pointer can be checked in this legacy backup.\n");
    scr_printf("Hold L1+R1 and press SQUARE to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE))
        return;

    result = bootstrap_transaction_ps2_set_pointer(
        header, start, size, &transaction);
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            app_ui_fatal_screen("Bootstrap restore failed.", result);
        else
            app_ui_fatal_screen("Restore verification failed.", result);
    }

    session_log_line(
        "Legacy pointer-only restore completed from %s; safety=%s",
        backup_path, safety_backup);
    diagnostics_controller_refresh(header, boot_chain, 1);

    scr_clear();
    scr_printf("Legacy bootstrap pointer restored and verified.\n");
    scr_printf("The payload sectors were not rewritten.\n");
    app_ui_wait_to_return();
}

void bootstrap_controller_restore(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain)
{
    rescue_storage_entry_t rescue;
    const unsigned char *payload;
    const char *safety_backup;
    bootstrap_transaction_result_t transaction;
    int result;

    result = rescue_storage_find(storage_selected(), header, &rescue);
    if (result == RESCUE_STORAGE_NOT_FOUND ||
        result == RESCUE_STORAGE_HEADER_ONLY) {
        restore_legacy_pointer(header, boot_chain);
        return;
    }
    if (result < 0) {
        scr_clear();
        scr_printf("A rescue capsule exists but is not safe to restore.\n");
        scr_printf("Validation code: %d\n\n", result);
        scr_printf("It was not replaced by a pointer-only restore.\n");
        session_log_line("Rescue capsule load rejected with code %d", result);
        session_log_flush(storage_selected());
        app_ui_wait_to_return();
        return;
    }

    result = hdd_validate_payload_bounds(rescue.info.payload_start,
                                         rescue.info.payload_sectors);
    if (result < 0) {
        rescue_storage_entry_release(&rescue);
        scr_clear();
        scr_printf("Rescue payload does not fit this __mbr area.\n");
        scr_printf("Validation code: %d\n", result);
        app_ui_wait_to_return();
        return;
    }

    safety_backup = save_backup(header);
    if (safety_backup == NULL) {
        rescue_storage_entry_release(&rescue);
        backup_error_screen();
        return;
    }

    payload = rescue_storage_payload(&rescue);
    scr_clear();
    scr_printf("Full rescue restore from\n%s\n\n", rescue.path);
    scr_printf("Family  : %s (%s)\n", rescue.info.family,
               rescue.info.confidence);
    scr_printf("Target  : 0x%08x\n", rescue.info.payload_start);
    scr_printf("Payload : %u bytes / 0x%x sectors\n",
               rescue.info.payload_bytes, rescue.info.payload_sectors);
    scr_printf("Safety  : %s\n\n", safety_backup);
    scr_printf("The payload will be written and compared first.\n");
    scr_printf("Its pointer will be enabled only after verification.\n\n");
    scr_printf("Hold L1+R1 and press SQUARE to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE)) {
        rescue_storage_entry_release(&rescue);
        return;
    }

    scr_clear();
    scr_printf("Restoring and verifying rescue payload...\n");
    result = bootstrap_transaction_ps2_activate(
        header, payload, rescue.info.payload_bytes,
        rescue.info.payload_start, rescue.info.payload_sectors,
        free, rescue.file_data, &transaction);
    rescue.file_data = NULL;
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD)
            app_ui_fatal_screen(
                "Rescue payload verification failed; pointer remains disabled.",
                result);
        else if (transaction.stage ==
                 BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            app_ui_fatal_screen(
                "Rescue payload verified, but pointer restore failed.", result);
        else
            app_ui_fatal_screen(
                "Rescue pointer read-back verification failed.", result);
    }

    session_log_line(
        "Full rescue restored from %s (%u bytes); safety backup=%s",
        rescue.path, rescue.file_size, safety_backup);
    diagnostics_controller_refresh(header, boot_chain, 1);
    scr_clear();
    scr_printf("Payload and pointer restored and verified.\n\n");
    scr_printf("Source: %s\n", rescue.path);
    app_ui_wait_to_return();
}

void bootstrap_controller_install(
    unsigned char header[APA_HEADER_SIZE],
    boot_chain_info_t *boot_chain)
{
    bootstrap_source_t source;
    bootstrap_source_result_t source_result;
    const char *backup_path;
    const char *installed_rescue;
    int signing_port;
    bootstrap_signing_result_t signing_result;
    bootstrap_transaction_result_t transaction;
    int result;

    if (read_le32(header + APA_OSD_START_OFFSET) != 0 ||
        read_le32(header + APA_OSD_SIZE_OFFSET) != 0) {
        scr_clear();
        scr_printf("Disable the current bootstrap before installing.\n");
        app_ui_wait_to_return();
        return;
    }

    bootstrap_source_init(&source, storage_selected());
    scr_clear();
    scr_printf("Loading %s\n", source.path);
    if (storage_selected() == 2)
        scr_printf("Waiting briefly for USB if necessary...\n");

    result = bootstrap_source_prepare(storage_selected(), &source,
                                      &source_result);
    if (result < 0) {
        if (source_result.stage == BOOTSTRAP_SOURCE_STAGE_LOAD) {
            session_log_line("MBR source load failed from %s: %d",
                             source.path, source_result.code);
            session_log_flush(storage_selected());
            scr_clear();
            scr_printf("Could not load %s\nCode: %d\n",
                       source.path, source_result.code);
            app_ui_wait_to_return();
            return;
        }
        if (source_result.stage == BOOTSTRAP_SOURCE_STAGE_KELF) {
            session_log_line("MBR source structural validation failed: %d",
                             source_result.code);
            session_log_flush(storage_selected());
            scr_clear();
            scr_printf("The selected MBR source is not a valid KELF.\n");
            scr_printf("Validation code: %d\n", source_result.code);
            app_ui_wait_to_return();
            return;
        }

        session_log_line(
            "MBR source capacity validation failed: getstat=%d, start=0x%08x, size=0x%08x, sectors=%u",
            source_result.getstat_result, source_result.mbr_start,
            source_result.mbr_size, source_result.payload_sectors);
        session_log_flush(storage_selected());
        scr_clear();
        scr_printf("The __mbr reserved payload area is not valid.\n");
        scr_printf("getstat:%d start:0x%08x size:0x%08x\n",
                   source_result.getstat_result,
                   source_result.mbr_start, source_result.mbr_size);
        app_ui_wait_to_return();
        return;
    }

    backup_path = save_backup(header);
    if (backup_path == NULL) {
        bootstrap_source_release(&source);
        backup_error_screen();
        return;
    }

    signing_port = storage_targets[storage_selected()].memory_card_port;
    if (signing_port < 0)
        signing_port = app_ui_choose_signing_card();
    if (signing_port < 0) {
        bootstrap_source_release(&source);
        return;
    }

    scr_clear();
    scr_printf("Signing MBR through mc%d...\n", signing_port);
    result = bootstrap_signing_sign(
        signing_port, source.payload, source.payload_size, &signing_result);
    if (result < 0) {
        bootstrap_source_release(&source);
        if (signing_result.stage == BOOTSTRAP_SIGNING_STAGE_MAGICGATE) {
            session_log_line("MagicGate signing failed through mc%d", signing_port);
            session_log_flush(storage_selected());
            scr_clear();
            scr_printf("MagicGate signing failed through mc%d.\n", signing_port);
            scr_printf("HDD was NOT modified. Check the PS2 memory card.\n");
            app_ui_wait_to_return();
            return;
        }
        app_ui_fatal_screen(
            "Signed KELF failed structural validation.", signing_result.code);
    }

    scr_clear();
    scr_printf("Ready to install signed HDD bootstrap\n\n");
    scr_printf("Source : %s\n", source.path);
    scr_printf("Backup : %s\n", backup_path);
    scr_printf("Target : sector 0x%08x\n", MBR_PAYLOAD_START);
    scr_printf("Size   : %u bytes / 0x%x sectors\n\n",
               source.payload_size, source.sectors);
    scr_printf("This installs only the MBR bootstrap program.\n");
    scr_printf("It does not create FHDB/HDD-OSD partitions.\n\n");
    scr_printf("Hold L1+R1 and press CIRCLE to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_CIRCLE)) {
        bootstrap_source_release(&source);
        return;
    }

    scr_clear();
    scr_printf("Writing and verifying signed payload...\n");
    result = bootstrap_transaction_ps2_activate(
        header, source.payload, source.payload_size,
        MBR_PAYLOAD_START, source.sectors, free, source.payload,
        &transaction);
    source.payload = NULL;
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD)
            app_ui_fatal_screen(
                "Payload write/read-back failed; pointer remains disabled.",
                result);
        else if (transaction.stage ==
                 BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            app_ui_fatal_screen(
                "Payload verified, but enabling its pointer failed.", result);
        else
            app_ui_fatal_screen("Installed pointer verification failed.", result);
    }

    session_log_line(
        "Bootstrap installed and verified at sector 0x%08x (%u bytes, %u sectors)",
        MBR_PAYLOAD_START, source.payload_size, source.sectors);
    diagnostics_controller_refresh(header, boot_chain, 1);
    installed_rescue = save_rescue_capsule(header, boot_chain);
    session_log_flush(storage_selected());

    scr_clear();
    scr_printf("Bootstrap installed, enabled, and verified.\n\n");
    scr_printf("The payload is signed for this console.\n");
    scr_printf("Rescue: %s\n",
               installed_rescue != NULL ? installed_rescue : "not available");
    scr_printf("Remember: its expected partitions must also exist.\n");
    app_ui_wait_to_return();
}
