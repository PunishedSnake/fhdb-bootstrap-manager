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
#include <delaythread.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <debug.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>
#include <libsecr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apa.h"
#include "boot_chain.h"
#include "boot_chain_ps2.h"
#include "boot_payload_ps2.h"
#include "boot_report.h"
#include "capsule_format.h"
#include "hdd_limits.h"
#include "hdd_read.h"
#include "kelf.h"
#include "platform.h"
#include "sha256.h"
#include "storage.h"
#include "version.h"

/* Application identity; APA layout constants live in apa.h. */
#define APP_NAME "PS2 HDD Bootstrap Manager"

/* The HDD bootstrap payload begins in the reserved area of __mbr at sector 0x2000. */
#define MBR_PAYLOAD_START HDD_MBR_PAYLOAD_START
#define SECTOR_SIZE HDD_SECTOR_SIZE
#define TRANSFER_SECTORS HDD_TRANSFER_SECTORS
#define TRANSFER_BYTES HDD_TRANSFER_BYTES
#define MAX_MBR_PAYLOAD_SIZE HDD_MAX_MBR_PAYLOAD_SIZE
#define MAX_RESCUE_CAPSULE_SIZE \
    (RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE + MAX_MBR_PAYLOAD_SIZE)

/* Human-readable diagnostics and logging remain bounded in EE memory. */
#define SESSION_LOG_SIZE 16384
#define SESSION_LOG_ROTATE_SIZE (128 * 1024)
#define TEXT_FILE_LIMIT 32768

/* Local names keep the source compatible with older PS2SDK header revisions. */
#define HDIOC_SETOSDMBR_LOCAL 0x6833
#define HDIOC_WRITESECTOR_LOCAL 0x6837
#define HDIOC_FLUSH_LOCAL 0x4804

/* Backup diagnostics use sentinel values that cannot be mistaken for IOP errors. */
#define BACKUP_SLOT_COUNT 2
#define BACKUP_NOT_TRIED 999999
#define BACKUP_OCCUPIED 999998

/* DMA-safe buffers shared with pad, fileXio, and raw HDD RPC operations. */
static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char verify_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char backup_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char sector_verify_buffer[TRANSFER_BYTES] __attribute__((aligned(64)));
static unsigned char capsule_metadata[RESCUE_CAPSULE_METADATA_SIZE]
    __attribute__((aligned(64)));

/* The latest report is kept in memory so it can follow a storage selection. */
static char boot_report[BOOT_REPORT_SIZE];
static unsigned int boot_report_length;
static char session_log[SESSION_LOG_SIZE];
static unsigned int session_log_length;
static unsigned int session_log_saved[3];
static unsigned int session_log_sequence;
static int last_report_save_result = BACKUP_NOT_TRIED;

/* Per-slot results are displayed verbatim when a mandatory backup fails. */
static int backup_read_result[BACKUP_SLOT_COUNT];
static int backup_write_result[BACKUP_SLOT_COUNT];
static int backup_verify_result[BACKUP_SLOT_COUNT];
static char backup_diagnostic_path[BACKUP_SLOT_COUNT][64];

/* Input packet used by the raw write devctl, including at most two sectors. */
typedef struct {
    u32 lba;
    u32 size;
    unsigned char data[TRANSFER_BYTES];
} raw_write_packet_t;

static raw_write_packet_t write_packet __attribute__((aligned(64)));

/* Read-only evidence is modeled in boot_chain.h. */
static boot_chain_info_t boot_chain;

/* Forward declarations for logging paths used by early fatal screens. */
static int flush_session_log(unsigned int storage);
static int save_boot_chain_report(unsigned int storage);
static void refresh_boot_chain_report(int save_to_storage);

/* Append formatted text without allowing a report or log buffer to overflow. */
static void append_text(char *buffer, unsigned int capacity,
                        unsigned int *length, const char *format, ...)
{
    va_list arguments;
    int written;

    if (*length >= capacity - 1)
        return;
    va_start(arguments, format);
    written = vsnprintf(buffer + *length, capacity - *length,
                        format, arguments);
    va_end(arguments);
    if (written < 0)
        return;
    if ((unsigned int)written >= capacity - *length)
        *length = capacity - 1;
    else
        *length += (unsigned int)written;
}

/* Record an ordered English diagnostic line for the selected-device log. */
static void log_line(const char *format, ...)
{
    char line[256];
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (written < 0)
        return;
    line[sizeof(line) - 1] = '\0';
    append_text(session_log, sizeof(session_log), &session_log_length,
                "[%04u] %s\n", ++session_log_sequence, line);
}

/* ------------------------------------------------------------------------- */
/* Console lifecycle and UI                                                */
/* ------------------------------------------------------------------------- */

/* Perform a normal PS2 shutdown through the poweroff RPC service. */
static void shutdown_console(void)
{
    log_line("Controlled power-off requested");
    flush_session_log(selected_storage);
    poweroffShutdown();
    SleepThread();
}

