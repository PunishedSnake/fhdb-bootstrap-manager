/*
 * PS2 HDD Bootstrap Manager
 *
 * Manages the PlayStation 2 HDD bootstrap stored in the APA __mbr partition.
 * The tool can independently back up the current APA master header and active
 * payload, disable or restore the bootstrap, inspect the downstream boot
 * environment, and sign, install, and verify an MBR bootstrap without
 * formatting the disk or manually rewriting sector zero.
 *
 * Every HDD-changing path follows the same deliberately conservative pattern:
 * validate the APA header, create and verify a user-selected backup, require an
 * awkward confirmation chord, perform the smallest possible change, flush the
 * drive, and read the result back before claiming success.
 */

/* EE kernel, RPC, module loading, video, controller, and power services. */
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <debug.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>
#include <libsecr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apa.h"
#include "bootstrap_source.h"
#include "bootstrap_transaction_ps2.h"
#include "boot_chain.h"
#include "boot_diagnostics_ps2.h"
#include "boot_report_session.h"
#include "hdd_limits.h"
#include "hdd_read.h"
#include "header_backup.h"
#include "kelf.h"
#include "rescue_storage.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

/* Application identity; APA layout constants live in apa.h. */
#define APP_NAME "PS2 HDD Bootstrap Manager"

/* The HDD bootstrap payload begins in the reserved area of __mbr at sector 0x2000. */
#define MBR_PAYLOAD_START HDD_MBR_PAYLOAD_START

/* Human-readable diagnostics and logging remain bounded in EE memory. */
#define TEXT_FILE_LIMIT 32768

/* Application-owned buffers retained across higher-level workflows. */
static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));

/* Explicit diagnostics returned by the mandatory header-backup storage gate. */
static header_backup_diagnostics_t backup_diagnostics;

/* Read-only evidence is modeled in boot_chain.h. */
static boot_chain_info_t boot_chain;

/* Forward declaration for diagnostics refresh used by the UI and write workflows. */
static void refresh_boot_chain_report(int save_to_storage);

/* ------------------------------------------------------------------------- */
/* Console lifecycle and UI                                                */
/* ------------------------------------------------------------------------- */

/* Perform a normal PS2 shutdown through the poweroff RPC service. */
static void shutdown_console(void)
{
    session_log_line("Controlled power-off requested");
    session_log_flush(storage_selected());
    poweroffShutdown();
    SleepThread();
}

/* Return to the ROM Browser using the same ExecOSD path used by PS2 HDD tools. */
static void restart_to_browser(void)
{
    static char *browser_args[] = {"BootBrowser", NULL};

    session_log_line("Restart to PS2 Browser requested");
    session_log_flush(storage_selected());
    scr_clear();
    scr_printf("Restarting to the PS2 Browser...\n");
    fileXioDevctl("hdd0:", HDIOC_DEV9OFF, NULL, 0, NULL, 0);
    ExecOSD(1, browser_args);
}
/* Offer both requested exit modes from one reusable screen. */
static void power_menu(void)
{
    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("X        Restart to PS2 Browser\n");
        scr_printf("TRIANGLE Power off\n");
        scr_printf("CIRCLE   Return to manager\n");
        pressed = wait_for_press();
        if (pressed & PAD_CROSS)
            restart_to_browser();
        if (pressed & PAD_TRIANGLE)
            shutdown_console();
        if (pressed & PAD_CIRCLE)
            return;
    }
}

/* Display a fatal diagnostic while allowing a controlled restart or shutdown. */
static void fatal_screen(const char *message, int code)
{
    session_log_line("FATAL: %s (code %d)", message, code);
    session_log_flush(storage_selected());
    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("ERROR: %s\n", message);
        scr_printf("Code: %d\n\n", code);
        scr_printf("X = restart   TRIANGLE = power off\n");
        pressed = wait_for_press();
        if (pressed & PAD_CROSS)
            restart_to_browser();
        if (pressed & PAD_TRIANGLE)
            shutdown_console();
    }
}

/* Pause after non-fatal information so the user can read or photograph it. */
static void wait_to_return(void)
{
    scr_printf("\nPress X to return to the manager.\n");
    while (!(wait_for_press() & PAD_CROSS)) {}
}

/* ------------------------------------------------------------------------- */
/* Configuration parsing and higher-level storage workflows                */
/* ------------------------------------------------------------------------- */

