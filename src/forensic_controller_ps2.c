/*
 * PS2 UI/controller for degraded read-only APA reconstruction and explicitly
 * authorized topology repair. The scanner is portable; this file supplies raw
 * reads, report persistence and the snapshot/write gate. High-frequency disk
 * presentation is owned by disk_status_ps2/gs_ui_ps2 at the raw transport.
 */

#include <tamtypes.h>
#include <debug.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "apa_forensic.h"
#include "app_ui_ps2.h"
#include "forensic_controller_ps2.h"
#include "forensic_snapshot.h"
#include "hdd_forensic_repair_ps2.h"
#include "hdd_read.h"
#include "platform.h"
#include "session_log.h"
#include "storage.h"

/* A 512-node real disk already overflowed the old 64 KiB report near node 315.
 * 512 KiB is large enough for the expanded 2048-node laboratory budget while
 * remaining a bounded static buffer on the EE. */
#define FORENSIC_REPORT_BYTES (512u * 1024u)

static apa_forensic_result_t forensic_scan_result;
static int forensic_scan_valid;
static char forensic_report[FORENSIC_REPORT_BYTES];

static int raw_reader(void *context, uint32_t lba, unsigned int sectors,
                      unsigned char *destination)
{
    (void)context;
    return hdd_read_raw_sectors(lba, sectors, destination);
}

static int get_total_sectors(uint32_t *total_out)
{
    int value;

    value = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR,
                          NULL, 0, NULL, 0);
    /* ps2hdd returns totalLBA through a signed int. Small negative values are
     * IOP errno results; a large negative bit-pattern may represent >1 TiB. */
    if (value < 0 && value > -4096)
        return value;
    *total_out = (uint32_t)value;
    if (*total_out < 2)
        return -1;
    return 0;
}

static int run_scan(void)
{
    uint32_t total_sectors;
    int result;

    result = get_total_sectors(&total_sectors);
    if (result < 0)
        return result;

    pad_activity_begin();
    /* Every physical raw read publishes its exact LBA directly through the
     * GS/GIF live monitor. The portable scanner callback is intentionally NULL:
     * a second presentation callback would duplicate/flicker over the transport
     * telemetry and used to re-enter libdebug's per-character renderer. */
    result = apa_forensic_scan(raw_reader, NULL, total_sectors,
                               NULL, NULL, &forensic_scan_result);
    pad_activity_end();
    if (result < 0) {
        forensic_scan_valid = 0;
        return result;
    }

    forensic_scan_valid = 1;
    session_log_line(
        "APA forensic scan: sectors=0x%08x nodes=%u maps=%u grid=%u refs=%u unreadable=%u truncated=%d",
        (unsigned int)forensic_scan_result.total_sectors,
        forensic_scan_result.node_count, forensic_scan_result.map_count,
        forensic_scan_result.grid_reads, forensic_scan_result.reference_reads,
        forensic_scan_result.unreadable_reads, forensic_scan_result.truncated);
    if (forensic_scan_result.truncated) {
        session_log_line(
            "APA forensic write gate: LOCKED because scan reached node capacity %u",
            APA_FORENSIC_MAX_NODES);
    }
    session_log_flush(storage_selected());
    return 0;
}

static void report_append(unsigned int *used, const char *format, ...)
{
    va_list arguments;
    int written;

    if (*used >= FORENSIC_REPORT_BYTES - 1u)
        return;
    va_start(arguments, format);
    written = vsnprintf(forensic_report + *used,
                        FORENSIC_REPORT_BYTES - *used,
                        format, arguments);
    va_end(arguments);
    if (written < 0)
        return;
    if ((unsigned int)written >= FORENSIC_REPORT_BYTES - *used)
        *used = FORENSIC_REPORT_BYTES - 1u;
    else
        *used += (unsigned int)written;
}

