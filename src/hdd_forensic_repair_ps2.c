/* Guarded PS2 writer for forensic APA topology repair plans. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include <string.h>

#include "apa.h"
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
                     const apa_forensic_patch_t *patch)
{
    const apa_forensic_node_t *node = &scan->nodes[patch->node_index];
    int result;

    /* Refuse to apply an old plan to bytes that changed after the scan. */
    result = hdd_read_raw_sectors(patch->lba, 2, source_verify);
    if (result < 0)
        return HDD_FORENSIC_REPAIR_READBACK_FAILED;
    if (memcmp(source_verify, node->header, APA_HEADER_SIZE) != 0)
        return HDD_FORENSIC_REPAIR_SOURCE_CHANGED;

    if (apa_forensic_build_patched_header(scan, patch, repaired_header) < 0 ||
        !patch_header_is_safe(scan, patch, repaired_header))
        return HDD_FORENSIC_REPAIR_PATCH_INVALID;

    write_packet.lba = patch->lba;
    write_packet.size = 2;
    memcpy(write_packet.data, repaired_header, APA_HEADER_SIZE);
    result = fileXioDevctl("hdd0:", HDIOC_WRITESECTOR_LOCAL,
                           &write_packet, sizeof(write_packet), NULL, 0);
    if (result < 0)
        return HDD_FORENSIC_REPAIR_WRITE_FAILED;
    result = fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL,
                           NULL, 0, NULL, 0);
    if (result < 0)
        return HDD_FORENSIC_REPAIR_FLUSH_FAILED;
    result = hdd_read_raw_sectors(patch->lba, 2, write_verify);
    if (result < 0)
        return HDD_FORENSIC_REPAIR_READBACK_FAILED;
    if (memcmp(write_verify, repaired_header, APA_HEADER_SIZE) != 0)
        return HDD_FORENSIC_REPAIR_COMPARE_FAILED;
    return 0;
}

static int verify_one_final(const apa_forensic_result_t *scan,
                            const apa_forensic_patch_t *patch)
{
    int result;

    if (apa_forensic_build_patched_header(scan, patch, repaired_header) < 0)
        return HDD_FORENSIC_REPAIR_PATCH_INVALID;
    result = hdd_read_raw_sectors(patch->lba, 2, write_verify);
    if (result < 0)
        return HDD_FORENSIC_REPAIR_READBACK_FAILED;
    if (memcmp(write_verify, repaired_header, APA_HEADER_SIZE) != 0)
        return HDD_FORENSIC_REPAIR_COMPARE_FAILED;
    return 0;
}

int hdd_forensic_repair_apply_verified(
    const apa_forensic_result_t *scan,
    const apa_forensic_repair_plan_t *plan)
{
    unsigned int i;
    int result;

    if (scan == NULL || plan == NULL)
        return HDD_FORENSIC_REPAIR_INVALID_ARGUMENT;
    if (!plan->manual_allowed || plan->patch_count == 0 ||
        plan->map_index >= scan->map_count)
        return HDD_FORENSIC_REPAIR_PLAN_BLOCKED;

    /* Interior headers first. LBA 0 is the final commit point so an interrupted
     * operation does not advertise a new master topology before its members
     * have individually reached disk and passed read-back. */
    for (i = 0; i < plan->patch_count; i++) {
        if (plan->patches[i].lba == 0)
            continue;
        result = write_one(scan, &plan->patches[i]);
        if (result < 0)
            return result;
    }
    for (i = 0; i < plan->patch_count; i++) {
        if (plan->patches[i].lba != 0)
            continue;
        result = write_one(scan, &plan->patches[i]);
        if (result < 0)
            return result;
    }

    /* Re-read the whole touched set after the commit sequence. */
    for (i = 0; i < plan->patch_count; i++) {
        result = verify_one_final(scan, &plan->patches[i]);
        if (result < 0)
            return result;
    }
    return 0;
}