/* Return to the ROM Browser using the same ExecOSD path used by PS2 HDD tools. */
static void restart_to_browser(void)
{
    static char *browser_args[] = {"BootBrowser", NULL};

    log_line("Restart to PS2 Browser requested");
    flush_session_log(selected_storage);
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
    log_line("FATAL: %s (code %d)", message, code);
    flush_session_log(selected_storage);
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


/* Append only the unsaved portion of this session to one selected device. */
static int flush_session_log(unsigned int storage)
{
    char path[64];
    iox_stat_t existing;
    unsigned int start;
    int attempts;
    int truncate = 0;
    int result;

    if (storage >= STORAGE_TARGET_COUNT || session_log_length == 0)
        return -1;
    storage_path(path, sizeof(path), storage, "HDDMAN.LOG");
    memset(&existing, 0, sizeof(existing));
    if (fileXioGetStat(path, &existing) >= 0 &&
        existing.size >= SESSION_LOG_ROTATE_SIZE) {
        truncate = 1;
        session_log_saved[storage] = 0;
    }
    start = session_log_saved[storage];
    if (start > session_log_length)
        start = 0;
    if (start == session_log_length)
        return 0;
    attempts = storage == 2 ? 20 : 1;
    do {
        result = append_log_file(path, session_log + start,
                                 (int)(session_log_length - start), truncate);
        if (result >= 0)
            break;
        DelayThread(250000);
    } while (--attempts > 0);
    if (result == 0)
        session_log_saved[storage] = session_log_length;
    return result;
}

/* Save the latest read-only boot-chain report as a separate text file. */
static int save_boot_chain_report(unsigned int storage)
{
    char path[64];
    int attempts;

    if (storage >= STORAGE_TARGET_COUNT || boot_report_length == 0)
        return -1;
    storage_path(path, sizeof(path), storage, "BOOTCHAIN.TXT");
    attempts = storage == 2 ? 20 : 1;
    do {
        last_report_save_result =
            write_whole_file(path, boot_report, (int)boot_report_length);
        if (last_report_save_result >= 0)
            break;
        DelayThread(250000);
    } while (--attempts > 0);
    return last_report_save_result;
}

/* Load a bounded MBR.XLF into EE memory; USB receives a short mount grace period. */
static int load_payload_file(const char *path, unsigned char **data_out,
                             unsigned int *size_out, int retry_usb)
{
    int fd = -1;
    int attempts = retry_usb ? 20 : 1;
    int size;
    int total;
    unsigned char *data;

    while (attempts-- > 0) {
        fd = fileXioOpen(path, FIO_O_RDONLY, 0);
        if (fd >= 0)
            break;
        DelayThread(250000);
    }
    if (fd < 0)
        return fd;

    size = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (size <= 0 || (unsigned int)size > MAX_MBR_PAYLOAD_SIZE) {
        fileXioClose(fd);
        return -120;
    }
    if (fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return -121;
    }

    data = malloc(size);
    if (data == NULL) {
        fileXioClose(fd);
        return -122;
    }
    total = 0;
    while (total < size) {
        int received = fileXioRead(fd, data + total, size - total);
        if (received <= 0) {
            free(data);
            fileXioClose(fd);
            return received < 0 ? received : -123;
        }
        total += received;
    }
    fileXioClose(fd);
    *data_out = data;
    *size_out = (unsigned int)size;
    return 0;
}

/* Select mc0, mc1, or mass without changing anything until X confirms. */
static void choose_storage(void)
{
    unsigned int choice = selected_storage;

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
            selected_storage = choice;
            log_line("Selected storage device: %s",
                     storage_targets[selected_storage].name);
            save_boot_chain_report(selected_storage);
            flush_session_log(selected_storage);
            return;
        }
        if (pressed & PAD_TRIANGLE)
            return;
    }
}

/* USB has no MagicGate hardware, so choose which memory card signs MBR.XLF. */
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

/* Update only osdStart/osdSize through ps2hdd and flush its APA cache. */
static int set_osd_mbr(u32 start, u32 size)
{
    hddSetOsdMBR_t info;
    int result;

    info.start = start;
    info.size = size;
    result = fileXioDevctl("hdd0:", HDIOC_SETOSDMBR_LOCAL,
                           &info, sizeof(info), NULL, 0);
    if (result < 0)
        return result;
    return fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL, NULL, 0, NULL, 0);
}

/* Re-read the APA header and confirm checksum plus requested pointer values. */
static int verify_values(u32 expected_start, u32 expected_size)
{
    if (read_header(verify_buffer) < 0)
        return -1;
    if (!is_standard_apa_header(verify_buffer))
        return -2;
    if (read_le32(verify_buffer + APA_OSD_START_OFFSET) != expected_start)
        return -3;
    if (read_le32(verify_buffer + APA_OSD_SIZE_OFFSET) != expected_size)
        return -4;
    memcpy(header_buffer, verify_buffer, APA_HEADER_SIZE);
    return 0;
}

