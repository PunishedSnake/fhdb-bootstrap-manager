/*
 * PS2 HDD Bootstrap Manager
 *
 * Manages the PlayStation 2 HDD bootstrap stored in the APA __mbr partition.
 * The tool can independently back up the current APA master header, disable
 * or restore its bootstrap pointer, and sign, install, and verify an MBR
 * bootstrap without formatting the disk or manually rewriting sector zero.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Application identity and offsets within the 1024-byte APA master header. */
#define APP_NAME "PS2 HDD Bootstrap Manager"
#define APP_VERSION "0.2.0"
#define APA_HEADER_SIZE 1024
#define APA_MAGIC_OFFSET 0x004
#define APA_ID_OFFSET 0x010
#define APA_MBR_MAGIC_OFFSET 0x100
#define APA_OSD_START_OFFSET 0x130
#define APA_OSD_SIZE_OFFSET 0x134
#define PC_MBR_SIGNATURE_OFFSET 0x1fe

/* The HDD bootstrap payload begins in the reserved area of __mbr at sector 0x2000. */
#define MBR_PAYLOAD_START 0x2000
#define SECTOR_SIZE 512
#define TRANSFER_SECTORS 2
#define TRANSFER_BYTES (SECTOR_SIZE * TRANSFER_SECTORS)
#define MAX_MBR_PAYLOAD_SIZE (4 * 1024 * 1024)

/* Local names keep the source compatible with older PS2SDK header revisions. */
#define HDIOC_SETOSDMBR_LOCAL 0x6833
#define HDIOC_READSECTOR_LOCAL 0x6836
#define HDIOC_WRITESECTOR_LOCAL 0x6837
#define HDIOC_FLUSH_LOCAL 0x4804

/* Backup diagnostics use sentinel values that cannot be mistaken for IOP errors. */
#define BACKUP_SLOT_COUNT 2
#define BACKUP_NOT_TRIED 999999
#define BACKUP_OCCUPIED 999998

/* DMA-safe buffers shared with pad, fileXio, and raw HDD RPC operations. */
static unsigned char pad_buffer[256] __attribute__((aligned(64)));
static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char verify_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char backup_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char sector_verify_buffer[TRANSFER_BYTES] __attribute__((aligned(64)));

/* One global storage choice controls backup lookup and the MBR.XLF source path. */
typedef struct {
    const char *name;
    const char *prefix;
    int memory_card_port;
} storage_target_t;

static const storage_target_t storage_targets[] = {
    {"mc0", "mc0:", 0},
    {"mc1", "mc1:", 1},
    {"mass", "mass:", -1}
};

#define STORAGE_TARGET_COUNT (sizeof(storage_targets) / sizeof(storage_targets[0]))
static unsigned int selected_storage = 0;

/* Per-slot results are displayed verbatim when a mandatory backup fails. */
static int backup_read_result[BACKUP_SLOT_COUNT];
static int backup_write_result[BACKUP_SLOT_COUNT];
static int backup_verify_result[BACKUP_SLOT_COUNT];
static char backup_diagnostic_path[BACKUP_SLOT_COUNT][64];

/* IRX modules embedded into the ELF by the Makefile's bin2c rules. */
extern unsigned char iomanX_irx[];
extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];
extern unsigned int size_fileXio_irx;
extern unsigned char secrman_irx[];
extern unsigned int size_secrman_irx;
extern unsigned char freesio2_irx[];
extern unsigned int size_freesio2_irx;
extern unsigned char freepad_irx[];
extern unsigned int size_freepad_irx;
extern unsigned char mcman_irx[];
extern unsigned int size_mcman_irx;
extern unsigned char mcserv_irx[];
extern unsigned int size_mcserv_irx;
extern unsigned char secrsif_irx[];
extern unsigned int size_secrsif_irx;
extern unsigned char poweroff_irx[];
extern unsigned int size_poweroff_irx;
extern unsigned char bdm_irx[];
extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];
extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;
extern unsigned char usbmass_bd_irx[];
extern unsigned int size_usbmass_bd_irx;
extern unsigned char ps2dev9_irx[];
extern unsigned int size_ps2dev9_irx;
extern unsigned char ps2atad_irx[];
extern unsigned int size_ps2atad_irx;
extern unsigned char ps2hdd_irx[];
extern unsigned int size_ps2hdd_irx;

/* Argument layout used by the raw read devctl. */
typedef struct {
    u32 lba;
    u32 size;
} raw_transfer_t;

/* Input packet used by the raw write devctl, including at most two sectors. */
typedef struct {
    u32 lba;
    u32 size;
    unsigned char data[TRANSFER_BYTES];
} raw_write_packet_t;

static raw_write_packet_t write_packet __attribute__((aligned(64)));

