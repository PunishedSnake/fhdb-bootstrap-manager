/*
 * FHDB Bootstrap Manager
 *
 * Safely disables or restores the PlayStation 2 HDD OSD bootstrap pointer
 * stored in the APA __mbr header. The program deliberately uses the public
 * ps2hdd devctl interface instead of writing sector zero by hand.
 *
 * The destructive-looking part is intentionally rather boring: validate the
 * header, create and verify a memory-card backup, ask for an awkward button
 * chord, update two fields through PS2SDK, flush, and read everything back.
 */

/* EE kernel, RPC, module loading, video, controller, and power-off services. */
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Application version and byte offsets within the 1024-byte APA header. */
#define APP_VERSION "0.1.1"
#define APA_HEADER_SIZE 1024
#define APA_MAGIC_OFFSET 0x004
#define APA_ID_OFFSET 0x010
#define APA_MBR_MAGIC_OFFSET 0x100
#define APA_OSD_START_OFFSET 0x130
#define APA_OSD_SIZE_OFFSET 0x134
#define PC_MBR_SIGNATURE_OFFSET 0x1fe
#define HDIOC_SETOSDMBR_LOCAL 0x6833
#define HDIOC_READSECTOR_LOCAL 0x6836
#define HDIOC_FLUSH_LOCAL 0x4804

/* DMA-safe working buffers used by the pad and fileXio RPC services. */
static unsigned char pad_buffer[256] __attribute__((aligned(64)));
static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char verify_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static unsigned char backup_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));

/*
 * Backup slots are tried in order. Two names per card let us preserve an
 * existing backup instead of silently replacing evidence that may be needed
 * to restore another disk.
 */
static const char *const backup_paths[] = {
    "mc0:/FHDBMBR.BIN",
    "mc0:/FHDBMBR2.BIN",
    "mc1:/FHDBMBR.BIN",
    "mc1:/FHDBMBR2.BIN"
};

#define BACKUP_PATH_COUNT (sizeof(backup_paths) / sizeof(backup_paths[0]))
#define BACKUP_NOT_TRIED 999999
#define BACKUP_OCCUPIED  999998

static int backup_read_result[BACKUP_PATH_COUNT];
static int backup_write_result[BACKUP_PATH_COUNT];
static int backup_verify_result[BACKUP_PATH_COUNT];

/* IRX modules embedded into the final ELF by the Makefile's bin2c rules. */
extern unsigned char iomanX_irx[];
extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];
extern unsigned int size_fileXio_irx;
extern unsigned char poweroff_irx[];
extern unsigned int size_poweroff_irx;
extern unsigned char ps2dev9_irx[];
extern unsigned int size_ps2dev9_irx;
extern unsigned char ps2atad_irx[];
extern unsigned int size_ps2atad_irx;
extern unsigned char ps2hdd_irx[];
extern unsigned int size_ps2hdd_irx;

/* Argument layout expected by HDIOC_READSECTOR. */
typedef struct {
    u32 lba;
    u32 size;
} raw_transfer_t;

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

/*
 * Accept only a normal APA master header with all three identifying strings
 * and a valid checksum. A random sector zero should never reach the write path.
 */
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

/*
 * Confirm that a backup belongs to the currently connected disk. The checksum
 * and the two OSD fields are ignored because those are expected to change when
 * disabling or restoring the bootstrap.
 */
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