static int save_report(void)
{
    unsigned int used = 0;
    unsigned int i;
    char path[64];

    if (!forensic_scan_valid)
        return -1;

    forensic_report[0] = '\0';
    report_append(&used, "PS2 HDD Bootstrap Manager - APA FORENSIC REPORT\n\n");
    report_append(&used, "Disk sectors : 0x%08x\n",
                  (unsigned int)forensic_scan_result.total_sectors);
    report_append(&used, "Grid step    : 0x%08x\n",
                  (unsigned int)forensic_scan_result.grid_step);
    report_append(&used, "Grid reads   : %u\nReference reads: %u\nUnreadable   : %u\n",
                  forensic_scan_result.grid_reads,
                  forensic_scan_result.reference_reads,
                  forensic_scan_result.unreadable_reads);
    report_append(&used,
                  "Nodes        : %u / %u capacity\nMaps         : %u\nTruncated    : %s\n",
                  forensic_scan_result.node_count, APA_FORENSIC_MAX_NODES,
                  forensic_scan_result.map_count,
                  forensic_scan_result.truncated ? "YES" : "no");
    if (forensic_scan_result.truncated) {
        report_append(&used,
                      "Write planning: LOCKED - scan is incomplete; visible tail is not physical tail\n");
    } else {
        report_append(&used, "Write planning: complete-scan policy applies\n");
    }
    report_append(&used, "\n");

    for (i = 0; i < forensic_scan_result.map_count; i++) {
        const apa_forensic_map_t *map = &forensic_scan_result.maps[i];
        apa_forensic_repair_plan_t plan;

        apa_forensic_build_repair_plan(&forensic_scan_result, i, &plan);
        report_append(&used,
                      "MAP %u: %s\n confidence=%u nodes=%u reciprocal=%u inferred=%u conflicts=%u overlaps=%u repairable=%s\n",
                      i + 1u, apa_forensic_map_name(map->kind),
                      map->confidence, map->node_count,
                      map->reciprocal_links, map->inferred_links,
                      map->conflicts, map->overlaps,
                      map->repairable ? "yes" : "no");
        report_append(&used,
                      " patches=%u corroborated=%u speculative=%u automatic=%s manual=%s\n\n",
                      plan.patch_count, plan.corroborated_count,
                      plan.speculative_count,
                      plan.automatic_safe ? "yes" : "no",
                      plan.manual_allowed ? "yes" : "no");
    }

    report_append(&used, "DISCOVERED HEADERS\n\n");
    for (i = 0; i < forensic_scan_result.node_count; i++) {
        const apa_forensic_node_t *node = &forensic_scan_result.nodes[i];
        report_append(&used,
                      "[%u] LBA=0x%08x id='%s' confidence=%u evidence=0x%08x\n",
                      i, (unsigned int)node->lba, node->id,
                      node->confidence, (unsigned int)node->evidence);
        report_append(&used,
                      " start=0x%08x length=0x%08x prev=0x%08x next=0x%08x type=0x%04x flags=0x%04x\n",
                      (unsigned int)node->start,
                      (unsigned int)node->length,
                      (unsigned int)node->prev,
                      (unsigned int)node->next,
                      (unsigned int)node->type,
                      (unsigned int)node->flags);
        report_append(&used,
                      " main=0x%08x number=%u nsub=%u checksum=%s\n\n",
                      (unsigned int)node->main,
                      (unsigned int)node->number,
                      (unsigned int)node->nsub,
                      node->stored_checksum == node->calculated_checksum
                          ? "OK" : "BAD");
    }

    if (used >= FORENSIC_REPORT_BYTES - 1u)
        strcpy(forensic_report + FORENSIC_REPORT_BYTES - 32u,
               "\n[REPORT TRUNCATED]\n");

    storage_path(path, sizeof(path), storage_selected(), "FORENSIC.TXT");
    pad_activity_begin();
    i = (unsigned int)write_whole_file(path, forensic_report,
                                       (int)strlen(forensic_report));
    pad_activity_end();
    return (int)i;
}