/* ------------------------------------------------------------------------- */
/* APA header parsing and validation                                         */
/* ------------------------------------------------------------------------- */

/* Read an explicitly little-endian 32-bit value without alignment assumptions. */
static u32 read_le32(const unsigned char *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* APA checksum: the sum of words 1..255, excluding the checksum word itself. */
static u32 apa_checksum(const unsigned char *header)
{
    u32 sum = 0;
    unsigned int i;

    for (i = 1; i < 256; i++)
        sum += read_le32(header + (i * 4));
    return sum;
}

/* Reject anything that is not a normal, internally consistent APA master header. */
static int is_standard_apa_header(const unsigned char *header)
{
    static const unsigned char apa_magic[4] = {0x41, 0x50, 0x41, 0x00};
    static const char mbr_id[] = "__mbr";
    static const char sce_magic[] = "Sony Computer Entertainment Inc.";

    if (memcmp(header + APA_MAGIC_OFFSET, apa_magic, sizeof(apa_magic)) != 0)
        return 0;
    if (memcmp(header + APA_ID_OFFSET, mbr_id, sizeof(mbr_id) - 1) != 0)
        return 0;
    if (memcmp(header + APA_MBR_MAGIC_OFFSET, sce_magic, sizeof(sce_magic) - 1) != 0)
        return 0;
    if (read_le32(header) != apa_checksum(header))
        return 0;
    return 1;
}

/* Hybrid APA/GPT disks use the conventional 0x55AA signature in sector zero. */
static int is_hybrid_gpt(const unsigned char *header)
{
    return header[PC_MBR_SIGNATURE_OFFSET] == 0x55 &&
           header[PC_MBR_SIGNATURE_OFFSET + 1] == 0xaa;
}

/* Match a backup to this disk while ignoring checksum and mutable OSD fields. */
static int headers_match_same_disk(const unsigned char *a, const unsigned char *b)
{
    unsigned int i;

    for (i = 0; i < APA_HEADER_SIZE; i++) {
        if (i < 4 || (i >= APA_OSD_START_OFFSET && i < APA_OSD_SIZE_OFFSET + 4))
            continue;
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

/* Validate enough KELF structure to reject plain ELFs, truncation, and nonsense. */
static int validate_kelf_layout(const unsigned char *data, unsigned int size)
{
    const SecrKELFHeader_t *header;
    unsigned int offset;

    if (size < sizeof(SecrKELFHeader_t))
        return -1;
    if (size >= 4 && data[0] == 0x7f && data[1] == 'E' &&
        data[2] == 'L' && data[3] == 'F')
        return -2;

    header = (const SecrKELFHeader_t *)data;
    if (header->KELF_header_size < sizeof(SecrKELFHeader_t) ||
        header->KELF_header_size > size)
        return -3;
    if (header->BIT_count > 63)
        return -4;
    if (header->ELF_size == 0 ||
        (unsigned int)header->KELF_header_size + header->ELF_size != size)
        return -5;

    offset = sizeof(SecrKELFHeader_t) +
             (header->BIT_count * sizeof(SecrBitBlockData_t));
    if (offset > header->KELF_header_size)
        return -6;
    if ((header->flags & 1) != 0) {
        if (offset >= header->KELF_header_size)
            return -7;
        offset += data[offset] + 1;
    }
    if ((header->flags & 0xf000) == 0)
        offset += 8;
    if (offset + 32 > header->KELF_header_size)
        return -8;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* IOP, controller, storage, and console lifecycle                           */
/* ------------------------------------------------------------------------- */

/* Reset the IOP and enable loading embedded homebrew IRX modules. */
static void reset_iop(void)
{
    sceSifInitRpc(0);
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
    sceSifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

/* Execute an embedded IRX and treat either RPC or module-start failure as fatal. */
static int exec_irx(void *buffer, unsigned int size)
{
    int module_result = 0;
    int module_id = SifExecModuleBuffer(buffer, size, 0, NULL, &module_result);

    if (module_id < 0)
        return module_id;
    return module_result < 0 ? module_result : 0;
}

/* Load modules in dependency order, including USB and MagicGate services. */
static int load_modules(void)
{
    if (exec_irx(iomanX_irx, size_iomanX_irx) < 0) return -1;
    if (exec_irx(fileXio_irx, size_fileXio_irx) < 0) return -2;
    if (exec_irx(secrman_irx, size_secrman_irx) < 0) return -3;
    if (exec_irx(freesio2_irx, size_freesio2_irx) < 0) return -4;
    if (exec_irx(freepad_irx, size_freepad_irx) < 0) return -5;
    if (exec_irx(mcman_irx, size_mcman_irx) < 0) return -6;
    if (exec_irx(mcserv_irx, size_mcserv_irx) < 0) return -7;
    if (exec_irx(secrsif_irx, size_secrsif_irx) < 0) return -8;
    if (exec_irx(poweroff_irx, size_poweroff_irx) < 0) return -9;
    if (exec_irx(bdm_irx, size_bdm_irx) < 0) return -10;
    if (exec_irx(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx) < 0) return -11;
    if (exec_irx(usbd_irx, size_usbd_irx) < 0) return -12;
    if (exec_irx(usbmass_bd_irx, size_usbmass_bd_irx) < 0) return -13;
    if (exec_irx(ps2dev9_irx, size_ps2dev9_irx) < 0) return -14;
    if (exec_irx(ps2atad_irx, size_ps2atad_irx) < 0) return -15;
    if (exec_irx(ps2hdd_irx, size_ps2hdd_irx) < 0) return -16;
    return 0;
}

/* Wait for controller port 0, slot 0 to reach a readable state. */
static int wait_pad_ready(void)
{
    int state;
    int timeout = 500000;

    while (timeout-- > 0) {
        state = padGetState(0, 0);
        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1)
            return 0;
        DelayThread(10);
    }
    return -1;
}

/* Initialize the first controller using a statically allocated DMA buffer. */
static int init_pad(void)
{
    padInit(0);
    if (!padPortOpen(0, 0, pad_buffer))
        return -1;
    return wait_pad_ready();
}

/* Block until a new button edge is observed, avoiding repeated menu actions. */
static u32 wait_for_press(void)
{
    struct padButtonStatus buttons;
    static u32 previous = 0;

    for (;;) {
        if (wait_pad_ready() == 0 && padRead(0, 0, &buttons) != 0) {
            u32 current = 0xffffu ^ buttons.btns;
            u32 pressed = current & ~previous;
            previous = current;
            if (pressed)
                return pressed;
        }
        DelayThread(16000);
    }
}

/* Require a multi-button hold; TRIANGLE always cancels safely. */
static int wait_for_chord(u32 chord)
{
    struct padButtonStatus buttons;

    for (;;) {
        if (wait_pad_ready() == 0 && padRead(0, 0, &buttons) != 0) {
            u32 held = 0xffffu ^ buttons.btns;
            if (held & PAD_TRIANGLE)
                return 0;
            if ((held & chord) == chord)
                return 1;
        }
        DelayThread(16000);
    }
}

/* Perform a normal PS2 shutdown through the poweroff RPC service. */
static void shutdown_console(void)
{
    poweroffShutdown();
    SleepThread();
}

/* Return to the ROM Browser using the same ExecOSD path used by PS2 HDD tools. */
static void restart_to_browser(void)
{
    static char *browser_args[] = {"BootBrowser", NULL};

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
/* File and storage helpers                                                  */
/* ------------------------------------------------------------------------- */

/* Construct a root-level path for the selected storage device. */
static void storage_path(char *destination, unsigned int capacity,
                         unsigned int storage, const char *filename)
{
    snprintf(destination, capacity, "%s/%s",
             storage_targets[storage].prefix, filename);
}

/* Write all bytes, handling short fileXio transfers correctly. */
static int write_whole_file(const char *path, const void *data, int size)
{
    const unsigned char *source = data;
    int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    int total = 0;

    if (fd < 0)
        return fd;
    while (total < size) {
        int written = fileXioWrite(fd, source + total, size - total);
        if (written <= 0) {
            fileXioClose(fd);
            return written < 0 ? written : -1;
        }
        total += written;
    }
    fileXioClose(fd);
    return 0;
}

/* Read exactly the requested size and reject truncation or trailing bytes. */
static int read_exact_file(const char *path, void *data, int size)
{
    unsigned char *destination = data;
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    int total = 0;
    int result;

    if (fd < 0)
        return fd;
    result = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (result != size || fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return -1;
    }
    while (total < size) {
        result = fileXioRead(fd, destination + total, size - total);
        if (result <= 0) {
            fileXioClose(fd);
            return result < 0 ? result : -1;
        }
        total += result;
    }
    fileXioClose(fd);
    return 0;
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
    if (size <= 0 || size > MAX_MBR_PAYLOAD_SIZE) {
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

/* Read sectors 0 and 1, which contain the complete APA master header. */
static int read_header(unsigned char *destination)
{
    raw_transfer_t transfer;

    transfer.lba = 0;
    transfer.size = 2;
    memset(destination, 0, APA_HEADER_SIZE);
    return fileXioDevctl("hdd0:", HDIOC_READSECTOR_LOCAL,
                         &transfer, sizeof(transfer),
                         destination, APA_HEADER_SIZE);
}

/* Read a small raw sector range for post-write payload verification. */
static int read_raw_sectors(u32 lba, u32 sectors, unsigned char *destination)
{
    raw_transfer_t transfer;

    transfer.lba = lba;
    transfer.size = sectors;
    memset(destination, 0, sectors * SECTOR_SIZE);
    return fileXioDevctl("hdd0:", HDIOC_READSECTOR_LOCAL,
                         &transfer, sizeof(transfer), destination,
                         sectors * SECTOR_SIZE);
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
        result = read_raw_sectors(start_sector + sector_offset, sectors,
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

/* Save the current APA master header without changing any HDD data. */
static void backup_current_header(void)
{
    const char *backup_path = save_backup();

    if (backup_path == NULL) {
        backup_error_screen();
        return;
    }

    scr_clear();
    scr_printf("Current MBR header backup is available and verified.\n\n");
    scr_printf("Backup: %s\n\n", backup_path);
    scr_printf("No HDD data was modified.\n");
    wait_to_return();
}

/* Back up the active pointer, clear it, and verify the resulting header. */
static void disable_bootstrap(void)
{
    const char *backup_path = save_backup();
    int result;

    if (backup_path == NULL) {
        backup_error_screen();
        return;
    }

    scr_clear();
    scr_printf("Backup saved and verified:\n%s\n\n", backup_path);
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

    scr_clear();
    scr_printf("Bootstrap disabled and verified.\n\n");
    scr_printf("Backup: %s\n", backup_path);
    wait_to_return();
}

/* Restore only a non-zero pointer from a valid same-disk backup. */
static void restore_bootstrap(void)
{
    const char *backup_path = load_backup();
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
    scr_clear();
    scr_printf("Restore from %s\n", backup_path);
    scr_printf("osdStart: 0x%08x\n", (unsigned int)start);
    scr_printf("osdSize : 0x%08x\n\n", (unsigned int)size);
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

    scr_clear();
    scr_printf("Bootstrap restored and verified.\n");
    wait_to_return();
}

/* Sign, write, verify, and finally enable a stock MBR.XLF payload. */
static void install_bootstrap(void)
{
    char source_path[64];
    const char *backup_path;
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
        scr_clear();
        scr_printf("Could not load %s\nCode: %d\n", source_path, result);
        wait_to_return();
        return;
    }
    result = validate_kelf_layout(payload, payload_size);
    if (result < 0) {
        free(payload);
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
        scr_clear();
        scr_printf("MagicGate signing failed through mc%d.\n", signing_port);
        scr_printf("HDD was NOT modified. Check the PS2 memory card.\n");
        wait_to_return();
        return;
    }
    result = validate_kelf_layout(payload, payload_size);
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

    scr_clear();
    scr_printf("Bootstrap installed, enabled, and verified.\n\n");
    scr_printf("The payload is signed for this console.\n");
    scr_printf("Remember: its expected partitions must also exist.\n");
    wait_to_return();
}

/* ------------------------------------------------------------------------- */
/* Main state machine                                                        */
/* ------------------------------------------------------------------------- */

int main(void)
{
    int result;
    int hdd_status;

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

    /* Keep the menu live so storage and power choices do not require relaunching. */
    for (;;) {
        char backup_path[64];
        char payload_path[64];
        u32 start = read_le32(header_buffer + APA_OSD_START_OFFSET);
        u32 size = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);
        u32 pressed;

        storage_path(backup_path, sizeof(backup_path), selected_storage,
                     "HDDMBR.BIN");
        storage_path(payload_path, sizeof(payload_path), selected_storage,
                     "MBR.XLF");

        scr_clear();
        scr_printf(APP_NAME " v%s\n\n", APP_VERSION);
        scr_printf("APA header: valid\n");
        scr_printf("osdStart : 0x%08x\n", (unsigned int)start);
        scr_printf("osdSize  : 0x%08x\n", (unsigned int)size);
        scr_printf("Storage  : %s\n", storage_targets[selected_storage].name);
        scr_printf("Backup   : %s\n", backup_path);
        scr_printf("Payload  : %s\n\n", payload_path);

        if (start != 0 || size != 0) {
            scr_printf("HDD bootstrap is ENABLED.\n\n");
            scr_printf("X        Back up and disable\n");
        } else {
            scr_printf("HDD bootstrap is DISABLED.\n\n");
            scr_printf("SQUARE   Restore pointer from backup\n");
            scr_printf("CIRCLE   Sign and install MBR.XLF\n");
        }
        scr_printf("START    Back up current MBR header\n");
        scr_printf("SELECT   Change storage device\n");
        scr_printf("TRIANGLE Power / restart menu\n");

        pressed = wait_for_press();
        if (pressed & PAD_START) {
            backup_current_header();
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
            restore_bootstrap();
            continue;
        }
        if ((pressed & PAD_CIRCLE) && start == 0 && size == 0) {
            install_bootstrap();
            continue;
        }
    }
}
