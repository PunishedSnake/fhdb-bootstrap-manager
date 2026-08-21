/* Portable regression tests for forensic APA graph reconstruction. */

#include "apa.h"
#include "apa_forensic.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t lba;
    unsigned char header[APA_HEADER_SIZE];
} mock_slot_t;

typedef struct {
    mock_slot_t slots[16];
    unsigned int count;
} mock_disk_t;

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

static void finalize_checksum(unsigned char header[APA_HEADER_SIZE])
{
    write_le32_test(header, apa_checksum(header));
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
    finalize_checksum(header);
}

static void make_master(unsigned char header[APA_HEADER_SIZE], uint32_t prev,
                        uint32_t next)
{
    make_header(header, 0, 0x4000, prev, next, "__mbr", 1);
    memcpy(header + APA_MBR_MAGIC_OFFSET,
           "Sony Computer Entertainment Inc.", 32);
    write_le32_test(header + APA_MBR_VERSION_OFFSET, 2);
    finalize_checksum(header);
}

static void add_slot(mock_disk_t *disk, uint32_t lba,
                     const unsigned char header[APA_HEADER_SIZE])
{
    disk->slots[disk->count].lba = lba;
    memcpy(disk->slots[disk->count].header, header, APA_HEADER_SIZE);
    disk->count++;
}

static int mock_read(void *context, uint32_t lba, unsigned int sectors,
                     unsigned char *destination)
{
    mock_disk_t *disk = (mock_disk_t *)context;
    unsigned int i;

    if (sectors != 2)
        return -99;
    memset(destination, 0, APA_HEADER_SIZE);
    for (i = 0; i < disk->count; i++) {
        if (disk->slots[i].lba == lba) {
            memcpy(destination, disk->slots[i].header, APA_HEADER_SIZE);
            return 0;
        }
    }
    return 0;
}

static int find_map(const apa_forensic_result_t *result,
                    apa_forensic_map_kind_t kind)
{
    unsigned int i;

    for (i = 0; i < result->map_count; i++) {
        if (result->maps[i].kind == kind)
            return (int)i;
    }
    return -1;
}

static int test_healthy_graph(void)
{
    mock_disk_t disk;
    apa_forensic_result_t result;
    unsigned char header[APA_HEADER_SIZE];
    apa_forensic_repair_plan_t plan;
    int map;

    memset(&disk, 0, sizeof(disk));
    make_master(header, 0x80000, 0x40000);
    add_slot(&disk, 0, header);
    make_header(header, 0x40000, 0x40000, 0, 0x80000, "__system", 0x0100);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x80000, 0x40000, 0x40000, 0, "+OPL", 0x0100);
    add_slot(&disk, 0x80000, header);

    if (apa_forensic_scan(mock_read, &disk, 0x100000, NULL, NULL, &result) != 0)
        return 0;
    if (result.node_count != 3 || result.map_count == 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || result.maps[map].confidence != 100 ||
        !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 0 || plan.manual_allowed)
        return 0;
    return 1;
}

static int test_corroborated_broken_link(void)
{
    mock_disk_t disk;
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    unsigned char header[APA_HEADER_SIZE];
    unsigned char repaired[APA_HEADER_SIZE];
    int map;

    memset(&disk, 0, sizeof(disk));
    make_master(header, 0x80000, 0x40000);
    add_slot(&disk, 0, header);
    make_header(header, 0x40000, 0x40000, 0, 0x80000, "A", 0x0100);
    write_le32_test(header + APA_NEXT_OFFSET, 0x90000);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x80000, 0x40000, 0x40000, 0, "B", 0x0100);
    add_slot(&disk, 0x80000, header);

    if (apa_forensic_scan(mock_read, &disk, 0x100000, NULL, NULL, &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 1 || plan.corroborated_count != 1 ||
        plan.speculative_count != 0 || !plan.automatic_safe ||
        !plan.manual_allowed)
        return 0;
    if (apa_forensic_build_patched_header(&result, &plan.patches[0], repaired) != 0)
        return 0;
    if (read_le32(repaired + APA_NEXT_OFFSET) != 0x80000 ||
        read_le32(repaired) != apa_checksum(repaired))
        return 0;
    return 1;
}