/* Case-insensitive ASCII comparison used by old FMCB-style CNF files. */


/* Find a simple key/value entry while ignoring comments and whitespace. */


/* Parse FMCB's numeric or textual Skip_HDD setting from one configuration. */


/* Select mc0, mc1, or mass without changing anything until X confirms. */
static void choose_storage(void)
{
    unsigned int choice = storage_selected();

    for (;;) {
        u32 pressed;
        unsigned int i;

        scr_clear();
        scr_printf("Select storage device\n\n");
        for (i = 0; i < STORAGE_TARGET_COUNT; i++)
            scr_printf("%s %s\n", i == choice ? ">" : " ",
                       storage_targets[i].name);
        scr_printf("\nUP/DOWN Select   X Confirm\n");
        scr_printf("TRIANGLE Cancel\n");
        pressed = wait_for_press();
        if (pressed & PAD_UP)
            choice = (choice + STORAGE_TARGET_COUNT - 1) % STORAGE_TARGET_COUNT;
        if (pressed & PAD_DOWN)
            choice = (choice + 1) % STORAGE_TARGET_COUNT;
        if (pressed & PAD_CROSS) {
            storage_set_selected(choice);
            session_log_line("Selected storage device: %s",
                     storage_targets[storage_selected()].name);
            boot_report_session_save(storage_selected());
            session_log_flush(storage_selected());
            return;
        }
        if (pressed & PAD_TRIANGLE)
            return;
    }
}

/* USB has no MagicGate hardware, so choose which memory card signs the bootstrap source. */
static int choose_signing_card(void)
{
    unsigned int choice = 0;

    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf("Select MagicGate signing card\n\n");
        scr_printf("%s mc0\n", choice == 0 ? ">" : " ");
        scr_printf("%s mc1\n", choice == 1 ? ">" : " ");
        scr_printf("\nA PS2 memory card must be present.\n");
        scr_printf("UP/DOWN Select   X Confirm\n");
        scr_printf("TRIANGLE Cancel\n");
        pressed = wait_for_press();
        if (pressed & (PAD_UP | PAD_DOWN))
            choice ^= 1;
        if (pressed & PAD_CROSS)
            return (int)choice;
        if (pressed & PAD_TRIANGLE)
            return -1;
    }
}

/* ------------------------------------------------------------------------- */
/* HDD header reads, writes, and verification                                */
/* ------------------------------------------------------------------------- */

/* Read sectors 0 and 1 through the shared read-only transport. */
static int read_header(unsigned char *destination)
{
    return hdd_read_raw_sectors(0, 2, destination);
}

/* ------------------------------------------------------------------------- */
/* Read-only boot-chain inspection                                           */
/* ------------------------------------------------------------------------- */

/* Refresh the read-only evidence and optionally persist both report and log. */
static void refresh_boot_chain_report(int save_to_storage)
{
    u32 start = read_le32(header_buffer + APA_OSD_START_OFFSET);
    u32 sectors = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);

    boot_diagnostics_scan(&boot_chain, start, sectors);
    boot_report_session_render(&boot_chain, start, sectors,
                               APP_NAME, APP_VERSION);
    session_log_line("Boot-chain scan: family='%s', confidence=%s, payload_read=%d, "
             "kelf=%d", boot_chain.family, boot_chain.confidence,
             boot_chain.payload_read_result, boot_chain.payload_kelf_result);
    if (save_to_storage) {
        int result = boot_report_session_save(storage_selected());

        session_log_line("BOOTCHAIN.TXT save to %s returned %d",
                 storage_targets[storage_selected()].name, result);
        session_log_flush(storage_selected());
    }
}

/* Present a concise console summary while the complete evidence stays in text. */
static void diagnostics_screen(void)
{
    char path[64];

    refresh_boot_chain_report(1);
    storage_path(path, sizeof(path), storage_selected(), "BOOTCHAIN.TXT");
    scr_clear();
    scr_printf("Boot-chain inspection complete.\n\n");
    scr_printf("Family    : %s\n", boot_chain.family);
    scr_printf("Confidence: %s\n", boot_chain.confidence);
    scr_printf("Next stage: %s\n", boot_chain.next_stage);
    scr_printf("Report    : %s\n", path);
    scr_printf("Save code : %d\n\n",
               boot_report_session_last_save_result());
    scr_printf("The complete report is separate from HDDMAN.LOG.\n");
    wait_to_return();
}

