#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include <stdint.h>

#include "disk_status_ps2.h"
#include "gs_ui_ps2.h"

#define STATUS_STACK_DEPTH 6u

static const char *operation_stack[STATUS_STACK_DEPTH];
static const char *phase_stack[STATUS_STACK_DEPTH];
static unsigned int status_depth;
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
    int write_sensitive;

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

    write_sensitive = kind == DISK_STATUS_WRITE ||
                      kind == DISK_STATUS_FLUSH ||
                      kind == DISK_STATUS_POINTER;
    gs_ui_render_disk_status(operation,
                             phase != NULL && phase[0] != '\0'
                                 ? phase : kind_name(kind),
                             kind_name(kind), percent,
                             (unsigned int)progress_current,
                             (unsigned int)progress_total,
                             lba, sectors, write_sensitive);
}

void disk_status_begin(const char *operation, const char *phase)
{
    unsigned int slot = status_depth < STATUS_STACK_DEPTH
                            ? status_depth : STATUS_STACK_DEPTH - 1u;

    if (status_depth < STATUS_STACK_DEPTH)
        status_depth++;
    operation_stack[slot] = operation;
    phase_stack[slot] = phase;
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_phase(const char *phase)
{
    if (status_depth == 0)
        return;
    phase_stack[status_depth - 1u] = phase;
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_io(disk_status_kind_t kind, unsigned int lba,
                    unsigned int sectors, unsigned int current,
                    unsigned int total)
{
    /* GS sprites + one GIF DMA packet are cheap enough to publish every event.
       There is intentionally no presentation throttle here. */
    render(kind, lba, sectors, current, total);
}

void disk_status_end(void)
{
    if (status_depth != 0)
        status_depth--;
}