/* Write the signed payload in two-sector chunks, flush, then compare every byte. */
static int write_and_verify_payload(const unsigned char *payload,
                                    unsigned int payload_size, u32 start_sector)
{
    unsigned int offset = 0;
    u32 sector_offset = 0;

    while (offset < payload_size) {
        unsigned int remaining = payload_size - offset;
        unsigned int bytes = remaining > TRANSFER_BYTES ? TRANSFER_BYTES : remaining;
        u32 sectors = (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        int result;

        write_packet.lba = start_sector + sector_offset;
        write_packet.size = sectors;
        memset(write_packet.data, 0, TRANSFER_BYTES);
        memcpy(write_packet.data, payload + offset, bytes);
        result = fileXioDevctl("hdd0:", HDIOC_WRITESECTOR_LOCAL,
                               &write_packet,
                               sizeof(write_packet.lba) + sizeof(write_packet.size) +
                                   (sectors * SECTOR_SIZE),
                               NULL, 0);
        if (result < 0)
            return result;
        offset += bytes;
        sector_offset += sectors;
    }

    if (fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL, NULL, 0, NULL, 0) < 0)
        return -130;

    offset = 0;
    sector_offset = 0;
    while (offset < payload_size) {
        unsigned int remaining = payload_size - offset;
        unsigned int bytes = remaining > TRANSFER_BYTES ? TRANSFER_BYTES : remaining;
        u32 sectors = (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        int result;

        memset(write_packet.data, 0, TRANSFER_BYTES);
        memcpy(write_packet.data, payload + offset, bytes);
        result = hdd_read_raw_sectors(start_sector + sector_offset, sectors,
                                  sector_verify_buffer);
        if (result < 0)
            return result;
        if (memcmp(write_packet.data, sector_verify_buffer,
                   sectors * SECTOR_SIZE) != 0)
            return -131;
        offset += bytes;
        sector_offset += sectors;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Read-only boot-chain inspection                                           */
/* ------------------------------------------------------------------------- */

/* Map ROMVER's region character to the memory-card system folder family. */


/* Inspect all known regional FMCB folders because cross-model cards can mix them. */


/* Read the three locations from which FMCB configurations are commonly used. */


/* Inspect __sysconf for FHDB and modern OSDMenu configuration evidence. */


/* Inspect __system for the executable that a bootstrap is likely to launch. */


/* Classify the probable family separately from the directly observed evidence. */


/* Collect active-payload and downstream evidence, then classify it. */
static void analyze_boot_chain(boot_chain_info_t *info)
{
    u32 start = read_le32(header_buffer + APA_OSD_START_OFFSET);
    u32 sectors = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);

    memset(info, 0, sizeof(*info));
    info->skip_hdd[0] = -1;
    info->skip_hdd[1] = -1;
    info->skip_hdd[2] = -1;
    read_romver(info->romver);
    expected_system_folder(info->romver, info->expected_system_folder,
                           sizeof(info->expected_system_folder));
    scan_active_payload_evidence(info, start, sectors);
    scan_skip_hdd_settings(info);
    scan_memory_card_boot_files(info);
    scan_sysconf_partition(info);
    scan_system_partition(info);
    classify_boot_chain(info, start, sectors);
}
/* Refresh the in-memory evidence and optionally persist both report and log. */
static void refresh_boot_chain_report(int save_to_storage)
{
    analyze_boot_chain(&boot_chain);
    boot_report_length = boot_report_render(
        boot_report, sizeof(boot_report), &boot_chain,
        read_le32(header_buffer + APA_OSD_START_OFFSET),
        read_le32(header_buffer + APA_OSD_SIZE_OFFSET),
        APP_NAME, APP_VERSION);
    log_line("Boot-chain scan: family='%s', confidence=%s, payload_read=%d, "
             "kelf=%d", boot_chain.family, boot_chain.confidence,
             boot_chain.payload_read_result, boot_chain.payload_kelf_result);
    if (save_to_storage) {
        int result = save_boot_chain_report(selected_storage);

        log_line("BOOTCHAIN.TXT save to %s returned %d",
                 storage_targets[selected_storage].name, result);
        flush_session_log(selected_storage);
    }
}

/* Present a concise console summary while the complete evidence stays in text. */
static void diagnostics_screen(void)
{
    char path[64];

    refresh_boot_chain_report(1);
    storage_path(path, sizeof(path), selected_storage, "BOOTCHAIN.TXT");
    scr_clear();
    scr_printf("Boot-chain inspection complete.\n\n");
    scr_printf("Family    : %s\n", boot_chain.family);
    scr_printf("Confidence: %s\n", boot_chain.confidence);
    scr_printf("Next stage: %s\n", boot_chain.next_stage);
    scr_printf("Report    : %s\n", path);
    scr_printf("Save code : %d\n\n", last_report_save_result);
    scr_printf("The complete report is separate from HDDMAN.LOG.\n");
    wait_to_return();
}

/* ------------------------------------------------------------------------- */
/* Backup creation and restoration                                           */
/* ------------------------------------------------------------------------- */

/* Generate one of two non-overwriting backup paths on the selected device. */
static void backup_path_for_slot(char *path, unsigned int capacity,
                                 unsigned int storage, unsigned int slot)
{
    storage_path(path, capacity, storage,
                 slot == 0 ? "HDDMBR.BIN" : "HDDMBR2.BIN");
}

/* Save and read back the current header for standalone use or a write safety gate. */
static const char *save_backup(void)
{
    unsigned int i;

    for (i = 0; i < BACKUP_SLOT_COUNT; i++) {
        backup_path_for_slot(backup_diagnostic_path[i],
                             sizeof(backup_diagnostic_path[i]),
                             selected_storage, i);
        backup_read_result[i] = BACKUP_NOT_TRIED;
        backup_write_result[i] = BACKUP_NOT_TRIED;
        backup_verify_result[i] = BACKUP_NOT_TRIED;
    }

    for (i = 0; i < BACKUP_SLOT_COUNT; i++) {
        iox_stat_t existing_stat;

        memset(&existing_stat, 0, sizeof(existing_stat));
        backup_read_result[i] =
            fileXioGetStat(backup_diagnostic_path[i], &existing_stat);
        if (backup_read_result[i] >= 0) {
            if (existing_stat.size == APA_HEADER_SIZE &&
                read_exact_file(backup_diagnostic_path[i], backup_buffer,
                                APA_HEADER_SIZE) == 0 &&
                is_standard_apa_header(backup_buffer) &&
                memcmp(header_buffer, backup_buffer, APA_HEADER_SIZE) == 0)
                return backup_diagnostic_path[i];
            backup_write_result[i] = BACKUP_OCCUPIED;
            continue;
        }

        backup_write_result[i] =
            write_whole_file(backup_diagnostic_path[i], header_buffer,
                             APA_HEADER_SIZE);
        if (backup_write_result[i] == 0) {
            backup_verify_result[i] =
                read_exact_file(backup_diagnostic_path[i], backup_buffer,
                                APA_HEADER_SIZE);
            if (backup_verify_result[i] == 0 &&
                memcmp(header_buffer, backup_buffer, APA_HEADER_SIZE) == 0)
                return backup_diagnostic_path[i];
            if (backup_verify_result[i] == 0)
                backup_verify_result[i] = -1;
        }
    }
    return NULL;
}

/* Generate one of two non-overwriting full rescue-capsule paths. */
static void rescue_path_for_slot(char *path, unsigned int capacity,
                                 unsigned int storage, unsigned int slot)
{
    storage_path(path, capacity, storage,
                 slot == 0 ? "HDDRESCUE.BIN" : "HDDRESCUE2.BIN");
}

/* Write one complete capsule with a short USB-mount grace period if needed. */
static int write_rescue_file(const char *path, const unsigned char *metadata,
                             const unsigned char *apa_header,
                             const unsigned char *payload,
                             unsigned int payload_bytes)
{
    int attempts = selected_storage == 2 ? 20 : 1;
    int fd = -1;
    const unsigned char *parts[3];
    unsigned int sizes[3];
    unsigned int part;

    while (attempts-- > 0) {
        fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC,
                         0666);
        if (fd >= 0)
            break;
        DelayThread(250000);
    }
    if (fd < 0)
        return fd;

    parts[0] = metadata;
    parts[1] = apa_header;
    parts[2] = payload;
    sizes[0] = RESCUE_CAPSULE_METADATA_SIZE;
    sizes[1] = APA_HEADER_SIZE;
    sizes[2] = payload_bytes;
    for (part = 0; part < 3; part++) {
        unsigned int total = 0;

        while (total < sizes[part]) {
            int written = fileXioWrite(fd, parts[part] + total,
                                       sizes[part] - total);

            if (written <= 0) {
                fileXioClose(fd);
                return written < 0 ? written : -180;
            }
            total += (unsigned int)written;
        }
    }
    fileXioClose(fd);
    return 0;
}

/* Decode a capsule and verify both embedded SHA-256 digests before use. */
static int load_rescue_file(const char *path, rescue_capsule_info_t *info,
                            unsigned char **file_data_out,
                            unsigned int *file_size_out)
{
    unsigned char *data = NULL;
    unsigned int size = 0;
    unsigned char digest[32];
    const unsigned char *saved_header;
    const unsigned char *saved_payload;
    int result;

    result = read_bounded_file(path, MAX_RESCUE_CAPSULE_SIZE, &data, &size);
    if (result < 0)
        return result;
    if (size < RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE) {
        free(data);
        return -181;
    }
    memcpy(capsule_metadata, data, RESCUE_CAPSULE_METADATA_SIZE);
    result = rescue_capsule_decode(capsule_metadata, size, info);
    if (result < 0) {
        free(data);
        return -190 + result;
    }

    saved_header = data + RESCUE_CAPSULE_METADATA_SIZE;
    saved_payload = saved_header + APA_HEADER_SIZE;
    sha256_buffer(saved_header, APA_HEADER_SIZE, digest);
    if (memcmp(digest, info->apa_sha256, sizeof(digest)) != 0 ||
        !is_standard_apa_header(saved_header) ||
        (info->flags & RESCUE_CAPSULE_FLAG_VALID_APA) == 0) {
        free(data);
        return -182;
    }
    if ((info->flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) != 0) {
        unsigned int kelf_bytes = 0;

        sha256_buffer(saved_payload, info->payload_bytes, digest);
        if (memcmp(digest, info->payload_sha256, sizeof(digest)) != 0) {
            free(data);
            return -183;
        }
        if ((info->flags & RESCUE_CAPSULE_FLAG_VALID_KELF) != 0 &&
            (kelf_size_from_disk_image(saved_payload, info->payload_bytes,
                                       &kelf_bytes) < 0 ||
             kelf_bytes != info->kelf_file_bytes)) {
            free(data);
            return -184;
        }
    }
    *file_data_out = data;
    *file_size_out = size;
    return 0;
}

/* Check whether an existing protected slot already contains this exact state. */
static int rescue_file_matches(const char *path,
                               const rescue_capsule_info_t *expected)
{
    rescue_capsule_info_t existing;
    unsigned char *data = NULL;
    unsigned int size = 0;
    int result = load_rescue_file(path, &existing, &data, &size);

    (void)size;
    if (result < 0)
        return 0;
    result = existing.flags == expected->flags &&
             existing.payload_start == expected->payload_start &&
             existing.payload_sectors == expected->payload_sectors &&
             existing.payload_bytes == expected->payload_bytes &&
             memcmp(existing.apa_sha256, expected->apa_sha256, 32) == 0 &&
             memcmp(existing.payload_sha256,
                    expected->payload_sha256, 32) == 0;
    free(data);
    return result;
}

/* Save header plus exact active payload sectors and verify the resulting file. */
static const char *save_rescue_capsule(void)
{
    static char saved_path[64];
    rescue_capsule_info_t info;
    rescue_capsule_info_t verified_info;
    unsigned char *payload = NULL;
    unsigned char *verified_file = NULL;
    unsigned int payload_bytes = 0;
    unsigned int kelf_file_bytes = 0;
    unsigned int verified_size = 0;
    unsigned int slot;
    u32 start = read_le32(header_buffer + APA_OSD_START_OFFSET);
    u32 sectors = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);
    int result;

    if ((start == 0) != (sectors == 0)) {
        log_line("Rescue capsule rejected inconsistent pointer state");
        return NULL;
    }

    memset(&info, 0, sizeof(info));
    info.flags = RESCUE_CAPSULE_FLAG_VALID_APA;
    info.payload_start = start;
    info.payload_sectors = sectors;
    snprintf(info.romver, sizeof(info.romver), "%s", boot_chain.romver);
    snprintf(info.family, sizeof(info.family), "%s", boot_chain.family);
    snprintf(info.confidence, sizeof(info.confidence), "%s",
             boot_chain.confidence);
    sha256_buffer(header_buffer, APA_HEADER_SIZE, info.apa_sha256);

    if (start != 0) {
        result = hdd_validate_payload_bounds(start, sectors);
        if (result < 0) {
            log_line("Rescue capsule payload bounds failed: %d", result);
            return NULL;
        }
        result = hdd_read_payload_image(start, sectors, &payload, &payload_bytes);
        if (result < 0) {
            log_line("Rescue capsule payload read failed: %d", result);
            return NULL;
        }
        info.flags |= RESCUE_CAPSULE_FLAG_HAS_PAYLOAD;
        info.payload_bytes = payload_bytes;
        sha256_buffer(payload, payload_bytes, info.payload_sha256);
        if (kelf_size_from_disk_image(payload, payload_bytes,
                                      &kelf_file_bytes) == 0) {
            info.kelf_file_bytes = kelf_file_bytes;
            info.flags |= RESCUE_CAPSULE_FLAG_VALID_KELF;
        }
    }

    rescue_capsule_encode(capsule_metadata, &info);
    for (slot = 0; slot < BACKUP_SLOT_COUNT; slot++) {
        iox_stat_t existing;

        rescue_path_for_slot(saved_path, sizeof(saved_path), selected_storage,
                             slot);
        memset(&existing, 0, sizeof(existing));
        if (fileXioGetStat(saved_path, &existing) >= 0) {
            if (rescue_file_matches(saved_path, &info)) {
                free(payload);
                log_line("Existing rescue capsule already matches: %s",
                         saved_path);
                return saved_path;
            }
            continue;
        }

        result = write_rescue_file(saved_path, capsule_metadata,
                                   header_buffer, payload, payload_bytes);
        if (result < 0) {
            log_line("Rescue capsule write failed at %s: %d", saved_path,
                     result);
            continue;
        }
        result = load_rescue_file(saved_path, &verified_info, &verified_file,
                                  &verified_size);
        if (result == 0 && verified_info.flags == info.flags &&
            memcmp(verified_info.apa_sha256, info.apa_sha256, 32) == 0 &&
            memcmp(verified_info.payload_sha256,
                   info.payload_sha256, 32) == 0) {
            free(verified_file);
            free(payload);
            log_line("Rescue capsule saved and verified: %s (%u bytes)",
                     saved_path, verified_size);
            return saved_path;
        }
        free(verified_file);
        log_line("Rescue capsule read-back verification failed: %s (%d)",
                 saved_path, result);
    }
    free(payload);
    return NULL;
}

