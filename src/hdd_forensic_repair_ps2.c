/* Guarded PS2 writer for forensic APA topology repair plans. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include <string.h>

#include "apa.h"
#include "app_error.h"
#include "disk_status_ps2.h"
#include "hdd_forensic_repair_ps2.h"
#include "hdd_read.h"

#define HDIOC_WRITESECTOR_LOCAL 0x6837
#define HDIOC_FLUSH_LOCAL 0x4804

typedef struct {
    unsigned int lba;
    unsigned int size;
    unsigned char data[APA_HEADER_SIZE];
} forensic_write_packet_t;

static forensic_write_packet_t write_packet __attribute__((aligned(64)));
static unsigned char source_verify[APA_HEADER_SIZE]
    __attribute__((aligned(64)));
static unsigned char write_verify[APA_HEADER_SIZE]
    __attribute__((aligned(64)));
static unsigned char repaired_header[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

static int fail_forensic(int code, const char *stage)
{
    app_error_record(APP_ERROR_DOMAIN_FORENSIC_REPAIR, code, stage);
    return code;
}

static int patch_header_is_safe(const apa_forensic_result_t *scan,
                                const apa_forensic_patch_t *patch,
                                const unsigned char header[APA_HEADER_SIZE])
{
    const apa_forensic_node_t *node;
    const unsigned int required = APA_FORENSIC_EVIDENCE_MAGIC |
                                  APA_FORENSIC_EVIDENCE_SELF_START |
                                  APA_FORENSIC_EVIDENCE_LENGTH;

    if (patch->node_index >= scan->node_count)
        return 0;
    node = &scan->nodes[patch->node_index];
    if (node->lba != patch->lba ||
        (node->evidence & required) != required)
        return 0;
    if (memcmp(header + APA_MAGIC_OFFSET, "APA\0", 4) != 0 ||
        read_le32(header + APA_START_OFFSET) != patch->lba ||
        read_le32(header + APA_LENGTH_OFFSET) == 0 ||
        read_le32(header) != apa_checksum(header))
        return 0;
    return 1;
}

static int write_one(const apa_forensic_result_t *scan,
                     const apa_forensic_patch_t *patch,
                     unsigned int current, unsigned int total)
{
    const apa_forensic_node_t *node = &scan->nodes[patch->node_index];
    int result;

    /* Refuse to apply an old plan to bytes that changed after the scan. */
    disk_status_phase_at("Source-stability check before metadata write",
                         patch->lba == 0
                             ? "APA master header / sectors 0-1"
                             : "Interior APA partition header at target LBA");
    disk_status_io(DISK_STATUS_VERIFY, patch->lba, 2, current, total);
    result = hdd_read_raw_sectors(patch->lba, 2, source_verify);
    if (result < 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_READBACK_FAILED,
                             "pre-write source read");
    if (memcmp(source_verify, node->header, APA_HEADER_SIZE) != 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_SOURCE_CHANGED,
                             "pre-write source stability compare");

    if (apa_forensic_build_patched_header(scan, patch, repaired_header) < 0 ||
        !patch_header_is_safe(scan, patch, repaired_header))
        return fail_forensic(HDD_FORENSIC_REPAIR_PATCH_INVALID,
                             "build/final-validate patched APA header");

    disk_status_phase_at(
        patch->lba == 0
            ? "Writing APA master LAST (transaction commit point)"
            : "Writing interior APA topology header",
        patch->lba == 0
            ? "APA master header / sectors 0-1"
            : "Interior APA partition header at target LBA");
    disk_status_io(DISK_STATUS_WRITE, patch->lba, 2, current, total);
    write_packet.lba = patch->lba;
    write_packet.size = 2;
    memcpy(write_packet.data, repaired_header, APA_HEADER_SIZE);
    result = fileXioDevctl("hdd0:", HDIOC_WRITESECTOR_LOCAL,
                           &write_packet, sizeof(write_packet), NULL, 0);
    if (result < 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_WRITE_FAILED,
                             "HDIOC_WRITESECTOR APA header");

    disk_status_phase_at("Flushing committed APA header",
                         "ATA cache for currently patched APA header");
    disk_status_io(DISK_STATUS_FLUSH, patch->lba, 2, current, total);
    result = fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL,
                           NULL, 0, NULL, 0);
    if (result < 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_FLUSH_FAILED,
                             "HDIOC_FLUSH APA header");

    disk_status_phase_at("Immediate header read-back verification",
                         patch->lba == 0
                             ? "APA master header / sectors 0-1"
                             : "Interior APA partition header at target LBA");
    disk_status_io(DISK_STATUS_VERIFY, patch->lba, 2, current, total);
    result = hdd_read_raw_sectors(patch->lba, 2, write_verify);
    if (result < 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_READBACK_FAILED,
                             "immediate APA header read-back");
    if (memcmp(write_verify, repaired_header, APA_HEADER_SIZE) != 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_COMPARE_FAILED,
                             "immediate APA header compare");
    return 0;
}

