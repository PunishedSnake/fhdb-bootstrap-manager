/* End-to-end host regression for the forensic engine against sparse raw HDDs. */

#include "apa_forensic.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512u
#define TOTAL_SECTORS 0x100000u

typedef struct {
    FILE *file;
} file_reader_t;

static int file_read(void *context, uint32_t lba, unsigned int sectors,
                     unsigned char *destination)
{
    file_reader_t *reader = (file_reader_t *)context;
    uint64_t offset = (uint64_t)lba * SECTOR_SIZE;
    size_t bytes = (size_t)sectors * SECTOR_SIZE;

    if (fseek(reader->file, (long)offset, SEEK_SET) != 0)
        return -1;
    if (fread(destination, 1, bytes, reader->file) != bytes)
        return -2;
    return 0;
}

static int open_case(const char *directory, const char *name,
                     file_reader_t *reader)
{
    char path[512];

    if (snprintf(path, sizeof(path), "%s/%s.raw", directory, name) >=
        (int)sizeof(path))
        return -1;
    reader->file = fopen(path, "rb");
    return reader->file == NULL ? -1 : 0;
}

static int scan_case(const char *directory, const char *name,
                     apa_forensic_result_t *result)
{
    file_reader_t reader;
    int rc;

    if (open_case(directory, name, &reader) < 0)
        return -1;
    rc = apa_forensic_scan(file_read, &reader, TOTAL_SECTORS,
                           NULL, NULL, result);
    fclose(reader.file);
    return rc;
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

static int healthy_case(const char *directory)
{
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    int map;

    if (scan_case(directory, "healthy_chain", &result) != 0)
        return 0;
    if (result.node_count != 4 || result.truncated)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || result.maps[map].node_count != 4 ||
        result.maps[map].confidence != 100 ||
        !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 0 || plan.automatic_safe || plan.manual_allowed)
        return 0;
    return 1;
}

static int stale_link_case(const char *directory)
{
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    int map;

    if (scan_case(directory, "broken_next_stale_checksum", &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 1 || plan.corroborated_count != 1 ||
        plan.speculative_count != 0 || !plan.automatic_safe ||
        !plan.manual_allowed)
        return 0;
    return 1;
}

static int two_bit_case(const char *directory)
{
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    int map;

    if (scan_case(directory, "two_bit_next_stale_checksum", &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 1 || plan.corroborated_count != 1 ||
        !plan.automatic_safe)
        return 0;
    if (apa_forensic_patch_bit_distance(&plan.patches[0]) != 2u)
        return 0;
    return 1;
}

static int manual_case(const char *directory)
{
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    int map;

    if (scan_case(directory, "checksummed_wrong_next", &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_FORWARD);
    if (map < 0 || !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 1 || plan.corroborated_count != 0 ||
        plan.speculative_count != 1 || plan.automatic_safe ||
        !plan.manual_allowed)
        return 0;
    return 1;
}

static int offgrid_case(const char *directory)
{
    apa_forensic_result_t result;
    unsigned int i;
    int found = 0;

    if (scan_case(directory, "offgrid_subpartition", &result) != 0)
        return 0;
    if (result.reference_reads == 0 || result.node_count != 3)
        return 0;
    for (i = 0; i < result.node_count; i++) {
        if (result.nodes[i].lba == 0x48000u &&
            (result.nodes[i].evidence & APA_FORENSIC_EVIDENCE_REFERENCED) != 0)
            found = 1;
    }
    return found;
}

static int missing_master_case(const char *directory)
{
    apa_forensic_result_t result;
    unsigned int i;

    if (scan_case(directory, "missing_master", &result) != 0)
        return 0;
    if (result.node_count != 2)
        return 0;
    for (i = 0; i < result.map_count; i++) {
        if (result.maps[i].repairable)
            return 0;
    }
    return 1;
}

static int overlap_case(const char *directory)
{
    apa_forensic_result_t result;
    unsigned int i;
    int saw_overlap = 0;

    if (scan_case(directory, "overlapping_geometry", &result) != 0)
        return 0;

    /* Maps with the same node order are deliberately deduplicated by the
     * production engine. Geometry may therefore collapse into an identical
     * forward/reverse candidate. The safety property is that every retained
     * candidate which detects the overlap remains non-writeable, not that a
     * particular map-kind label must survive deduplication. */
    for (i = 0; i < result.map_count; i++) {
        if (result.maps[i].overlaps != 0) {
            saw_overlap = 1;
            if (result.maps[i].repairable)
                return 0;
        }
    }
    return saw_overlap;
}

static int multi_header_case(const char *directory)
{
    apa_forensic_result_t result;
    apa_forensic_repair_plan_t plan;
    int map;

    if (scan_case(directory, "two_header_link_damage", &result) != 0)
        return 0;
    map = find_map(&result, APA_FORENSIC_MAP_GEOMETRY);
    if (map < 0 || !result.maps[map].repairable)
        return 0;
    if (apa_forensic_build_repair_plan(&result, (unsigned int)map, &plan) != 0 ||
        plan.patch_count != 2 || plan.corroborated_count != 2 ||
        plan.speculative_count != 0 || !plan.automatic_safe ||
        !plan.manual_allowed)
        return 0;
    return 1;
}

static int conflict_case(const char *directory)
{
    apa_forensic_result_t result;
    unsigned int i;
    int saw_conflict = 0;

    if (scan_case(directory, "conflicting_live_target", &result) != 0)
        return 0;

    /* Reverse reconstruction can yield the same complete node order as the
     * geometry candidate. Since production deduplicates equal orders, the map
     * label is not part of the safety contract. The surviving hypothesis must
     * expose the live-target conflict and must remain non-writeable. */
    for (i = 0; i < result.map_count; i++) {
        if (result.maps[i].conflicts != 0) {
            saw_conflict = 1;
            if (result.maps[i].repairable)
                return 0;
        }
    }
    return saw_conflict;
}

int main(int argc, char **argv)
{
    const char *directory;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <forensic-fixture-dir>\n", argv[0]);
        return 2;
    }
    directory = argv[1];

    if (!healthy_case(directory)) {
        fprintf(stderr, "healthy sparse forensic fixture failed\n");
        return 1;
    }
    if (!stale_link_case(directory)) {
        fprintf(stderr, "stale-link sparse forensic fixture failed\n");
        return 1;
    }
    if (!two_bit_case(directory)) {
        fprintf(stderr, "two-bit sparse forensic fixture failed\n");
        return 1;
    }
    if (!manual_case(directory)) {
        fprintf(stderr, "manual sparse forensic fixture failed\n");
        return 1;
    }
    if (!offgrid_case(directory)) {
        fprintf(stderr, "off-grid sparse forensic fixture failed\n");
        return 1;
    }
    if (!missing_master_case(directory)) {
        fprintf(stderr, "missing-master sparse forensic fixture failed\n");
        return 1;
    }
    if (!overlap_case(directory)) {
        fprintf(stderr, "overlap sparse forensic fixture failed\n");
        return 1;
    }
    if (!multi_header_case(directory)) {
        fprintf(stderr, "multi-header sparse forensic fixture failed\n");
        return 1;
    }
    if (!conflict_case(directory)) {
        fprintf(stderr, "conflict sparse forensic fixture failed\n");
        return 1;
    }

    printf("All 9 sparse raw-HDD forensic fixture cases passed.\n");
    return 0;
}