/* Locate the first valid same-disk capsule, distinguishing absence from damage. */
static int find_rescue_capsule(char *found_path, unsigned int path_capacity,
                               rescue_capsule_info_t *info,
                               unsigned char **file_data,
                               unsigned int *file_size)
{
    unsigned int slot;
    int saw_existing = 0;
    int saw_invalid = 0;
    int saw_header_only = 0;
    int first_error = -200;

    for (slot = 0; slot < BACKUP_SLOT_COUNT; slot++) {
        char path[64];
        rescue_capsule_info_t candidate;
        unsigned char *candidate_data = NULL;
        unsigned int candidate_size = 0;
        const unsigned char *candidate_header;
        int result;

        rescue_path_for_slot(path, sizeof(path), selected_storage, slot);
        if (!path_exists(path))
            continue;
        saw_existing = 1;
        result = load_rescue_file(path, &candidate, &candidate_data,
                                  &candidate_size);
        if (result < 0) {
            saw_invalid = 1;
            if (first_error == -200 || first_error == -202)
                first_error = result;
            continue;
        }
        candidate_header = candidate_data + RESCUE_CAPSULE_METADATA_SIZE;
        if (!headers_match_same_disk(header_buffer, candidate_header)) {
            free(candidate_data);
            saw_invalid = 1;
            if (first_error == -200 || first_error == -202)
                first_error = -201;
            continue;
        }
        if ((candidate.flags & RESCUE_CAPSULE_FLAG_HAS_PAYLOAD) == 0) {
            free(candidate_data);
            saw_header_only = 1;
            if (first_error == -200)
                first_error = -202;
            continue;
        }
        if ((candidate.flags & RESCUE_CAPSULE_FLAG_VALID_KELF) == 0) {
            free(candidate_data);
            saw_invalid = 1;
            if (first_error == -200 || first_error == -202)
                first_error = -203;
            continue;
        }
        snprintf(found_path, path_capacity, "%s", path);
        *info = candidate;
        *file_data = candidate_data;
        *file_size = candidate_size;
        return 0;
    }
    if (!saw_existing)
        return -200;
    if (saw_invalid)
        return first_error;
    return saw_header_only ? -202 : first_error;
}

