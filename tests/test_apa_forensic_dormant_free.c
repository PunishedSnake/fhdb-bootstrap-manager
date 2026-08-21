/* Regression for valid historical __empty headers left inside a coalesced free extent. */

#include "apa.h"
#include "apa_forensic.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t lba;
    unsigned char header[APA_HEADER_SIZE];
} slot_t;

typedef struct {
    slot_t slots[8];
    unsigned int count;
} disk_t;

static void write_le16_test(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32_test(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void make_header(unsigned char header[APA_HEADER_SIZE], uint32_t lba,
                        uint32_t length, uint32_t prev, uint32_t next,
                        const char *id, uint16_t type)
{
    memset(header, 0, APA_HEADER_SIZE);
    memcpy(header + APA_MAGIC_OFFSET, "APA\0", 4);
    strncpy((char *)header + APA_ID_OFFSET, id, APA_ID_SIZE);
    write_le32_test(header + APA_NEXT_OFFSET, next);
    write_le32_test(header + APA_PREV_OFFSET, prev);
    write_le32_test(header + APA_START_OFFSET, lba);
    write_le32_test(header + APA_LENGTH_OFFSET, length);
    write_le16_test(header + APA_TYPE_OFFSET, type);
    write_le32_test(header, apa_checksum(header));
}

static void make_master(unsigned char header[APA_HEADER_SIZE], uint32_t prev,
                        uint32_t next)
{
    make_header(header, 0, 0x40000, prev, next, "__mbr", 1);
    memcpy(header + APA_MBR_MAGIC_OFFSET,
           "Sony Computer Entertainment Inc.", 32);
    write_le32_test(header + APA_MBR_VERSION_OFFSET, 2);
    write_le32_test(header, apa_checksum(header));
}

static void add_slot(disk_t *disk, uint32_t lba,
                     const unsigned char header[APA_HEADER_SIZE])
{
    disk->slots[disk->count].lba = lba;
    memcpy(disk->slots[disk->count].header, header, APA_HEADER_SIZE);
    disk->count++;
}

static int read_mock(void *context, uint32_t lba, unsigned int sectors,
                     unsigned char *destination)
{
    disk_t *disk = (disk_t *)context;
    unsigned int i;

    if (sectors != 2)
        return -1;
    memset(destination, 0, APA_HEADER_SIZE);
    for (i = 0; i < disk->count; i++) {
        if (disk->slots[i].lba == lba) {
            memcpy(destination, disk->slots[i].header, APA_HEADER_SIZE);
            break;
        }
    }
    return 0;
}

static int find_forward(const apa_forensic_result_t *result)
{
    unsigned int i;

    for (i = 0; i < result->map_count; i++) {
        if (result->maps[i].kind == APA_FORENSIC_MAP_FORWARD)
            return (int)i;
    }
    return -1;
}

static int find_node(const apa_forensic_result_t *result, uint32_t lba)
{
    unsigned int i;

    for (i = 0; i < result->node_count; i++) {
        if (result->nodes[i].lba == lba)
            return (int)i;
    }
    return -1;
}

int main(void)
{
    disk_t disk;
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    unsigned char header[APA_HEADER_SIZE];
    int map;
    int dormant_a;
    int dormant_b;

    memset(&disk, 0, sizeof(disk));

    /* Active master chain: master -> one coalesced free extent -> live partition. */
    make_master(header, 0x140000, 0x40000);
    add_slot(&disk, 0, header);
    make_header(header, 0x40000, 0x100000, 0, 0x140000, "__empty", 0);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x140000, 0x40000, 0x40000, 0,
                "PP.TEST-ACTIVE", 0x1337);
    add_slot(&disk, 0x140000, header);

    /* Historical allocator headers still physically present inside the active
     * 0x40000..0x140000 free extent. They are structurally valid but are not in
     * the master forward chain, matching the real 2 TB hardware report. */
    make_header(header, 0x80000, 0x40000, 0x40000, 0xc0000, "__empty", 0);
    add_slot(&disk, 0x80000, header);
    make_header(header, 0xc0000, 0x80000, 0x80000, 0x140000, "__empty", 0);
    add_slot(&disk, 0xc0000, header);

    if (apa_forensic_scan(read_mock, &disk, 0x180000,
                          NULL, NULL, &result) != 0) {
        fprintf(stderr, "scan failed\n");
        return 1;
    }

    dormant_a = find_node(&result, 0x80000);
    dormant_b = find_node(&result, 0xc0000);
    if (result.node_count != 5 || result.dormant_free_nodes != 2 ||
        dormant_a < 0 || dormant_b < 0 ||
        (result.nodes[dormant_a].evidence &
         APA_FORENSIC_EVIDENCE_DORMANT_FREE) == 0 ||
        (result.nodes[dormant_b].evidence &
         APA_FORENSIC_EVIDENCE_DORMANT_FREE) == 0) {
        fprintf(stderr, "dormant free classification failed\n");
        return 1;
    }

    map = find_forward(&result);
    if (map < 0 || result.maps[map].node_count != 3 ||
        result.maps[map].reciprocal_links != 2 ||
        result.maps[map].inferred_links != 0 ||
        result.maps[map].conflicts != 0 ||
        result.maps[map].overlaps != 0 ||
        result.maps[map].confidence != 100) {
        fprintf(stderr, "active forward map was penalized by dormant free history\n");
        return 1;
    }

    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 0 || plan.automatic_safe || plan.manual_allowed) {
        fprintf(stderr, "healthy coalesced free-space map proposed repair\n");
        return 1;
    }

    printf("Dormant APA free-space remnant regression passed.\n");
    return 0;
}