/* ------------------------------------------------------------------------- */
/* Backup creation and restoration                                           */
/* ------------------------------------------------------------------------- */

/* Run the mandatory non-overwriting header-backup storage gate. */
static const char *save_backup(void)
{
    return header_backup_save(storage_selected(), header_buffer,
                              &backup_diagnostics);
}

/* Save the current versioned rescue state through the isolated lifecycle. */
static const char *save_rescue_capsule(void)
{
    return rescue_storage_save_current(
        storage_selected(), header_buffer, boot_chain.romver,
        boot_chain.family, boot_chain.confidence);
}

/* Resolve a compatible enabled legacy pointer backup on selected storage. */
static const char *load_backup(u32 *start_out, u32 *size_out)
{
    static char found_path[HEADER_BACKUP_PATH_SIZE];

    if (header_backup_find_enabled(
            storage_selected(), header_buffer, found_path,
            sizeof(found_path), start_out, size_out) < 0)
        return NULL;
    return found_path;
}

/* Explain why the safety gate stopped before touching the disk. */
static void backup_error_screen(void)
{
    unsigned int i;

    session_log_line("Mandatory header backup failed; no HDD write was performed");
    for (i = 0; i < HEADER_BACKUP_SLOT_COUNT; i++)
        session_log_line("Backup slot %u path=%s read=%d write=%d verify=%d", i,
                 backup_diagnostics.path[i], backup_diagnostics.read_result[i],
                 backup_diagnostics.write_result[i], backup_diagnostics.verify_result[i]);
    session_log_flush(storage_selected());

    scr_clear();
    scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
    scr_printf("ERROR: Backup failed. HDD was NOT modified.\n\n");
    for (i = 0; i < HEADER_BACKUP_SLOT_COUNT; i++) {
        scr_printf("%s\n", backup_diagnostics.path[i]);
        scr_printf(" read:%d write:%d verify:%d\n",
                   backup_diagnostics.read_result[i], backup_diagnostics.write_result[i],
                   backup_diagnostics.verify_result[i]);
    }
    scr_printf("\n999998 means an existing file was preserved.\n");
    wait_to_return();
}

/* ------------------------------------------------------------------------- */
/* User operations: back up, disable, restore, and install                   */
/* ------------------------------------------------------------------------- */

/* Save the legacy header plus a complete versioned rescue capsule. */
static void backup_current_state(void)
{
    const char *backup_path = save_backup();
    const char *rescue_path;

    rescue_path = save_rescue_capsule();
    session_log_line("Standalone backup: header=%s rescue=%s",
             backup_path != NULL ? backup_path : "FAILED",
             rescue_path != NULL ? rescue_path : "FAILED");
    session_log_flush(storage_selected());

    scr_clear();
    scr_printf("Standalone backup result\n\n");
    scr_printf("Header : %s\n", backup_path != NULL ? backup_path : "FAILED");
    scr_printf("Rescue : %s\n\n", rescue_path != NULL ? rescue_path : "FAILED");
    if (rescue_path != NULL) {
        if (read_le32(header_buffer + APA_OSD_START_OFFSET) != 0)
            scr_printf("Header and complete active payload were verified.\n");
        else
            scr_printf("Header-only capsule: bootstrap is disabled.\n");
    }
    if (backup_path == NULL)
        scr_printf("Legacy HDDMBR*.BIN backup was not created.\n");
    if (rescue_path == NULL)
        scr_printf("No rescue capsule slot was available or verified.\n");
    scr_printf("\n");
    scr_printf("No HDD data was modified.\n");
    wait_to_return();
}

/* Back up the active pointer, clear it, and verify the resulting header. */
static void disable_bootstrap(void)
{
    const char *backup_path = save_backup();
    const char *rescue_path;
    bootstrap_transaction_result_t transaction;
    int result;

    if (backup_path == NULL) {
        backup_error_screen();
        return;
    }

    /* Preserve the complete payload when space permits, but never make an
       emergency pointer clear depend on a multi-megabyte optional copy. */
    rescue_path = save_rescue_capsule();

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
        header_buffer, 0, 0, &transaction);
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            fatal_screen("HDIOC_SETOSDMBR failed.", result);
        else
            fatal_screen("Disable verification failed. Keep the backup.", result);
    }

    session_log_line("Bootstrap pointer disabled and verified; header backup=%s; "
             "rescue=%s", backup_path,
             rescue_path != NULL ? rescue_path : "unavailable");
    refresh_boot_chain_report(1);

    scr_clear();
    scr_printf("Bootstrap disabled and verified.\n\n");
    scr_printf("Backup: %s\n", backup_path);
    wait_to_return();
}