static void show_node(const apa_forensic_map_t *map, unsigned int position)
{
    const apa_forensic_node_t *node =
        &forensic_scan_result.nodes[map->order[position]];

    scr_printf("Node %u/%u  LBA 0x%08x\n", position + 1u,
               map->node_count, (unsigned int)node->lba);
    scr_printf("id      : %s\n", node->id[0] ? node->id : "(empty; normal for APA sub-partitions)");
    scr_printf("conf    : %u%%\n", node->confidence);
    scr_printf("checksum: %s\n",
               node->stored_checksum == node->calculated_checksum
                   ? "OK" : "BAD");
    scr_printf("start   : 0x%08x\n", (unsigned int)node->start);
    scr_printf("length  : 0x%08x\n", (unsigned int)node->length);
    scr_printf("prev    : 0x%08x\n", (unsigned int)node->prev);
    scr_printf("next    : 0x%08x\n", (unsigned int)node->next);
    scr_printf("type/flg: %04x / %04x\n",
               (unsigned int)node->type, (unsigned int)node->flags);
    scr_printf("main/nsub: 0x%08x / %u\n",
               (unsigned int)node->main, (unsigned int)node->nsub);
}

static int repair_plan_screen(unsigned int map_index)
{
    apa_forensic_repair_plan_t plan;
    unsigned int patch_position = 0;
    char snapshot_path[FORENSIC_SNAPSHOT_PATH_SIZE];
    int result;

    if (forensic_scan_result.truncated) {
        scr_clear();
        scr_printf("Forensic topology repair LOCKED\n\n");
        scr_printf("The raw scan reached the %u-node safety capacity.\n",
                   APA_FORENSIC_MAX_NODES);
        scr_printf("This is a PARTIAL map, not the physical end of the APA chain.\n");
        scr_printf("No repair plan can be built from an incomplete scan.\n\n");
        scr_printf("Read-only browsing and FORENSIC.TXT export remain available.\n");
        app_ui_wait_to_return();
        return 0;
    }

    if (apa_forensic_build_repair_plan(&forensic_scan_result, map_index,
                                       &plan) < 0)
        return 0;

    if (plan.patch_count == 0 || !plan.manual_allowed) {
        scr_clear();
        scr_printf("Forensic topology repair\n\n");
        if (plan.patch_count == 0)
            scr_printf("This candidate does not require link changes.\n");
        else
            scr_printf("This candidate is not strong enough for writes.\n");
        scr_printf("Confidence: %u%%\n", plan.confidence);
        scr_printf("No HDD metadata was modified.\n");
        app_ui_wait_to_return();
        return 0;
    }

    for (;;) {
        const apa_forensic_patch_t *patch = &plan.patches[patch_position];
        u32 pressed;

        scr_clear();
        scr_printf("Forensic topology repair plan\n\n");
        scr_printf("Map confidence : %u%%\n", plan.confidence);
        scr_printf("Headers touched: %u\n", plan.patch_count);
        scr_printf("Checksum-backed: %u\n", plan.corroborated_count);
        scr_printf("Heuristic only : %u\n", plan.speculative_count);
        scr_printf("Automatic-safe : %s\n\n",
                   plan.automatic_safe ? "YES" : "no (manual expert path)");
        scr_printf("Patch %u/%u at LBA 0x%08x\n",
                   patch_position + 1u, plan.patch_count,
                   (unsigned int)patch->lba);
        scr_printf("prev  0x%08x -> 0x%08x\n",
                   (unsigned int)patch->old_prev,
                   (unsigned int)patch->new_prev);
        scr_printf("next  0x%08x -> 0x%08x\n",
                   (unsigned int)patch->old_next,
                   (unsigned int)patch->new_next);
        scr_printf("checksum corroboration: %s\n\n",
                   patch->checksum_corroborated ? "YES" : "NO");
        scr_printf("LEFT/RIGHT Inspect patches\n");
        scr_printf("X Prepare verified HDDMETA snapshot\n");
        scr_printf("TRIANGLE Return\n");
        pressed = wait_for_press();
        if ((pressed & PAD_LEFT) && patch_position > 0)
            patch_position--;
        if ((pressed & PAD_RIGHT) && patch_position + 1u < plan.patch_count)
            patch_position++;
        if (pressed & PAD_TRIANGLE)
            return 0;
        if (pressed & PAD_CROSS)
            break;
    }

    scr_clear();
    scr_printf("Saving complete pre-repair APA metadata snapshot...\n");
    pad_activity_begin();
    result = forensic_snapshot_save(storage_selected(), &forensic_scan_result,
                                    &plan, snapshot_path);
    pad_activity_end();
    if (result < 0) {
        scr_clear();
        scr_printf("HDDMETA snapshot FAILED.\n\nCode: %d\n", result);
        scr_printf("No HDD metadata was modified.\n");
        app_ui_wait_to_return();
        return 0;
    }

    scr_clear();
    scr_printf("APA topology repair authorization\n\n");
    scr_printf("Snapshot : %s\n", snapshot_path);
    scr_printf("Confidence: %u%%\n", plan.confidence);
    scr_printf("Headers   : %u\n", plan.patch_count);
    scr_printf("Heuristic : %u\n\n", plan.speculative_count);
    scr_printf("The original 1024-byte image of EVERY touched header\n");
    scr_printf("has been saved and read back before this screen.\n\n");
    if (plan.automatic_safe) {
        scr_printf("Hold L1+R1 and press SQUARE to apply.\n");
        if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE))
            return 0;
    } else {
        scr_printf("EXPERT PLAN: at least one change is heuristic.\n");
        scr_printf("Hold L1+R1+L2+R2 and press SQUARE to apply.\n");
        if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2 | PAD_SQUARE))
            return 0;
    }

    scr_clear();
    scr_printf("Applying APA topology plan...\n");
    scr_printf("Do not reset or remove power.\n");
    pad_activity_begin();
    result = hdd_forensic_repair_apply_verified(&forensic_scan_result, &plan);
    pad_activity_end();
    session_log_line(
        "Forensic topology repair result=%d map=%u confidence=%u patches=%u corroborated=%u speculative=%u snapshot=%s",
        result, map_index, plan.confidence, plan.patch_count,
        plan.corroborated_count, plan.speculative_count, snapshot_path);
    session_log_flush(storage_selected());

    scr_clear();
    if (result < 0) {
        scr_printf("APA topology repair STOPPED/FAILED.\n\nCode: %d\n", result);
        scr_printf("Snapshot: %s\n\n", snapshot_path);
        scr_printf("Some earlier headers may already have been committed.\n");
    } else {
        scr_printf("APA topology repair verified.\n\n");
        scr_printf("Snapshot: %s\n", snapshot_path);
    }
    scr_printf("A restart is mandatory before any further HDD operation.\n");
    scr_printf("Press X to restart.\n");
    while (!(wait_for_press() & PAD_CROSS)) {}
    return 1;
}