/* Search new names first, then the v0.1.x FHDB compatibility names. */
static const char *load_backup(void)
{
    static const char *const filenames[] = {
        "HDDMBR.BIN", "HDDMBR2.BIN", "FHDBMBR.BIN", "FHDBMBR2.BIN"
    };
    static char found_path[64];
    unsigned int i;

    for (i = 0; i < sizeof(filenames) / sizeof(filenames[0]); i++) {
        storage_path(found_path, sizeof(found_path), selected_storage, filenames[i]);
        if (read_exact_file(found_path, backup_buffer, APA_HEADER_SIZE) == 0 &&
            is_standard_apa_header(backup_buffer) &&
            headers_match_same_disk(header_buffer, backup_buffer) &&
            read_le32(backup_buffer + APA_OSD_START_OFFSET) != 0 &&
            read_le32(backup_buffer + APA_OSD_SIZE_OFFSET) != 0)
            return found_path;
    }
    return NULL;
}

/* Explain why the safety gate stopped before touching the disk. */
static void backup_error_screen(void)
{
    unsigned int i;

    log_line("Mandatory header backup failed; no HDD write was performed");
    for (i = 0; i < BACKUP_SLOT_COUNT; i++)
        log_line("Backup slot %u path=%s read=%d write=%d verify=%d", i,
                 backup_diagnostic_path[i], backup_read_result[i],
                 backup_write_result[i], backup_verify_result[i]);
    flush_session_log(selected_storage);

    scr_clear();
    scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
    scr_printf("ERROR: Backup failed. HDD was NOT modified.\n\n");
    for (i = 0; i < BACKUP_SLOT_COUNT; i++) {
        scr_printf("%s\n", backup_diagnostic_path[i]);
        scr_printf(" read:%d write:%d verify:%d\n",
                   backup_read_result[i], backup_write_result[i],
                   backup_verify_result[i]);
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
    log_line("Standalone backup: header=%s rescue=%s",
             backup_path != NULL ? backup_path : "FAILED",
             rescue_path != NULL ? rescue_path : "FAILED");
    flush_session_log(selected_storage);

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

    result = set_osd_mbr(0, 0);
    if (result < 0)
        fatal_screen("HDIOC_SETOSDMBR failed.", result);
    result = verify_values(0, 0);
    if (result < 0)
        fatal_screen("Disable verification failed. Keep the backup.", result);

    log_line("Bootstrap pointer disabled and verified; header backup=%s; "
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
    const char *backup_path = load_backup();
    const char *safety_backup;
    u32 start;
    u32 size;
    int result;

    if (backup_path == NULL) {
        scr_clear();
        scr_printf("No valid enabled backup was found on %s.\n",
                   storage_targets[selected_storage].name);
        scr_printf("Legacy FHDBMBR*.BIN names were also checked.\n");
        wait_to_return();
        return;
    }

    start = read_le32(backup_buffer + APA_OSD_START_OFFSET);
    size = read_le32(backup_buffer + APA_OSD_SIZE_OFFSET);
    result = hdd_validate_payload_bounds(start, size);
    if (result < 0) {
        scr_clear();
        scr_printf("The saved pointer is outside this __mbr area.\n");
        scr_printf("Validation code: %d\n", result);
        log_line("Legacy restore rejected out-of-bounds pointer from %s: %d",
                 backup_path, result);
        flush_session_log(selected_storage);
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

    result = set_osd_mbr(start, size);
    if (result < 0)
        fatal_screen("Bootstrap restore failed.", result);
    result = verify_values(start, size);
    if (result < 0)
        fatal_screen("Restore verification failed.", result);

    log_line("Legacy pointer-only restore completed from %s; safety=%s",
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
    char rescue_path[64];
    rescue_capsule_info_t info;
    unsigned char *file_data = NULL;
    unsigned int file_size = 0;
    const unsigned char *payload;
    const char *safety_backup;
    int result;

    result = find_rescue_capsule(rescue_path, sizeof(rescue_path), &info,
                                 &file_data, &file_size);
    if (result == -200 || result == -202) {
        restore_legacy_pointer();
        return;
    }
    if (result < 0) {
        scr_clear();
        scr_printf("A rescue capsule exists but is not safe to restore.\n");
        scr_printf("Validation code: %d\n\n", result);
        scr_printf("It was not replaced by a pointer-only restore.\n");
        log_line("Rescue capsule load rejected with code %d", result);
        flush_session_log(selected_storage);
        wait_to_return();
        return;
    }

    result = hdd_validate_payload_bounds(info.payload_start, info.payload_sectors);
    if (result < 0) {
        free(file_data);
        scr_clear();
        scr_printf("Rescue payload does not fit this __mbr area.\n");
        scr_printf("Validation code: %d\n", result);
        wait_to_return();
        return;
    }
    safety_backup = save_backup();
    if (safety_backup == NULL) {
        free(file_data);
        backup_error_screen();
        return;
    }

    payload = file_data + RESCUE_CAPSULE_METADATA_SIZE + APA_HEADER_SIZE;
    scr_clear();
    scr_printf("Full rescue restore from\n%s\n\n", rescue_path);
    scr_printf("Family  : %s (%s)\n", info.family, info.confidence);
    scr_printf("Target  : 0x%08x\n", (unsigned int)info.payload_start);
    scr_printf("Payload : %u bytes / 0x%x sectors\n",
               (unsigned int)info.payload_bytes,
               (unsigned int)info.payload_sectors);
    scr_printf("Safety  : %s\n\n", safety_backup);
    scr_printf("The payload will be written and compared first.\n");
    scr_printf("Its pointer will be enabled only after verification.\n\n");
    scr_printf("Hold L1+R1 and press SQUARE to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE)) {
        free(file_data);
        return;
    }

    scr_clear();
    scr_printf("Restoring and verifying rescue payload...\n");
    result = write_and_verify_payload(payload, info.payload_bytes,
                                      info.payload_start);
    free(file_data);
    if (result < 0)
        fatal_screen("Rescue payload verification failed; pointer remains disabled.",
                     result);
    result = set_osd_mbr(info.payload_start, info.payload_sectors);
    if (result < 0)
        fatal_screen("Rescue payload verified, but pointer restore failed.",
                     result);
    result = verify_values(info.payload_start, info.payload_sectors);
    if (result < 0)
        fatal_screen("Rescue pointer read-back verification failed.", result);

    log_line("Full rescue restored from %s (%u bytes); safety backup=%s",
             rescue_path, file_size, safety_backup);
    refresh_boot_chain_report(1);
    scr_clear();
    scr_printf("Payload and pointer restored and verified.\n\n");
    scr_printf("Source: %s\n", rescue_path);
    wait_to_return();
}

/* Sign, write, verify, and finally enable a stock MBR.XLF payload. */
static void install_bootstrap(void)
{
    char source_path[64];
    const char *backup_path;
    const char *installed_rescue;
    unsigned char *payload = NULL;
    unsigned int payload_size = 0;
    unsigned int sectors;
    int signing_port;
    int result;
    iox_stat_t mbr_stat;

    if (read_le32(header_buffer + APA_OSD_START_OFFSET) != 0 ||
        read_le32(header_buffer + APA_OSD_SIZE_OFFSET) != 0) {
        scr_clear();
        scr_printf("Disable the current bootstrap before installing.\n");
        wait_to_return();
        return;
    }

    storage_path(source_path, sizeof(source_path), selected_storage, "MBR.XLF");
    scr_clear();
    scr_printf("Loading %s\n", source_path);
    if (selected_storage == 2)
        scr_printf("Waiting briefly for USB if necessary...\n");

    result = load_payload_file(source_path, &payload, &payload_size,
                               selected_storage == 2);
    if (result < 0) {
        log_line("MBR.XLF load failed from %s: %d", source_path, result);
        flush_session_log(selected_storage);
        scr_clear();
        scr_printf("Could not load %s\nCode: %d\n", source_path, result);
        wait_to_return();
        return;
    }
    result = kelf_validate_layout(payload, payload_size);
    if (result < 0) {
        free(payload);
        log_line("MBR.XLF structural validation failed: %d", result);
        flush_session_log(selected_storage);
        scr_clear();
        scr_printf("MBR.XLF is not a structurally valid KELF.\n");
        scr_printf("Validation code: %d\n", result);
        wait_to_return();
        return;
    }

    sectors = (payload_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    memset(&mbr_stat, 0, sizeof(mbr_stat));
    result = fileXioGetStat("hdd0:__mbr", &mbr_stat);
    if (result < 0 || mbr_stat.private_5 != 0 ||
        mbr_stat.size <= MBR_PAYLOAD_START ||
        sectors > mbr_stat.size - MBR_PAYLOAD_START) {
        free(payload);
        log_line("MBR.XLF capacity validation failed: getstat=%d, start=0x%08x, "
                 "size=0x%08x, sectors=%u", result,
                 (unsigned int)mbr_stat.private_5,
                 (unsigned int)mbr_stat.size, sectors);
        flush_session_log(selected_storage);
        scr_clear();
        scr_printf("The __mbr reserved payload area is not valid.\n");
        scr_printf("getstat:%d start:0x%08x size:0x%08x\n",
                   result, mbr_stat.private_5, mbr_stat.size);
        wait_to_return();
        return;
    }

    backup_path = save_backup();
    if (backup_path == NULL) {
        free(payload);
        backup_error_screen();
        return;
    }

    signing_port = storage_targets[selected_storage].memory_card_port;
    if (signing_port < 0)
        signing_port = choose_signing_card();
    if (signing_port < 0) {
        free(payload);
        return;
    }

    scr_clear();
    scr_printf("Signing MBR.XLF through mc%d...\n", signing_port);
    if (SecrDownloadFile(2 + signing_port, 0, payload) == NULL) {
        free(payload);
        log_line("MagicGate signing failed through mc%d", signing_port);
        flush_session_log(selected_storage);
        scr_clear();
        scr_printf("MagicGate signing failed through mc%d.\n", signing_port);
        scr_printf("HDD was NOT modified. Check the PS2 memory card.\n");
        wait_to_return();
        return;
    }
    result = kelf_validate_layout(payload, payload_size);
    if (result < 0) {
        free(payload);
        fatal_screen("Signed KELF failed structural validation.", result);
    }

    scr_clear();
    scr_printf("Ready to install signed HDD bootstrap\n\n");
    scr_printf("Source : %s\n", source_path);
    scr_printf("Backup : %s\n", backup_path);
    scr_printf("Target : sector 0x%08x\n", MBR_PAYLOAD_START);
    scr_printf("Size   : %u bytes / 0x%x sectors\n\n",
               payload_size, sectors);
    scr_printf("This installs only the MBR bootstrap program.\n");
    scr_printf("It does not create FHDB/HDD-OSD partitions.\n\n");
    scr_printf("Hold L1+R1 and press CIRCLE to confirm.\n");
    scr_printf("Press TRIANGLE to cancel.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_CIRCLE)) {
        free(payload);
        return;
    }

    scr_clear();
    scr_printf("Writing and verifying signed payload...\n");
    result = write_and_verify_payload(payload, payload_size, MBR_PAYLOAD_START);
    free(payload);
    if (result < 0)
        fatal_screen("Payload write/read-back failed; pointer remains disabled.", result);

    result = set_osd_mbr(MBR_PAYLOAD_START, sectors);
    if (result < 0)
        fatal_screen("Payload verified, but enabling its pointer failed.", result);
    result = verify_values(MBR_PAYLOAD_START, sectors);
    if (result < 0)
        fatal_screen("Installed pointer verification failed.", result);

    log_line("Bootstrap installed and verified at sector 0x%08x (%u bytes, "
             "%u sectors)", MBR_PAYLOAD_START, payload_size, sectors);
    refresh_boot_chain_report(1);
    installed_rescue = save_rescue_capsule();
    flush_session_log(selected_storage);

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

    log_line("Session started: %s v%s; launch storage=%s", APP_NAME,
             APP_VERSION, storage_targets[selected_storage].name);
    log_line("APA header valid; osdStart=0x%08x; osdSize=0x%08x",
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
        scr_printf("Storage  : %s\n", storage_targets[selected_storage].name);
        scr_printf("Detected : %s\n", boot_chain.family);
        scr_printf("Report   : %s\n\n",
                   last_report_save_result == 0 ? "saved" : "not saved");

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