/* Legacy fallback: restore only a non-zero pointer from an old header backup. */
static void restore_legacy_pointer(void)
{
    const char *backup_path;
    const char *safety_backup;
    bootstrap_transaction_result_t transaction;
    u32 start;
    u32 size;
    int result;

    backup_path = load_backup(&start, &size);
    if (backup_path == NULL) {
        scr_clear();
        scr_printf("No valid enabled backup was found on %s.\n",
                   storage_targets[storage_selected()].name);
        scr_printf("Legacy FHDBMBR*.BIN names were also checked.\n");
        wait_to_return();
        return;
    }

    result = hdd_validate_payload_bounds(start, size);
    if (result < 0) {
        scr_clear();
        scr_printf("The saved pointer is outside this __mbr area.\n");
        scr_printf("Validation code: %d\n", result);
        session_log_line("Legacy restore rejected out-of-bounds pointer from %s: %d",
                 backup_path, result);
        session_log_flush(storage_selected());
        wait_to_return();
        return;
    }
    safety_backup = save_backup();
    if (safety_backup == NULL) {
        backup_error_screen();
        return;
    }

    scr_clear();
    scr_printf("Restore from %s\n", backup_path);
    scr_printf("osdStart: 0x%08x\n", (unsigned int)start);
    scr_printf("osdSize : 0x%08x\n\n", (unsigned int)size);
    scr_printf("Safety backup: %s\n\n", safety_backup);
    scr_printf("Only the pointer can be checked in this legacy backup.\n");
    scr_printf("Hold L1+R1 and press SQUARE to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE))
        return;

    result = bootstrap_transaction_ps2_set_pointer(
        header_buffer, start, size, &transaction);
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            fatal_screen("Bootstrap restore failed.", result);
        else
            fatal_screen("Restore verification failed.", result);
    }

    session_log_line("Legacy pointer-only restore completed from %s; safety=%s",
             backup_path, safety_backup);
    refresh_boot_chain_report(1);

    scr_clear();
    scr_printf("Legacy bootstrap pointer restored and verified.\n");
    scr_printf("The payload sectors were not rewritten.\n");
    wait_to_return();
}

/* Restore a verified payload first and expose it to ROM only after read-back. */
static void restore_rescue_capsule(void)
{
    rescue_storage_entry_t rescue;
    const unsigned char *payload;
    const char *safety_backup;
    bootstrap_transaction_result_t transaction;
    int result;

    result = rescue_storage_find(storage_selected(), header_buffer, &rescue);
    if (result == RESCUE_STORAGE_NOT_FOUND ||
        result == RESCUE_STORAGE_HEADER_ONLY) {
        restore_legacy_pointer();
        return;
    }
    if (result < 0) {
        scr_clear();
        scr_printf("A rescue capsule exists but is not safe to restore.\n");
        scr_printf("Validation code: %d\n\n", result);
        scr_printf("It was not replaced by a pointer-only restore.\n");
        session_log_line("Rescue capsule load rejected with code %d", result);
        session_log_flush(storage_selected());
        wait_to_return();
        return;
    }

    result = hdd_validate_payload_bounds(rescue.info.payload_start,
                                         rescue.info.payload_sectors);
    if (result < 0) {
        rescue_storage_entry_release(&rescue);
        scr_clear();
        scr_printf("Rescue payload does not fit this __mbr area.\n");
        scr_printf("Validation code: %d\n", result);
        wait_to_return();
        return;
    }
    safety_backup = save_backup();
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
    scr_printf("Target  : 0x%08x\n",
               (unsigned int)rescue.info.payload_start);
    scr_printf("Payload : %u bytes / 0x%x sectors\n",
               (unsigned int)rescue.info.payload_bytes,
               (unsigned int)rescue.info.payload_sectors);
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
        header_buffer, payload, rescue.info.payload_bytes,
        rescue.info.payload_start, rescue.info.payload_sectors,
        free, rescue.file_data, &transaction);
    rescue.file_data = NULL;
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD)
            fatal_screen(
                "Rescue payload verification failed; pointer remains disabled.",
                result);
        else if (transaction.stage ==
                 BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            fatal_screen(
                "Rescue payload verified, but pointer restore failed.", result);
        else
            fatal_screen("Rescue pointer read-back verification failed.",
                         result);
    }

    session_log_line(
        "Full rescue restored from %s (%u bytes); safety backup=%s",
        rescue.path, rescue.file_size, safety_backup);
    refresh_boot_chain_report(1);
    scr_clear();
    scr_printf("Payload and pointer restored and verified.\n\n");
    scr_printf("Source: %s\n", rescue.path);
    wait_to_return();
}

