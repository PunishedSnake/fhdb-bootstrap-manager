#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include <stdint.h>
#include <stdio.h>

#include "disk_status_ps2.h"
#include "gs_ui_ps2.h"
#include "platform.h"

#define STATUS_STACK_DEPTH 6u
/* Raw forensic scanning can generate ~15k READ events on a 2 TB disk. Waiting
 * for VBlank on every one would serialize the scan to display refresh speed.
 * Keep the latest telemetry authoritative, but only present one out of each 32
 * high-rate reads. */
#define STATUS_READ_RENDER_DIVISOR 32u

/* Bulk HDL transfers are 64 KiB per event. Rendering every block can insert a
 * framebuffer/VBlank wait between otherwise back-to-back USB requests, which
 * is exactly the kind of tiny UI tax that becomes a large throughput loss on a
 * 12 Mbit/s bus. Keep destructive metadata writes immediate, but coalesce only
 * large semantic streaming events. Copy remains visibly live at roughly every
 * 256 KiB; HDD-only verification updates every 2 MiB. The final event is
 * always rendered regardless of the divisor. */
#define STATUS_STREAM_MIN_SECTORS 64u
#define STATUS_WRITE_RENDER_DIVISOR 4u
#define STATUS_VERIFY_RENDER_DIVISOR 32u

static const char *operation_stack[STATUS_STACK_DEPTH];
static const char *phase_stack[STATUS_STACK_DEPTH];
static const char *location_stack[STATUS_STACK_DEPTH];
static unsigned char write_intent_stack[STATUS_STACK_DEPTH];
static unsigned int status_depth;
static unsigned int status_overflow_depth;
static unsigned int read_events_since_render;
static unsigned int stream_events_since_render;

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

static const char *automatic_location(unsigned int lba,
                                      unsigned int sectors,
                                      char buffer[96])
{
    if (sectors == 0u)
        return "Waiting for the next HDD command";
    if (lba == 0u && sectors <= 2u)
        return "APA master header / sectors 0-1";
    if (lba < 0x2000u) {
        snprintf(buffer, 96, "Reserved APA metadata area near LBA 0x%08x", lba);
        return buffer;
    }
    snprintf(buffer, 96, "Physical HDD data near LBA 0x%08x", lba);
    return buffer;
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
    const char *location = status_depth != 0
                               ? location_stack[status_depth - 1u]
                               : NULL;
    int write_intent = status_depth != 0 &&
                       write_intent_stack[status_depth - 1u] != 0u;
    const char *io_label = kind_name(kind);
    char automatic[96];
    uint32_t progress_current = current;
    uint32_t progress_total = total;
    unsigned int percent = 0;
    int write_sensitive;

    /* current/total are semantic task progress supplied by the caller. Never
     * substitute a physical LBA divided by HDD size: APA is a linked list, not
     * a sequential workload, so the last valid node can live at 66% (or any
     * other position) of the disk while the operation is actually complete. */
    if (progress_total != 0) {
        uint64_t scaled = (uint64_t)progress_current * 100u;
        percent = (unsigned int)(scaled / progress_total);
        if (percent > 100u)
            percent = 100u;
    }

    if (location == NULL || location[0] == '\0')
        location = automatic_location(lba, sectors, automatic);

    if (write_intent && kind == DISK_STATUS_READ)
        io_label = "READ / WRITE PREFLIGHT";
    else if (write_intent && kind == DISK_STATUS_SCAN)
        io_label = "WRITE PREP";

    write_sensitive = write_intent || kind == DISK_STATUS_WRITE ||
                      kind == DISK_STATUS_FLUSH ||
                      kind == DISK_STATUS_POINTER;

    /* The GS frontend renders this complete status view into its back buffer
     * and owns the framebuffer swap. High-rate I/O coalescing happens before
     * this function so presentation cannot become part of the storage timing
     * loop itself. */
    gs_ui_render_disk_status(operation,
                             phase != NULL && phase[0] != '\0'
                                 ? phase : kind_name(kind),
                             location,
                             io_label, percent,
                             (unsigned int)progress_current,
                             (unsigned int)progress_total,
                             lba, sectors, write_sensitive);
}

static void reset_render_counters(void)
{
    read_events_since_render = 0;
    stream_events_since_render = 0;
}

void disk_status_begin_at(const char *operation, const char *phase,
                          const char *location)
{
    unsigned int slot;

    if (status_depth < STATUS_STACK_DEPTH) {
        /* The controller's ANALOG lamp is our physical I/O activity LED. Keep
         * it tied to the same scoped status lifetime used by every HDD path,
         * including HDL catalogue, preflight, copy, verify and cleanup. The
         * platform helper is nesting-safe, so older callers that explicitly
         * bracketed an operation remain harmlessly compatible. */
        pad_activity_begin();
        slot = status_depth++;
    } else {
        status_overflow_depth++;
        slot = STATUS_STACK_DEPTH - 1u;
    }
    operation_stack[slot] = operation;
    phase_stack[slot] = phase;
    location_stack[slot] = location;
    write_intent_stack[slot] = 0u;
    reset_render_counters();
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_begin(const char *operation, const char *phase)
{
    disk_status_begin_at(operation, phase, NULL);
}

void disk_status_phase(const char *phase)
{
    if (status_depth == 0)
        return;
    phase_stack[status_depth - 1u] = phase;
    reset_render_counters();
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_location(const char *location)
{
    if (status_depth == 0)
        return;
    location_stack[status_depth - 1u] = location;
    reset_render_counters();
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_phase_at(const char *phase, const char *location)
{
    if (status_depth == 0)
        return;
    phase_stack[status_depth - 1u] = phase;
    location_stack[status_depth - 1u] = location;
    reset_render_counters();
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_set_write_intent(int armed)
{
    if (status_depth == 0)
        return;
    write_intent_stack[status_depth - 1u] = armed ? 1u : 0u;
    reset_render_counters();
    render(DISK_STATUS_SCAN, 0, 0, 0, 0);
}

void disk_status_io(disk_status_kind_t kind, unsigned int lba,
                    unsigned int sectors, unsigned int current,
                    unsigned int total)
{
    unsigned int divisor = 0;
    int final_event = total != 0u && current >= total;

    /* A bare raw read with no semantic progress used to overwrite useful UI
     * with a fake HDD-position percentage. Leave the caller's activity page on
     * screen instead. Scoped operations still get full LBA telemetry. */
    if (status_depth == 0 && current == 0u && total == 0u)
        return;

    if (kind == DISK_STATUS_READ) {
        read_events_since_render++;
        if (read_events_since_render < STATUS_READ_RENDER_DIVISOR)
            return;
        read_events_since_render = 0;
        stream_events_since_render = 0;
    } else if (total != 0u && sectors >= STATUS_STREAM_MIN_SECTORS &&
               (kind == DISK_STATUS_WRITE || kind == DISK_STATUS_VERIFY)) {
        divisor = kind == DISK_STATUS_WRITE ? STATUS_WRITE_RENDER_DIVISOR
                                             : STATUS_VERIFY_RENDER_DIVISOR;
        stream_events_since_render++;
        if (!final_event && stream_events_since_render < divisor)
            return;
        stream_events_since_render = 0;
        read_events_since_render = 0;
    } else {
        reset_render_counters();
    }
    render(kind, lba, sectors, current, total);
}

void disk_status_end(void)
{
    reset_render_counters();
    if (status_overflow_depth != 0) {
        status_overflow_depth--;
        return;
    }
    if (status_depth != 0) {
        write_intent_stack[status_depth - 1u] = 0u;
        status_depth--;
        pad_activity_end();
    }
}