/* Reset the IOP and enable loading the embedded homebrew IRX modules. */
static void reset_iop(void)
{
    sceSifInitRpc(0);
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
    sceSifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

/* Execute an embedded IRX and normalize every non-negative module ID to success. */
static int exec_irx(void *buffer, unsigned int size)
{
    int result = SifExecModuleBuffer(buffer, size, 0, NULL, NULL);
    return result < 0 ? result : 0;
}

/* Load filesystem, controller, memory-card, power, DEV9, ATA, and APA drivers. */
static int load_modules(void)
{
    if (exec_irx(iomanX_irx, size_iomanX_irx) < 0) return -1;
    if (exec_irx(fileXio_irx, size_fileXio_irx) < 0) return -2;
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0) return -3;
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0) return -4;
    if (SifLoadModule("rom0:MCMAN", 0, NULL) < 0) return -5;
    if (SifLoadModule("rom0:MCSERV", 0, NULL) < 0) return -6;
    if (exec_irx(poweroff_irx, size_poweroff_irx) < 0) return -7;
    if (exec_irx(ps2dev9_irx, size_ps2dev9_irx) < 0) return -8;
    if (exec_irx(ps2atad_irx, size_ps2atad_irx) < 0) return -9;
    if (exec_irx(ps2hdd_irx, size_ps2hdd_irx) < 0) return -10;
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

/* Initialize the first controller using the statically allocated DMA buffer. */
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
    u32 previous = 0;

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

/* Read sectors 0 and 1, which together contain the complete APA master header. */
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

/*
 * Update only osdStart and osdSize through ps2hdd. The driver recalculates the
 * APA checksum; an explicit flush requests that cached writes reach the disk.
 */
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

/* Write one complete backup using IOP flags (not similarly named POSIX flags). */
static int write_whole_file(const char *path, const void *data, int size)
{
    int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    int written;

    if (fd < 0)
        return fd;
    written = fileXioWrite(fd, data, size);
    fileXioClose(fd);
    return written == size ? 0 : -1;
}

/* Read exactly the requested number of bytes or reject the file as incomplete. */
static int read_whole_file(const char *path, void *data, int size)
{
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    int received;

    if (fd < 0)
        return fd;
    received = fileXioRead(fd, data, size);
    fileXioClose(fd);
    return received == size ? 0 : -1;
}

/*
 * Reuse an identical backup when possible; otherwise use the first free slot.
 * A newly written file must survive a full read-back and byte-for-byte compare
 * before the caller is allowed to offer the HDD modification.
 */
static const char *save_backup(void)
{
    unsigned int i;

    for (i = 0; i < BACKUP_PATH_COUNT; i++) {
        backup_read_result[i] = BACKUP_NOT_TRIED;
        backup_write_result[i] = BACKUP_NOT_TRIED;
        backup_verify_result[i] = BACKUP_NOT_TRIED;
    }

    for (i = 0; i < BACKUP_PATH_COUNT; i++) {
        backup_read_result[i] =
            read_whole_file(backup_paths[i], backup_buffer, APA_HEADER_SIZE);
        if (backup_read_result[i] == 0) {
            if (is_standard_apa_header(backup_buffer) &&
                headers_match_same_disk(header_buffer, backup_buffer) &&
                read_le32(backup_buffer + APA_OSD_START_OFFSET) ==
                    read_le32(header_buffer + APA_OSD_START_OFFSET) &&
                read_le32(backup_buffer + APA_OSD_SIZE_OFFSET) ==
                    read_le32(header_buffer + APA_OSD_SIZE_OFFSET))
                return backup_paths[i];
            backup_write_result[i] = BACKUP_OCCUPIED;
            continue;
        }

        backup_write_result[i] =
            write_whole_file(backup_paths[i], header_buffer, APA_HEADER_SIZE);
        if (backup_write_result[i] == 0) {
            backup_verify_result[i] =
                read_whole_file(backup_paths[i], backup_buffer, APA_HEADER_SIZE);
            if (backup_verify_result[i] == 0) {
                if (memcmp(header_buffer, backup_buffer, APA_HEADER_SIZE) == 0)
                    return backup_paths[i];
                backup_verify_result[i] = -1;
            }
        }
    }
    return NULL;
}

/* Locate a valid, enabled backup that matches the currently connected disk. */
static const char *load_backup(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(backup_paths) / sizeof(backup_paths[0]); i++) {
        if (read_whole_file(backup_paths[i], backup_buffer, APA_HEADER_SIZE) == 0 &&
            is_standard_apa_header(backup_buffer) &&
            headers_match_same_disk(header_buffer, backup_buffer) &&
            read_le32(backup_buffer + APA_OSD_START_OFFSET) != 0 &&
            read_le32(backup_buffer + APA_OSD_SIZE_OFFSET) != 0)
            return backup_paths[i];
    }
    return NULL;
}