/* Sign, write, verify, and finally enable a prepared stock MBR payload. */
static void install_bootstrap(void)
{
    bootstrap_source_t source;
    bootstrap_source_result_t source_result;
    const char *backup_path;
    const char *installed_rescue;
    int signing_port;
    bootstrap_transaction_result_t transaction;
    int result;

    if (read_le32(header_buffer + APA_OSD_START_OFFSET) != 0 ||
        read_le32(header_buffer + APA_OSD_SIZE_OFFSET) != 0) {
        scr_clear();
        scr_printf("Disable the current bootstrap before installing.\n");
        wait_to_return();
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
            session_log_line("MBR.XLF load failed from %s: %d",
                             source.path, source_result.code);
            session_log_flush(storage_selected());
            scr_clear();
            scr_printf("Could not load %s\nCode: %d\n",
                       source.path, source_result.code);
            wait_to_return();
            return;
        }
        if (source_result.stage == BOOTSTRAP_SOURCE_STAGE_KELF) {
            session_log_line("MBR.XLF structural validation failed: %d",
                             source_result.code);
            session_log_flush(storage_selected());
            scr_clear();
            scr_printf("MBR.XLF is not a structurally valid KELF.\n");
            scr_printf("Validation code: %d\n", source_result.code);
            wait_to_return();
            return;
        }

        session_log_line(
            "MBR.XLF capacity validation failed: getstat=%d, start=0x%08x, "
            "size=0x%08x, sectors=%u",
            source_result.getstat_result, source_result.mbr_start,
            source_result.mbr_size, source_result.payload_sectors);
        session_log_flush(storage_selected());
        scr_clear();
        scr_printf("The __mbr reserved payload area is not valid.\n");
        scr_printf("getstat:%d start:0x%08x size:0x%08x\n",
                   source_result.getstat_result,
                   source_result.mbr_start, source_result.mbr_size);
        wait_to_return();
        return;
    }

    backup_path = save_backup();
    if (backup_path == NULL) {
        bootstrap_source_release(&source);
        backup_error_screen();
        return;
    }

    signing_port = storage_targets[storage_selected()].memory_card_port;
    if (signing_port < 0)
        signing_port = choose_signing_card();
    if (signing_port < 0) {
        bootstrap_source_release(&source);
        return;
    }

    scr_clear();
    scr_printf("Signing MBR.XLF through mc%d...\n", signing_port);
    if (SecrDownloadFile(2 + signing_port, 0, source.payload) == NULL) {
        bootstrap_source_release(&source);
        session_log_line("MagicGate signing failed through mc%d", signing_port);
        session_log_flush(storage_selected());
        scr_clear();
        scr_printf("MagicGate signing failed through mc%d.\n", signing_port);
        scr_printf("HDD was NOT modified. Check the PS2 memory card.\n");
        wait_to_return();
        return;
    }
    result = kelf_validate_layout(source.payload, source.payload_size);
    if (result < 0) {
        bootstrap_source_release(&source);
        fatal_screen("Signed KELF failed structural validation.", result);
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
        header_buffer, source.payload, source.payload_size,
        MBR_PAYLOAD_START, source.sectors, free, source.payload,
        &transaction);
    source.payload = NULL;
    if (result < 0) {
        if (transaction.stage == BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD)
            fatal_screen(
                "Payload write/read-back failed; pointer remains disabled.",
                result);
        else if (transaction.stage ==
                 BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET)
            fatal_screen(
                "Payload verified, but enabling its pointer failed.", result);
        else
            fatal_screen("Installed pointer verification failed.", result);
    }

    session_log_line(
        "Bootstrap installed and verified at sector 0x%08x (%u bytes, "
        "%u sectors)", MBR_PAYLOAD_START, source.payload_size,
        source.sectors);
    refresh_boot_chain_report(1);
    installed_rescue = save_rescue_capsule();
    session_log_flush(storage_selected());

    scr_clear();
    scr_printf("Bootstrap installed, enabled, and verified.\n\n");
    scr_printf("The payload is signed for this console.\n");
    scr_printf("Rescue: %s\n",
               installed_rescue != NULL ? installed_rescue : "not available");
    scr_printf("Remember: its expected partitions must also exist.\n");
    wait_to_return();
}