static int map_screen(unsigned int map_index)
{
    const apa_forensic_map_t *map = &forensic_scan_result.maps[map_index];
    unsigned int position = 0;

    for (;;) {
        u32 pressed;

        scr_clear();
        scr_printf("DEGRADED READ-ONLY APA MAP\n\n");
        scr_printf("Candidate: %s\n", apa_forensic_map_name(map->kind));
        scr_printf("Confidence: %u%%  nodes:%u\n", map->confidence,
                   map->node_count);
        scr_printf("Reciprocal:%u inferred:%u conflicts:%u overlaps:%u\n",
                   map->reciprocal_links, map->inferred_links,
                   map->conflicts, map->overlaps);
        if (forensic_scan_result.truncated)
            scr_printf("Completeness: PARTIAL - WRITES LOCKED\n\n");
        else
            scr_printf("Completeness: complete\n\n");
        if (map->node_count != 0)
            show_node(map, position);
        scr_printf("\nLEFT/RIGHT Browse recovered headers\n");
        if (forensic_scan_result.truncated)
            scr_printf("SQUARE   Repair plan LOCKED (scan incomplete)\n");
        else
            scr_printf("SQUARE   Inspect/build topology repair plan\n");
        scr_printf("TRIANGLE Back\n");
        pressed = wait_for_press();
        if ((pressed & PAD_LEFT) && position > 0)
            position--;
        if ((pressed & PAD_RIGHT) && position + 1u < map->node_count)
            position++;
        if (pressed & PAD_SQUARE) {
            if (repair_plan_screen(map_index))
                return 1;
        }
        if (pressed & PAD_TRIANGLE)
            return 0;
    }
}