/* Display a fatal diagnostic and keep the machine alive until safe shutdown. */
static void fatal_screen(const char *message, int code)
{
    scr_clear();
    scr_printf("FHDB Bootstrap Manager v%s\n\n", APP_VERSION);
    scr_printf("ERROR: %s\n", message);
    scr_printf("Code: %d\n\n", code);
    scr_printf("Press TRIANGLE to power off.\n");
    while (!(wait_for_press() & PAD_TRIANGLE)) {}
    poweroffShutdown();
    SleepThread();
}

/* Show per-path memory-card diagnostics while explicitly confirming HDD safety. */
static void backup_error_screen(void)
{
    unsigned int i;

    scr_clear();
    scr_printf("FHDB Bootstrap Manager v%s\n\n", APP_VERSION);
    scr_printf("ERROR: Memory-card backup failed.\n");
    scr_printf("HDD was NOT modified.\n\n");
    for (i = 0; i < BACKUP_PATH_COUNT; i++) {
        scr_printf("%s\n", backup_paths[i]);
        scr_printf(" read:%d write:%d verify:%d\n",
                   backup_read_result[i], backup_write_result[i],
                   backup_verify_result[i]);
    }
    scr_printf("\nPhotograph this screen. TRIANGLE = power off.\n");
    while (!(wait_for_press() & PAD_TRIANGLE)) {}
    poweroffShutdown();
    SleepThread();
}

/* Re-read sector zero and confirm both the APA checksum and requested values. */
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
    return 0;
}

/*
 * Main state machine:
 *   1. initialize hardware and validate the disk;
 *   2. display the current bootstrap state;
 *   3. back up and disable, or load a backup and restore;
 *   4. verify every HDD update before reporting success.
 */