/* ------------------------------------------------------------------------- */
/* Main state machine                                                        */
/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int result;
    int hdd_status;

    select_launch_storage(argc, argv);

    /* Bring up video first so initialization failures remain visible. */
    init_scr();
    scr_printf(APP_NAME " v%s\n", APP_VERSION);
    scr_printf("Initializing...\n");

    reset_iop();
    result = load_modules();
    if (result < 0) {
        scr_printf("ERROR: Could not load required IOP modules.\n");
        scr_printf("Code: %d\nPower off with the console button.\n", result);
        SleepThread();
    }
    fileXioInit();
    poweroffInit();
    SecrInit();
    if (init_pad() < 0) {
        scr_printf("ERROR: Controller 1 is not available.\n");
        scr_printf("Power off with the console button.\n");
        SleepThread();
    }

    /* Refuse menu access unless ps2hdd recognizes a safe APA device. */
    hdd_status = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    if (hdd_status != 0)
        fatal_screen("HDD is missing, locked, or not valid APA.", hdd_status);
    result = read_header(header_buffer);
    if (result < 0)
        fatal_screen("Could not read sectors 0-1.", result);
    if (!is_standard_apa_header(header_buffer))
        fatal_screen("Invalid APA __mbr header or checksum.", -101);
    if (is_hybrid_gpt(header_buffer))
        fatal_screen("Hybrid APA/GPT layout is not supported.", -102);

    session_log_line("Session started: %s v%s; launch storage=%s", APP_NAME,
             APP_VERSION, storage_targets[storage_selected()].name);
    session_log_line("APA header valid; osdStart=0x%08x; osdSize=0x%08x",
             (unsigned int)read_le32(header_buffer + APA_OSD_START_OFFSET),
             (unsigned int)read_le32(header_buffer + APA_OSD_SIZE_OFFSET));
    refresh_boot_chain_report(1);

    /* Keep the menu live so storage and power choices do not require relaunching. */
    for (;;) {
        u32 start = read_le32(header_buffer + APA_OSD_START_OFFSET);
        u32 size = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);
        u32 pressed;

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("APA header: valid\n");
        scr_printf("osdStart : 0x%08x\n", (unsigned int)start);
        scr_printf("osdSize  : 0x%08x\n", (unsigned int)size);
        scr_printf("Storage  : %s\n", storage_targets[storage_selected()].name);
        scr_printf("Detected : %s\n", boot_chain.family);
        scr_printf("Report   : %s\n\n",
                   boot_report_session_last_save_result() == 0
                       ? "saved" : "not saved");

        if (start != 0 || size != 0) {
            scr_printf("HDD bootstrap is ENABLED.\n\n");
            scr_printf("X        Back up and disable\n");
        } else {
            scr_printf("HDD bootstrap is DISABLED.\n\n");
            scr_printf("SQUARE   Restore rescue / legacy pointer\n");
            scr_printf("CIRCLE   Sign and install MBR.XLF\n");
        }
        scr_printf("START    Create full rescue backup\n");
        scr_printf("R1       Inspect boot chain / save reports\n");
        scr_printf("SELECT   Change storage device\n");
        scr_printf("TRIANGLE Power / restart menu\n");

        pressed = wait_for_press();
        if (pressed & PAD_START) {
            backup_current_state();
            continue;
        }
        if (pressed & PAD_R1) {
            diagnostics_screen();
            continue;
        }
        if (pressed & PAD_SELECT) {
            choose_storage();
            continue;
        }
        if (pressed & PAD_TRIANGLE) {
            power_menu();
            continue;
        }
        if ((pressed & PAD_CROSS) && (start != 0 || size != 0)) {
            disable_bootstrap();
            continue;
        }
        if ((pressed & PAD_SQUARE) && start == 0 && size == 0) {
            restore_rescue_capsule();
            continue;
        }
        if ((pressed & PAD_CIRCLE) && start == 0 && size == 0) {
            install_bootstrap();
            continue;
        }
    }
}