int forensic_controller_screen(void)
{
    unsigned int selected_map = 0;

    if (!forensic_scan_valid) {
        int result = run_scan();
        if (result < 0) {
            scr_clear();
            scr_printf("APA forensic scan failed.\n\nCode: %d\n", result);
            app_ui_wait_to_return();
            return 0;
        }
    }

    for (;;) {
        u32 pressed;

        if (selected_map >= forensic_scan_result.map_count)
            selected_map = 0;
        scr_clear();
        scr_printf("APA FORENSIC / DEGRADED READ-ONLY\n\n");
        scr_printf("Disk sectors : 0x%08x\n",
                   (unsigned int)forensic_scan_result.total_sectors);
        scr_printf("Headers found: %u / %u\n",
                   forensic_scan_result.node_count, APA_FORENSIC_MAX_NODES);
        scr_printf("Maps built   : %u\n", forensic_scan_result.map_count);
        scr_printf("Unreadable   : %u\n", forensic_scan_result.unreadable_reads);
        if (forensic_scan_result.truncated) {
            scr_printf("Scan state   : PARTIAL - CAPACITY REACHED\n");
            scr_printf("Write state  : LOCKED (read-only only)\n\n");
        } else {
            scr_printf("Scan state   : complete\n\n");
        }
        if (forensic_scan_result.map_count != 0) {
            const apa_forensic_map_t *map =
                &forensic_scan_result.maps[selected_map];
            scr_printf("> Candidate %u/%u: %s\n",
                       selected_map + 1u, forensic_scan_result.map_count,
                       apa_forensic_map_name(map->kind));
            scr_printf("  confidence %u%%, nodes %u, conflicts %u, overlaps %u\n\n",
                       map->confidence, map->node_count,
                       map->conflicts, map->overlaps);
            scr_printf("UP/DOWN Select candidate   X Inspect read-only map\n");
        } else {
            scr_printf("No coherent partition map could be built.\n");
        }
        scr_printf("SQUARE Save FORENSIC.TXT\n");
        scr_printf("START  Re-scan raw disk\n");
        scr_printf("TRIANGLE Return\n");

        pressed = wait_for_press();
        if ((pressed & PAD_UP) && forensic_scan_result.map_count != 0)
            selected_map = (selected_map + forensic_scan_result.map_count - 1u) %
                           forensic_scan_result.map_count;
        if ((pressed & PAD_DOWN) && forensic_scan_result.map_count != 0)
            selected_map = (selected_map + 1u) % forensic_scan_result.map_count;
        if ((pressed & PAD_CROSS) && forensic_scan_result.map_count != 0) {
            if (map_screen(selected_map))
                return 1;
        }
        if (pressed & PAD_SQUARE) {
            int result = save_report();
            scr_clear();
            if (result < 0)
                scr_printf("FORENSIC.TXT save failed: %d\n", result);
            else
                scr_printf("FORENSIC.TXT saved to %s.\n",
                           storage_targets[storage_selected()].name);
            app_ui_wait_to_return();
        }
        if (pressed & PAD_START) {
            int result = run_scan();
            if (result < 0) {
                scr_clear();
                scr_printf("Raw re-scan failed: %d\n", result);
                app_ui_wait_to_return();
            }
            selected_map = 0;
        }
        if (pressed & PAD_TRIANGLE)
            return 0;
    }
}
