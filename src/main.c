/*
 * PS2 HDD Bootstrap Manager application composition root.
 *
 * main.c owns startup, normal APA admission and composition. Expensive
 * boot-chain evidence collection is deliberately deferred until Diagnostics
 * (or a later workflow) requests it; startup should establish that the HDD is
 * admissible and hand control to the dashboard as quickly as practical.
 *
 * Normal Torii-compatible operations never raw-write APA master sectors.
 * Michishirube's exceptional recovery writers are separately planner-gated,
 * externally snapshotted, read-back verified and followed by a mandatory
 * restart before normal ps2hdd use continues.
 */

#include <kernel.h>
#include <timer.h>
#include <debug.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>

#include <stdio.h>
#include <string.h>

#include "apa.h"
#include "app_identity.h"
#include "app_ui_ps2.h"
#include "bootstrap_signing.h"
#include "boot_chain.h"
#include "gs_ui_ps2.h"
#include "hdd_read.h"
#include "manager_menu_ps2.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"
#include "version.h"

extern void __real_scr_clear(void);
extern void __real_scr_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

static unsigned char header_buffer[APA_HEADER_SIZE] __attribute__((aligned(64)));
static boot_chain_info_t boot_chain;

typedef struct {
    unsigned int iop_reset_ms;
    unsigned int modules_ms;
    unsigned int services_ms;
    unsigned int pad_ms;
    unsigned int hdd_status_ms;
    unsigned int header_ms;
    unsigned int total_ms;
} startup_timing_t;

static int read_header(unsigned char *destination)
{
    return hdd_read_raw_sectors(0, 2, destination);
}

static unsigned int elapsed_ms(u64 start, u64 end)
{
    u32 seconds = 0;
    u32 microseconds = 0;

    TimerBusClock2USec(end - start, &seconds, &microseconds);
    return seconds * 1000u + microseconds / 1000u;
}

/* Populate only cheap evidence needed for sensible pre-scan UI/rescue metadata.
 * Full memory-card/PFS boot-chain discovery is intentionally lazy. */
static void mark_boot_chain_pending(boot_chain_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->skip_hdd[0] = -1;
    info->skip_hdd[1] = -1;
    info->skip_hdd[2] = -1;
    info->payload_read_result = -1;
    info->payload_kelf_result = -1;
    info->sysconf_mount_result = -1;
    info->system_mount_result = -1;
    read_romver(info->romver);
    expected_system_folder(info->romver, info->expected_system_folder,
                           sizeof(info->expected_system_folder));
    snprintf(info->family, sizeof(info->family), "Not scanned");
    snprintf(info->confidence, sizeof(info->confidence), "pending");
    snprintf(info->next_stage, sizeof(info->next_stage),
             "Run Diagnostics for boot-chain evidence");
}

int main(int argc, char **argv)
{
    startup_timing_t timing;
    u64 total_start;
    u64 stage_start;
    u64 stage_end;
    int result;
    int hdd_status;

    memset(&timing, 0, sizeof(timing));
    total_start = GetTimerSystemTime();
    select_launch_storage(argc, argv);

    /* Hardware validation showed that libdebug's CRT/read-circuit bootstrap is
       reliable on the physical console while a standalone 640x448 graph setup
       produced a black screen. Keep init_scr() only for proven video hardware
       initialization. Linker wrappers route all subsequent scr_clear/printf
       calls into gs_ui_ps2, so libdebug does not render the application UI. */
    init_scr();
    result = gs_ui_initialize();
    if (result < 0) {
        /* The hardware bootstrap is alive here, so use the real debug renderer
           only as the last-resort initialization failure path. */
        __real_scr_clear();
        __real_scr_printf("GS UI initialization failed: %d\n", result);
        SleepThread();
    }
    scr_clear();
    scr_printf(APP_NAME " v%s\n", APP_VERSION);
    scr_printf("Initializing IOP...\n");

    stage_start = GetTimerSystemTime();
    reset_iop();
    stage_end = GetTimerSystemTime();
    timing.iop_reset_ms = elapsed_ms(stage_start, stage_end);

    scr_printf("Loading embedded drivers...\n");
    stage_start = GetTimerSystemTime();
    result = load_modules();
    stage_end = GetTimerSystemTime();
    timing.modules_ms = elapsed_ms(stage_start, stage_end);
    if (result < 0) {
        scr_printf("ERROR: Could not load required IOP modules.\n");
        scr_printf("Code: %d\nPower off with the console button.\n", result);
        SleepThread();
    }

    scr_printf("Initializing services...\n");
    stage_start = GetTimerSystemTime();
    fileXioInit();
    poweroffInit();
    bootstrap_signing_init();
    stage_end = GetTimerSystemTime();
    timing.services_ms = elapsed_ms(stage_start, stage_end);

    scr_printf("Initializing controller...\n");
    stage_start = GetTimerSystemTime();
    result = init_pad();
    stage_end = GetTimerSystemTime();
    timing.pad_ms = elapsed_ms(stage_start, stage_end);
    if (result < 0) {
        scr_printf("ERROR: Controller 1 is not available.\n");
        scr_printf("Power off with the console button.\n");
        SleepThread();
    }

    scr_printf("Checking HDD...\n");
    stage_start = GetTimerSystemTime();
    /* hdd_recovery_wrap intercepts only this first HDIOC_STATUS. A readable
       but damaged master can enter the narrowly guarded startup recovery path
       before normal admission rejects it. */
    hdd_status = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    stage_end = GetTimerSystemTime();
    timing.hdd_status_ms = elapsed_ms(stage_start, stage_end);
    if (hdd_status != 0)
        app_ui_fatal_screen(
            "HDD is missing, locked, or not valid APA.", hdd_status);

    scr_printf("Reading APA master...\n");
    stage_start = GetTimerSystemTime();
    result = read_header(header_buffer);
    stage_end = GetTimerSystemTime();
    timing.header_ms = elapsed_ms(stage_start, stage_end);
    if (result < 0)
        app_ui_fatal_screen("Could not read sectors 0-1.", result);
    if (!is_standard_apa_header(header_buffer))
        app_ui_fatal_screen("Invalid APA __mbr header or checksum.", -101);
    if (is_hybrid_gpt(header_buffer))
        app_ui_fatal_screen("Hybrid APA/GPT layout is not supported.", -102);

    mark_boot_chain_pending(&boot_chain);
    timing.total_ms = elapsed_ms(total_start, GetTimerSystemTime());

    session_log_line("Session started: %s v%s; launch storage=%s", APP_NAME,
                     APP_VERSION,
                     storage_targets[storage_selected()].name);
    session_log_line("APA header valid; osdStart=0x%08x; osdSize=0x%08x",
                     (unsigned int)read_le32(
                         header_buffer + APA_OSD_START_OFFSET),
                     (unsigned int)read_le32(
                         header_buffer + APA_OSD_SIZE_OFFSET));
    session_log_line(
        "Startup timing ms: iop=%u modules=%u services=%u pad=%u hdd_status=%u header=%u total=%u",
        timing.iop_reset_ms, timing.modules_ms, timing.services_ms,
        timing.pad_ms, timing.hdd_status_ms, timing.header_ms,
        timing.total_ms);
    session_log_line(
        "Full boot-chain diagnostics deferred until requested; dashboard is ready");

    manager_menu_run(header_buffer, &boot_chain);
    return 0;
}