static int test_two_bit_corroborated_link(void)
{
    mock_disk_t disk;
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    unsigned char header[APA_HEADER_SIZE];
    int map;

    memset(&disk, 0, sizeof(disk));
    make_master(header, 0x80000, 0x40000);
    add_slot(&disk, 0, header);
    make_header(header, 0x40000, 0x40000, 0, 0x80000, "A", 0x0100);
    /* Flip exactly two bits in next and deliberately retain the old checksum.
       B's reciprocal prev plus geometry reconstruct the intended 0x80000. */
    write_le32_test(header + APA_NEXT_OFFSET, 0x80000u ^ 0x30000u);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x80000, 0x40000, 0x40000, 0, "B", 0x0100);
    add_slot(&disk, 0x80000, header);

    if (apa_forensic_scan(mock_read, &disk, 0x100000, NULL, NULL, &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 1 || !plan.patches[0].checksum_corroborated ||
        apa_forensic_patch_bit_distance(&plan.patches[0]) != 2 ||
        !plan.automatic_safe)
        return 0;
    return 1;
}

static int test_checksummed_wrong_link_is_manual_only(void)
{
    mock_disk_t disk;
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    unsigned char header[APA_HEADER_SIZE];
    int map;

    memset(&disk, 0, sizeof(disk));
    make_master(header, 0x80000, 0x40000);
    add_slot(&disk, 0, header);
    make_header(header, 0x40000, 0x40000, 0, 0x80000, "A", 0x0100);
    write_le32_test(header + APA_NEXT_OFFSET, 0x90000);
    finalize_checksum(header);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x80000, 0x40000, 0x40000, 0, "B", 0x0100);
    add_slot(&disk, 0x80000, header);

    if (apa_forensic_scan(mock_read, &disk, 0x100000, NULL, NULL, &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 1 || plan.corroborated_count != 0 ||
        plan.speculative_count != 1 || plan.automatic_safe ||
        !plan.manual_allowed)
        return 0;
    return 1;
}

static int test_referenced_off_grid_subpartition(void)
{
    mock_disk_t disk;
    apa_forensic_result_t result;
    unsigned char header[APA_HEADER_SIZE];

    memset(&disk, 0, sizeof(disk));
    make_master(header, 0x40000, 0x40000);
    add_slot(&disk, 0, header);
    make_header(header, 0x40000, 0x40000, 0, 0, "MAIN", 0x0100);
    write_le32_test(header + APA_NSUB_OFFSET, 1);
    write_le32_test(header + APA_SUBS_OFFSET, 0x48000);
    write_le32_test(header + APA_SUBS_OFFSET + 4, 0x8000);
    finalize_checksum(header);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x48000, 0x8000, 0, 0, "MAIN", 0x0100);
    write_le16_test(header + APA_FLAGS_OFFSET, APA_SUB_FLAG_VALUE);
    write_le32_test(header + APA_MAIN_OFFSET, 0x40000);
    write_le32_test(header + APA_NUMBER_OFFSET, 1);
    finalize_checksum(header);
    add_slot(&disk, 0x48000, header);

    if (apa_forensic_scan(mock_read, &disk, 0x100000, NULL, NULL, &result) != 0)
        return 0;
    if (result.node_count != 3 || result.reference_reads == 0)
        return 0;
    if (find_map(&result, APA_FORENSIC_MAP_GEOMETRY) < 0)
        return 0;
    return 1;
}

static int test_missing_master_never_becomes_writable(void)
{
    mock_disk_t disk;
    apa_forensic_result_t result;
    unsigned char header[APA_HEADER_SIZE];
    unsigned int i;

    memset(&disk, 0, sizeof(disk));
    make_header(header, 0x40000, 0x40000, 0, 0x80000, "A", 0x0100);
    add_slot(&disk, 0x40000, header);
    make_header(header, 0x80000, 0x40000, 0x40000, 0, "B", 0x0100);
    add_slot(&disk, 0x80000, header);

    if (apa_forensic_scan(mock_read, &disk, 0x100000, NULL, NULL, &result) != 0)
        return 0;
    if (result.node_count != 2)
        return 0;
    for (i = 0; i < result.map_count; i++) {
        if (result.maps[i].repairable)
            return 0;
    }
    return 1;
}

int main(void)
{
    if (!test_healthy_graph()) {
        fprintf(stderr, "healthy forensic graph test failed\n");
        return 1;
    }
    if (!test_corroborated_broken_link()) {
        fprintf(stderr, "corroborated broken-link test failed\n");
        return 1;
    }
    if (!test_two_bit_corroborated_link()) {
        fprintf(stderr, "two-bit corroborated link test failed\n");
        return 1;
    }
    if (!test_checksummed_wrong_link_is_manual_only()) {
        fprintf(stderr, "manual-only checksummed-link test failed\n");
        return 1;
    }
    if (!test_referenced_off_grid_subpartition()) {
        fprintf(stderr, "off-grid referenced subpartition test failed\n");
        return 1;
    }
    if (!test_missing_master_never_becomes_writable()) {
        fprintf(stderr, "missing-master write gate test failed\n");
        return 1;
    }

    printf("All portable APA forensic graph tests passed.\n");
    return 0;
}