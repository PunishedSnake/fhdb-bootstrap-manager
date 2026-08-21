#include <debug.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include <stdint.h>
#include <string.h>

#include "app_identity.h"
#include "disk_status_ps2.h"
#include "version.h"

#define STATUS_BAR_WIDTH 28u
#define STATUS_STACK_DEPTH 6u
#define STATUS_READ_REDRAW_EVERY 16u

static const char *operation_stack[STATUS_STACK_DEPTH];
static const char *phase_stack[STATUS_STACK_DEPTH];
static unsigned int status_depth;
static unsigned int event_counter;
static uint32_t cached_total_sectors;
static int total_sectors_known;

static const char *kind_name(disk_status_kind_t kind)
{
    switch (kind) {
        case DISK_STATUS_READ: return "READ";
        case DISK_STATUS_WRITE: return "WRITE";
        case DISK_STATUS_VERIFY: return "VERIFY";
        case DISK_STATUS_FLUSH: return "FLUSH";
        case DISK_STATUS_POINTER: return "POINTER UPDATE";
        case DISK_STATUS_SCAN: return "SCAN";
        default: return "HDD I/O";
    }
}

static void ensure_total_sectors(void)
{
    int value;

    if (total_sectors_known)
        return;
    value = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR,
                          NULL, 0, NULL, 0);
    if (!(value < 0 && value > -4096) && (uint32_t)value >= 2u) {
        cached_total_sectors = (uint32_t)value;
        total_sectors_known = 1;
    }
}

static void print_bar(unsigned int percent)
{
    unsigned int filled = (percent * STATUS_BAR_WIDTH) / 100u;
    unsigned int i;

    scr_printf("[");
    for (i = 0; i < STATUS_BAR_WIDTH; i++)
        scr_printf(i < filled ? "#" : "-");
    scr_printf("] %3u%%\n", percent);
}

static void render(disk_status_kind_t kind, unsigned int lba,
                   unsigned int sectors, unsigned int current,
                   unsigned int total)
{
    const char *operation = status_depth != 0
                                ? operation_stack[status_depth - 1u]
                                : "Raw HDD activity";
    const char *phase = status_depth != 0
                            ? phase_stack[status_depth - 1u]
                            : NULL;
    uint32_t progress_current = current;
    uint32_t progress_total = total;
    unsigned int percent = 0;

    ensure_total_sectors();
    if (progress_total == 0 && total_sectors_known) {
        progress_current = lba;
        progress_total = cached_total_sectors;
    }
    if (progress_total != 0) {
        uint64_t scaled = (uint64_t)progress_current * 100u;
        percent = (unsigned int)(scaled / progress_total);
        if (percent > 100u)
            percent = 100u;
    }

    scr_clear();
    scr_printf(APP_NAME " v%s\n", APP_VERSION);
    scr_printf("LIVE HDD MONITOR\n\n");
    scr_printf("Operation: %s\n", operation != NULL ? operation : "HDD activity");
    scr_printf("Action   : %s\n", phase != NULL && phase[0] != '\0'
                                      ? phase : kind_name(kind));
    scr_printf("I/O      : %s\n", kind_name(kind));
    if (progress_total != 0) {
        print_bar(percent);
        scr_printf("Position : 0x%08x / 0x%08x sectors\n",
                   progress_current, progress_total);
    } else {
        scr_printf("[disk position: total size unavailable]\n");
    }
    if (sectors != 0) {
        scr_printf("Sector   : 0x%08x", lba);
        if (sectors > 1u)
            scr_printf(" .. 0x%08x", lba + sectors - 1u);
        scr_printf("  (%u sector%s)\n", sectors, sectors == 1u ? "" : "s");
    }
    scr_printf("\nDo not reset/remove power during WRITE/FLUSH.\n");
}

void disk_status_begin(const char *operation, const char *phase)
{
    unsigned int slot = status_depth < STATUS_STACK_DEPTH
                            ? status_depth : STATUS_STACK_DEPTH - 1u;

    if (status_depth < STATUS_STACK_DEPTH)
        status_depth++;
    operation_stack[slot] = operation;
    phase_stack[slot] = phase;
    event_counter = 0;
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_phase(const char *phase)
{
    if (status_depth == 0)
        return;
    phase_stack[status_depth - 1u] = phase;
    event_counter = 0;
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_io(disk_status_kind_t kind, unsigned int lba,
                    unsigned int sectors, unsigned int current,
                    unsigned int total)
{
    int force = kind != DISK_STATUS_READ && kind != DISK_STATUS_SCAN;

    event_counter++;
    if (!force && (event_counter % STATUS_READ_REDRAW_EVERY) != 0u &&
        current != 0u && current != total)
        return;
    render(kind, lba, sectors, current, total);
}

void disk_status_end(void)
{
    if (status_depth != 0)
        status_depth--;
    event_counter = 0;
}