static int verify_one_final(const apa_forensic_result_t *scan,
                            const apa_forensic_patch_t *patch,
                            unsigned int current, unsigned int total)
{
    int result;

    if (apa_forensic_build_patched_header(scan, patch, repaired_header) < 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_PATCH_INVALID,
                             "rebuild expected header for final verification");
    disk_status_phase_at("Final full touched-set verification",
                         patch->lba == 0
                             ? "APA master header / sectors 0-1"
                             : "Re-reading each committed APA header");
    disk_status_io(DISK_STATUS_VERIFY, patch->lba, 2, current, total);
    result = hdd_read_raw_sectors(patch->lba, 2, write_verify);
    if (result < 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_READBACK_FAILED,
                             "final touched-set read-back");
    if (memcmp(write_verify, repaired_header, APA_HEADER_SIZE) != 0)
        return fail_forensic(HDD_FORENSIC_REPAIR_COMPARE_FAILED,
                             "final touched-set compare");
    return 0;
}

int hdd_forensic_repair_apply_verified(
    const apa_forensic_result_t *scan,
    const apa_forensic_repair_plan_t *plan)
{
    unsigned int i;
    unsigned int committed = 0;
    int result;

    if (scan == NULL || plan == NULL) {
        app_error_record(APP_ERROR_DOMAIN_FORENSIC_REPAIR,
                         HDD_FORENSIC_REPAIR_INVALID_ARGUMENT,
                         "validate scan/plan arguments");
        return HDD_FORENSIC_REPAIR_INVALID_ARGUMENT;
    }
    if (scan->truncated || !plan->manual_allowed || plan->patch_count == 0 ||
        plan->map_index >= scan->map_count) {
        app_error_record(APP_ERROR_DOMAIN_FORENSIC_REPAIR,
                         HDD_FORENSIC_REPAIR_PLAN_BLOCKED,
                         scan->truncated
                             ? "validate forensic write gate: incomplete scan"
                             : "validate forensic write gate");
        return HDD_FORENSIC_REPAIR_PLAN_BLOCKED;
    }

    disk_status_begin_at("APA multi-header topology repair",
                         "Preparing interior-header-first transaction",
                         "APA metadata headers selected by forensic repair plan");

    /* Interior headers first. LBA 0 is the final commit point so an interrupted
     * operation does not advertise a new master topology before its members
     * have individually reached disk and passed read-back. */
    for (i = 0; i < plan->patch_count; i++) {
        if (plan->patches[i].lba == 0)
            continue;
        result = write_one(scan, &plan->patches[i], committed,
                           plan->patch_count);
        if (result < 0) {
            disk_status_end();
            return result;
        }
        committed++;
    }
    for (i = 0; i < plan->patch_count; i++) {
        if (plan->patches[i].lba != 0)
            continue;
        result = write_one(scan, &plan->patches[i], committed,
                           plan->patch_count);
        if (result < 0) {
            disk_status_end();
            return result;
        }
        committed++;
    }

    /* Re-read the whole touched set after the commit sequence. */
    for (i = 0; i < plan->patch_count; i++) {
        result = verify_one_final(scan, &plan->patches[i], i + 1u,
                                  plan->patch_count);
        if (result < 0) {
            disk_status_end();
            return result;
        }
    }
    disk_status_end();
    return 0;
}