int main(void)
{
    int result;
    int hdd_status;
    u32 start;
    u32 size;
    u32 pressed;
    const char *backup_path;

    /* Bring up the display first so early initialization failures remain visible. */
    init_scr();
    scr_printf("FHDB Bootstrap Manager v%s\n", APP_VERSION);
    scr_printf("Initializing...\n");

    reset_iop();
    result = load_modules();
    if (result < 0)
        fatal_screen("Could not load required IOP modules.", result);
    fileXioInit();
    poweroffInit();
    if (init_pad() < 0)
        fatal_screen("Controller 1 is not available.", -100);

    /* Refuse to continue unless ps2hdd recognizes a healthy APA device. */
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

    start = read_le32(header_buffer + APA_OSD_START_OFFSET);
    size = read_le32(header_buffer + APA_OSD_SIZE_OFFSET);

    /* The menu is intentionally tiny: inspect, disable, restore, or leave. */
    for (;;) {
        scr_clear();
        scr_printf("FHDB Bootstrap Manager v%s\n\n", APP_VERSION);
        scr_printf("APA header: valid\n");
        scr_printf("osdStart : 0x%08x\n", (unsigned int)start);
        scr_printf("osdSize  : 0x%08x\n\n", (unsigned int)size);

        if (start != 0 || size != 0) {
            scr_printf("HDD bootstrap is ENABLED.\n\n");
            scr_printf("X       Disable bootstrap (backup required)\n");
        } else {
            scr_printf("HDD bootstrap is DISABLED.\n\n");
            scr_printf("SQUARE  Restore bootstrap from MC backup\n");
        }
        scr_printf("TRIANGLE Power off without changes\n");

        pressed = wait_for_press();
        if (pressed & PAD_TRIANGLE) {
            poweroffShutdown();
            SleepThread();
        }

        if ((pressed & PAD_CROSS) && (start != 0 || size != 0)) {
            /* No backup means no confirmation screen and absolutely no HDD write. */
            backup_path = save_backup();
            if (backup_path == NULL)
                backup_error_screen();

            scr_clear();
            scr_printf("Backup saved to %s\n\n", backup_path);
            scr_printf("This disables only the MBR program pointer.\n");
            scr_printf("Partitions and games are not deleted.\n\n");
            scr_printf("Hold L1+R1 and press X to confirm.\n");
            scr_printf("Press TRIANGLE to cancel.\n");

            /* Require a three-button chord to make accidental confirmation unlikely. */
            for (;;) {
                struct padButtonStatus buttons;
                if (wait_pad_ready() == 0 && padRead(0, 0, &buttons) != 0) {
                    u32 held = 0xffffu ^ buttons.btns;
                    if (held & PAD_TRIANGLE)
                        break;
                    if ((held & (PAD_L1 | PAD_R1 | PAD_CROSS)) ==
                        (PAD_L1 | PAD_R1 | PAD_CROSS)) {
                        /* Zeroing both fields makes the ROM ignore the HDD payload. */
                        result = set_osd_mbr(0, 0);
                        if (result < 0)
                            fatal_screen("HDIOC_SETOSDMBR failed.", result);
                        result = verify_values(0, 0);
                        if (result < 0)
                            fatal_screen("Write verification failed. Keep the backup.", result);
                        start = 0;
                        size = 0;
                        scr_clear();
                        scr_printf("Bootstrap disabled and verified.\n\n");
                        scr_printf("Backup: %s\n", backup_path);
                        scr_printf("Press TRIANGLE to power off.\n");
                        while (!(wait_for_press() & PAD_TRIANGLE)) {}
                        poweroffShutdown();
                        SleepThread();
                    }
                }
                DelayThread(16000);
            }
        }

        if ((pressed & PAD_SQUARE) && start == 0 && size == 0) {
            /* Restoration uses only a valid backup tied to this disk's header. */
            backup_path = load_backup();
            if (backup_path == NULL)
                fatal_screen("No 1024-byte backup found on mc0 or mc1.", -104);
            if (!is_standard_apa_header(backup_buffer))
                fatal_screen("Backup is not a valid APA __mbr header.", -105);
            start = read_le32(backup_buffer + APA_OSD_START_OFFSET);
            size = read_le32(backup_buffer + APA_OSD_SIZE_OFFSET);
            if (start == 0 || size == 0)
                fatal_screen("Backup contains a disabled bootstrap.", -106);

            scr_clear();
            scr_printf("Restore from %s\n", backup_path);
            scr_printf("osdStart: 0x%08x\n", (unsigned int)start);
            scr_printf("osdSize : 0x%08x\n\n", (unsigned int)size);
            scr_printf("Hold L1+R1 and press SQUARE to confirm.\n");
            scr_printf("Press TRIANGLE to cancel.\n");

            /* Restoring is protected by a separate chord to distinguish the action. */
            for (;;) {
                struct padButtonStatus buttons;
                if (wait_pad_ready() == 0 && padRead(0, 0, &buttons) != 0) {
                    u32 held = 0xffffu ^ buttons.btns;
                    if (held & PAD_TRIANGLE) {
                        start = 0;
                        size = 0;
                        break;
                    }
                    if ((held & (PAD_L1 | PAD_R1 | PAD_SQUARE)) ==
                        (PAD_L1 | PAD_R1 | PAD_SQUARE)) {
                        result = set_osd_mbr(start, size);
                        if (result < 0)
                            fatal_screen("Bootstrap restore failed.", result);
                        result = verify_values(start, size);
                        if (result < 0)
                            fatal_screen("Restore verification failed.", result);
                        scr_clear();
                        scr_printf("Bootstrap restored and verified.\n\n");
                        scr_printf("Press TRIANGLE to power off.\n");
                        while (!(wait_for_press() & PAD_TRIANGLE)) {}
                        poweroffShutdown();
                        SleepThread();
                    }
                }
                DelayThread(16000);
            }
        }
    }
}
