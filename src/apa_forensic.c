/*
 * Portable read-only APA forensic graph reconstruction.
 *
 * This code intentionally treats disk structure as evidence rather than truth.
 * It never writes, mounts, allocates, or calls PS2SDK. A coarse scan discovers
 * standard APA-aligned headers and a second pass follows surviving graph and
 * sub-partition references. Candidate maps keep inference explicit so the UI
 * can distinguish a readable hypothesis from a safe write plan.
 */

#include "apa_forensic.h"

#include <stddef.h>
#include <string.h>

static void write_le32_forensic(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static int type_plausible(uint16_t type)
{
    switch (type) {
        case 0x0000:
        case 0x0001:
        case 0x0082:
        case 0x0083:
        case 0x0088:
        case 0x0100:
        case 0x0101:
        case 0x1337:
            return 1;
        default:
            return 0;
    }
}

static int id_plausible(const unsigned char *id)
{
    unsigned int i;
    int saw_character = 0;

    for (i = 0; i < APA_ID_SIZE; i++) {
        if (id[i] == 0)
            break;
        if (id[i] < 0x20 || id[i] > 0x7e)
            return 0;
        saw_character = 1;
    }
    return saw_character;
}

static unsigned int clamp_confidence(int value)
{
    if (value < 0)
        return 0;
    if (value > 100)
        return 100;
    return (unsigned int)value;
}

static unsigned int inspect_header(const unsigned char header[APA_HEADER_SIZE],
                                   uint32_t lba, uint32_t total_sectors,
                                   uint32_t source_evidence,
                                   uint32_t *evidence_out)
{
    const uint32_t semantic_anchor_mask =
        APA_FORENSIC_EVIDENCE_MAGIC |
        APA_FORENSIC_EVIDENCE_LENGTH |
        APA_FORENSIC_EVIDENCE_ID;
    const uint32_t direct_grid_anchor_mask =
        APA_FORENSIC_EVIDENCE_MAGIC |
        APA_FORENSIC_EVIDENCE_SELF_START;
    uint32_t evidence = source_evidence;
    uint32_t start = read_le32(header + APA_START_OFFSET);
    uint32_t length = read_le32(header + APA_LENGTH_OFFSET);
    uint32_t nsub = read_le32(header + APA_NSUB_OFFSET);
    uint16_t type = read_le16(header + APA_TYPE_OFFSET);
    uint64_t end = (uint64_t)start + (uint64_t)length;
    int score = 0;

    if (memcmp(header + APA_MAGIC_OFFSET, "APA\0", 4) == 0) {
        evidence |= APA_FORENSIC_EVIDENCE_MAGIC;
        score += 25;
    }
    if (start == lba) {
        evidence |= APA_FORENSIC_EVIDENCE_SELF_START;
        score += 20;
    }
    if (read_le32(header) == apa_checksum(header)) {
        evidence |= APA_FORENSIC_EVIDENCE_CHECKSUM;
        score += 20;
    }
    if (length != 0 && start < total_sectors && end <= total_sectors) {
        evidence |= APA_FORENSIC_EVIDENCE_LENGTH;
        score += 15;
    }
    if (nsub <= APA_MAX_SUBS) {
        evidence |= APA_FORENSIC_EVIDENCE_NSUB;
        score += 5;
    }
    if (type_plausible(type)) {
        evidence |= APA_FORENSIC_EVIDENCE_TYPE;
        score += 5;
    }
    if (id_plausible(header + APA_ID_OFFSET)) {
        evidence |= APA_FORENSIC_EVIDENCE_ID;
        score += 10;
    }

    /* Zero-filled/unallocated sectors trivially satisfy checksum/type/nsub and,
     * at LBA 0, even self-start. Require actual APA-shaped semantic evidence
     * before treating those generic zero values as a candidate header. */
    if ((evidence & semantic_anchor_mask) == 0)
        score = 0;

    /* A real 2 TB HDL disk exposed a second grid false-positive class: random
     * game data can accidentally contain a printable byte string, a bounded
     * length and nsub<=APA_MAX_SUBS, scoring 30 despite having neither APA magic
     * nor a self-consistent start LBA. Direct grid discovery now requires one of
     * those two primary anchors. A header reached through a surviving graph
     * reference may still be retained with weaker anchors so damaged magic/start
     * fields remain inspectable in degraded read-only recovery. */
    if ((source_evidence & APA_FORENSIC_EVIDENCE_REFERENCED) == 0 &&
        (evidence & direct_grid_anchor_mask) == 0)
        score = 0;

    *evidence_out = evidence;
    return clamp_confidence(score);
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

static int add_candidate(apa_forensic_read_fn reader, void *reader_context,
                         uint32_t lba, uint32_t total_sectors,
                         uint32_t source_evidence,
                         apa_forensic_result_t *result)
{
    unsigned char header[APA_HEADER_SIZE];
    uint32_t evidence;
    unsigned int confidence;
    int existing;
    int read_result;
    apa_forensic_node_t *node;

    if (lba >= total_sectors || total_sectors - lba < 2)
        return 0;

    existing = find_node(result, lba);
    if (existing >= 0) {
        result->nodes[existing].evidence |= source_evidence;
        return 0;
    }

    read_result = reader(reader_context, lba, 2, header);
    if (read_result < 0) {
        result->unreadable_reads++;
        return read_result;
    }

    confidence = inspect_header(header, lba, total_sectors,
                                source_evidence, &evidence);
    if (confidence < 30 && !(lba == 0 && confidence >= 20))
        return 0;

    if (result->node_count >= APA_FORENSIC_MAX_NODES) {
        result->truncated = 1;
        return 1;
    }

    node = &result->nodes[result->node_count++];
    memset(node, 0, sizeof(*node));
    node->lba = lba;
    node->stored_checksum = read_le32(header);
    node->calculated_checksum = apa_checksum(header);
    node->next = read_le32(header + APA_NEXT_OFFSET);
    node->prev = read_le32(header + APA_PREV_OFFSET);
    node->start = read_le32(header + APA_START_OFFSET);
    node->length = read_le32(header + APA_LENGTH_OFFSET);
    node->type = read_le16(header + APA_TYPE_OFFSET);
    node->flags = read_le16(header + APA_FLAGS_OFFSET);
    node->nsub = read_le32(header + APA_NSUB_OFFSET);
    node->main = read_le32(header + APA_MAIN_OFFSET);
    node->number = read_le32(header + APA_NUMBER_OFFSET);
    node->evidence = evidence;
    node->confidence = confidence;
    memcpy(node->header, header, APA_HEADER_SIZE);
    memcpy(node->id, header + APA_ID_OFFSET, APA_ID_SIZE);
    node->id[APA_ID_SIZE] = '\0';
    return 1;
}

static int referenced_node_candidate(const apa_forensic_node_t *node)
{
    const uint32_t required = APA_FORENSIC_EVIDENCE_MAGIC |
                              APA_FORENSIC_EVIDENCE_LENGTH;

    return node->confidence >= 50 &&
           (node->evidence & required) == required;
}

static void follow_reference(apa_forensic_read_fn reader, void *reader_context,
                             uint32_t lba, uint32_t total_sectors,
                             apa_forensic_result_t *result)
{
    int before;

    if (lba == 0 || lba >= total_sectors)
        return;
    before = find_node(result, lba);
    if (before >= 0) {
        result->nodes[before].evidence |= APA_FORENSIC_EVIDENCE_REFERENCED;
        return;
    }
    result->reference_reads++;
    (void)add_candidate(reader, reader_context, lba, total_sectors,
                        APA_FORENSIC_EVIDENCE_REFERENCED, result);
}

static void chase_references(apa_forensic_read_fn reader, void *reader_context,
                             uint32_t total_sectors,
                             apa_forensic_result_t *result)
{
    unsigned int cursor = 0;

    while (cursor < result->node_count && !result->truncated) {
        apa_forensic_node_t *node = &result->nodes[cursor++];
        unsigned int i;

        if (!referenced_node_candidate(node))
            continue;

        follow_reference(reader, reader_context, node->next, total_sectors,
                         result);
        follow_reference(reader, reader_context, node->prev, total_sectors,
                         result);
        follow_reference(reader, reader_context, node->main, total_sectors,
                         result);

        if (node->nsub <= APA_MAX_SUBS && node->confidence >= 60) {
            for (i = 0; i < node->nsub; i++) {
                uint32_t sub_start = read_le32(
                    node->header + APA_SUBS_OFFSET +
                    (i * APA_SUB_ENTRY_SIZE));
                follow_reference(reader, reader_context, sub_start,
                                 total_sectors, result);
            }
        }
    }
}

static int map_contains(const apa_forensic_map_t *map, unsigned int index)
{
    unsigned int i;

    for (i = 0; i < map->node_count; i++) {
        if (map->order[i] == index)
            return 1;
    }
    return 0;
}

static int unique_link_candidate(const apa_forensic_result_t *result,
                                 const apa_forensic_map_t *map,
                                 uint32_t current_lba, int match_prev)
{
    int found = -1;
    unsigned int i;

    for (i = 0; i < result->node_count; i++) {
        const apa_forensic_node_t *node = &result->nodes[i];
        uint32_t link = match_prev ? node->prev : node->next;

        if (map_contains(map, i) || node->confidence < 55)
            continue;
        if (link != current_lba)
            continue;
        if (found >= 0)
            return -1;
        found = (int)i;
    }
    return found;
}

static void build_forward_map(const apa_forensic_result_t *result,
                              apa_forensic_map_t *map)
{
    int master = find_node(result, 0);
    int current;

    memset(map, 0, sizeof(*map));
    map->kind = APA_FORENSIC_MAP_FORWARD;
    if (master < 0)
        return;

    map->order[map->node_count++] = (unsigned short)master;
    current = master;
    while (map->node_count < APA_FORENSIC_MAX_NODES) {
        uint32_t next_lba = result->nodes[current].next;
        int next = next_lba != 0 ? find_node(result, next_lba) : -1;

        if (next_lba == 0) {
            if (current == master)
                next = unique_link_candidate(result, map, 0, 1);
            else
                break;
        }
        if (next < 0 || map_contains(map, (unsigned int)next)) {
            next = unique_link_candidate(result, map,
                                         result->nodes[current].lba, 1);
        }
        if (next < 0 || map_contains(map, (unsigned int)next))
            break;
        map->order[map->node_count++] = (unsigned short)next;
        current = next;
    }
}

static void reverse_indices(unsigned short *values, unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count / 2; i++) {
        unsigned short tmp = values[i];
        values[i] = values[count - 1 - i];
        values[count - 1 - i] = tmp;
    }
}

static int index_in_values(const unsigned short *values, unsigned int count,
                           unsigned short value)
{
    unsigned int i;

    for (i = 0; i < count; i++) {
        if (values[i] == value)
            return 1;
    }
    return 0;
}

static void build_reverse_map(const apa_forensic_result_t *result,
                              apa_forensic_map_t *map)
{
    unsigned short reverse_order[APA_FORENSIC_MAX_NODES];
    unsigned int reverse_count = 0;
    int master = find_node(result, 0);
    int current;

    memset(map, 0, sizeof(*map));
    map->kind = APA_FORENSIC_MAP_REVERSE;
    if (master < 0)
        return;

    current = result->nodes[master].prev != 0
                  ? find_node(result, result->nodes[master].prev) : -1;
    if (current < 0)
        current = unique_link_candidate(result, map, 0, 0);

    while (current >= 0 && reverse_count < APA_FORENSIC_MAX_NODES - 1) {
        uint32_t prev_lba;
        int prev;
        unsigned int i;

        if (index_in_values(reverse_order, reverse_count,
                            (unsigned short)current))
            break;

        reverse_order[reverse_count++] = (unsigned short)current;
        prev_lba = result->nodes[current].prev;
        if (prev_lba == 0)
            break;
        prev = find_node(result, prev_lba);
        if (prev < 0) {
            apa_forensic_map_t temporary = *map;
            for (i = 0; i < reverse_count; i++)
                temporary.order[temporary.node_count++] = reverse_order[i];
            prev = unique_link_candidate(result, &temporary,
                                         result->nodes[current].lba, 0);
        }
        current = prev;
    }

    map->order[map->node_count++] = (unsigned short)master;
    reverse_indices(reverse_order, reverse_count);
    if (reverse_count > 0) {
        unsigned int i;
        for (i = 0; i < reverse_count; i++)
            map->order[map->node_count++] = reverse_order[i];
    }
}

static void sort_map_by_lba(const apa_forensic_result_t *result,
                            apa_forensic_map_t *map)
{
    unsigned int i;

    for (i = 1; i < map->node_count; i++) {
        unsigned short value = map->order[i];
        unsigned int j = i;

        while (j > 0 &&
               result->nodes[map->order[j - 1]].lba >
                   result->nodes[value].lba) {
            map->order[j] = map->order[j - 1];
            j--;
        }
        map->order[j] = value;
    }
}

static void build_geometry_map(const apa_forensic_result_t *result,
                               apa_forensic_map_t *map)
{
    unsigned int i;

    memset(map, 0, sizeof(*map));
    map->kind = APA_FORENSIC_MAP_GEOMETRY;
    for (i = 0; i < result->node_count &&
                map->node_count < APA_FORENSIC_MAX_NODES; i++) {
        const apa_forensic_node_t *node = &result->nodes[i];
        const uint32_t required = APA_FORENSIC_EVIDENCE_SELF_START |
                                  APA_FORENSIC_EVIDENCE_LENGTH;

        if (node->lba == 0 ||
            (node->confidence >= 50 &&
             (node->evidence & required) == required))
            map->order[map->node_count++] = (unsigned short)i;
    }
    sort_map_by_lba(result, map);
}

static int maps_equal(const apa_forensic_map_t *a,
                      const apa_forensic_map_t *b)
{
    unsigned int i;

    if (a->node_count != b->node_count)
        return 0;
    for (i = 0; i < a->node_count; i++) {
        if (a->order[i] != b->order[i])
            return 0;
    }
    return 1;
}

static void evaluate_map(const apa_forensic_result_t *result,
                         apa_forensic_map_t *map)
{
    unsigned int high_nodes = 0;
    unsigned int weak_nodes = 0;
    unsigned int i;
    int score = 100;

    if (map->node_count == 0) {
        map->confidence = 0;
        return;
    }

    for (i = 0; i < result->node_count; i++) {
        if (result->nodes[i].confidence >= 60)
            high_nodes++;
    }

    for (i = 0; i < map->node_count; i++) {
        const apa_forensic_node_t *node = &result->nodes[map->order[i]];
        uint32_t desired_prev;
        uint32_t desired_next;

        if (node->confidence < 60)
            weak_nodes++;

        if (i == 0) {
            desired_prev = map->node_count > 1
                               ? result->nodes[map->order[map->node_count - 1]].lba
                               : 0;
            desired_next = map->node_count > 1
                               ? result->nodes[map->order[1]].lba : 0;
        } else {
            desired_prev = i == 1 ? 0 : result->nodes[map->order[i - 1]].lba;
            desired_next = i + 1 < map->node_count
                               ? result->nodes[map->order[i + 1]].lba : 0;
        }

        if (node->prev != desired_prev) {
            int target = node->prev != 0 ? find_node(result, node->prev) : -1;
            map->inferred_links++;
            if (target >= 0 && node->prev != desired_prev)
                map->conflicts++;
        }
        if (node->next != desired_next) {
            int target = node->next != 0 ? find_node(result, node->next) : -1;
            map->inferred_links++;
            if (target >= 0 && node->next != desired_next)
                map->conflicts++;
        }

        if (i + 1 < map->node_count) {
            const apa_forensic_node_t *next =
                &result->nodes[map->order[i + 1]];
            if (node->next == next->lba && next->prev == node->lba)
                map->reciprocal_links++;
            if (i > 0) {
                uint64_t end = (uint64_t)node->start + node->length;
                if (end > next->start)
                    map->overlaps++;
            }
        }
    }

    score -= (int)map->conflicts * 15;
    score -= (int)map->overlaps * 30;
    score -= (int)map->inferred_links * 2;
    score -= (int)weak_nodes * 4;
    if (high_nodes > map->node_count)
        score -= (int)(high_nodes - map->node_count) * 5;
    if (result->truncated)
        score -= 10;

    map->confidence = clamp_confidence(score);
    map->repairable = !result->truncated &&
                      map->node_count >= 2 &&
                      result->nodes[map->order[0]].lba == 0 &&
                      map->confidence >= 85 &&
                      map->conflicts == 0 && map->overlaps == 0;
    if (map->repairable) {
        const uint32_t required = APA_FORENSIC_EVIDENCE_MAGIC |
                                  APA_FORENSIC_EVIDENCE_SELF_START |
                                  APA_FORENSIC_EVIDENCE_LENGTH;
        for (i = 0; i < map->node_count; i++) {
            const apa_forensic_node_t *node =
                &result->nodes[map->order[i]];
            if ((node->evidence & required) != required) {
                map->repairable = 0;
                break;
            }
        }
    }
}

static void add_map_if_unique(apa_forensic_result_t *result,
                              const apa_forensic_map_t *candidate)
{
    unsigned int i;

    if (candidate->node_count == 0 ||
        result->map_count >= APA_FORENSIC_MAX_MAPS)
        return;
    for (i = 0; i < result->map_count; i++) {
        if (maps_equal(&result->maps[i], candidate))
            return;
    }
    result->maps[result->map_count++] = *candidate;
}

int apa_forensic_scan(apa_forensic_read_fn reader, void *reader_context,
                      uint32_t total_sectors,
                      apa_forensic_progress_fn progress,
                      void *progress_context,
                      apa_forensic_result_t *result)
{
    uint32_t lba;
    apa_forensic_map_t candidate;

    if (reader == NULL || result == NULL || total_sectors < 2)
        return -1;

    memset(result, 0, sizeof(*result));
    result->total_sectors = total_sectors;
    result->grid_step = APA_FORENSIC_SCAN_STEP;

    result->grid_reads++;
    (void)add_candidate(reader, reader_context, 0, total_sectors,
                        APA_FORENSIC_EVIDENCE_GRID, result);
    if (progress != NULL)
        progress(progress_context, 0, total_sectors, result->node_count);

    for (lba = APA_FORENSIC_SCAN_STEP;
         lba < total_sectors && !result->truncated;
         lba += APA_FORENSIC_SCAN_STEP) {
        result->grid_reads++;
        (void)add_candidate(reader, reader_context, lba, total_sectors,
                            APA_FORENSIC_EVIDENCE_GRID, result);
        if (progress != NULL &&
            ((result->grid_reads & 31u) == 0u ||
             total_sectors - lba <= APA_FORENSIC_SCAN_STEP))
            progress(progress_context, lba, total_sectors,
                     result->node_count);
        if (UINT32_MAX - lba < APA_FORENSIC_SCAN_STEP)
            break;
    }

    chase_references(reader, reader_context, total_sectors, result);

    build_forward_map(result, &candidate);
    evaluate_map(result, &candidate);
    add_map_if_unique(result, &candidate);

    build_reverse_map(result, &candidate);
    evaluate_map(result, &candidate);
    add_map_if_unique(result, &candidate);

    build_geometry_map(result, &candidate);
    evaluate_map(result, &candidate);
    add_map_if_unique(result, &candidate);

    if (progress != NULL)
        progress(progress_context, total_sectors, total_sectors,
                 result->node_count);
    return 0;
}

static int patch_checksum_corroborated(const apa_forensic_node_t *node,
                                       uint32_t new_next, uint32_t new_prev)
{
    unsigned char repaired[APA_HEADER_SIZE];

    memcpy(repaired, node->header, sizeof(repaired));
    write_le32_forensic(repaired + APA_NEXT_OFFSET, new_next);
    write_le32_forensic(repaired + APA_PREV_OFFSET, new_prev);
    return apa_checksum(repaired) == node->stored_checksum;
}

int apa_forensic_build_repair_plan(const apa_forensic_result_t *result,
                                   unsigned int map_index,
                                   apa_forensic_repair_plan_t *plan)
{
    const apa_forensic_map_t *map;
    unsigned int i;

    if (result == NULL || plan == NULL || map_index >= result->map_count)
        return -1;

    memset(plan, 0, sizeof(*plan));
    plan->map_index = map_index;
    map = &result->maps[map_index];
    plan->confidence = map->confidence;

    /* A partial graph has no trustworthy tail. The real-hardware 2 TB scan that
     * triggered this guard showed why: once the node cap was hit, the visible
     * tail looked like it should point to zero and the master looked like it
     * should point back to that visible tail, producing two entirely artificial
     * speculative patches. Truncation is therefore a hard no-write condition. */
    if (result->truncated || !map->repairable)
        return 0;

    for (i = 0; i < map->node_count; i++) {
        const apa_forensic_node_t *node = &result->nodes[map->order[i]];
        uint32_t desired_prev;
        uint32_t desired_next;
        apa_forensic_patch_t *patch;

        if (i == 0) {
            desired_prev = map->node_count > 1
                               ? result->nodes[map->order[map->node_count - 1]].lba
                               : 0;
            desired_next = map->node_count > 1
                               ? result->nodes[map->order[1]].lba : 0;
        } else {
            desired_prev = i == 1 ? 0 : result->nodes[map->order[i - 1]].lba;
            desired_next = i + 1 < map->node_count
                               ? result->nodes[map->order[i + 1]].lba : 0;
        }

        if (node->prev == desired_prev && node->next == desired_next)
            continue;
        if (plan->patch_count >= APA_FORENSIC_MAX_PATCHES)
            return -2;

        patch = &plan->patches[plan->patch_count++];
        patch->node_index = map->order[i];
        patch->lba = node->lba;
        patch->old_next = node->next;
        patch->old_prev = node->prev;
        patch->new_next = desired_next;
        patch->new_prev = desired_prev;
        patch->checksum_corroborated = patch_checksum_corroborated(
            node, desired_next, desired_prev);
        if (patch->checksum_corroborated)
            plan->corroborated_count++;
        else
            plan->speculative_count++;
    }

    plan->automatic_safe = !result->truncated &&
                           plan->patch_count > 0 &&
                           plan->speculative_count == 0 &&
                           map->confidence >= 90;
    plan->manual_allowed = !result->truncated &&
                           plan->patch_count > 0 &&
                           map->confidence >= 90 &&
                           map->conflicts == 0 && map->overlaps == 0;
    return 0;
}

int apa_forensic_build_patched_header(
    const apa_forensic_result_t *result,
    const apa_forensic_patch_t *patch,
    unsigned char repaired[APA_HEADER_SIZE])
{
    const apa_forensic_node_t *node;

    if (result == NULL || patch == NULL || repaired == NULL ||
        patch->node_index >= result->node_count)
        return -1;
    node = &result->nodes[patch->node_index];
    if (node->lba != patch->lba || node->next != patch->old_next ||
        node->prev != patch->old_prev)
        return -2;

    memcpy(repaired, node->header, APA_HEADER_SIZE);
    write_le32_forensic(repaired + APA_NEXT_OFFSET, patch->new_next);
    write_le32_forensic(repaired + APA_PREV_OFFSET, patch->new_prev);
    write_le32_forensic(repaired, apa_checksum(repaired));
    return 0;
}

const char *apa_forensic_map_name(apa_forensic_map_kind_t kind)
{
    switch (kind) {
        case APA_FORENSIC_MAP_FORWARD:
            return "forward links";
        case APA_FORENSIC_MAP_REVERSE:
            return "reverse links";
        case APA_FORENSIC_MAP_GEOMETRY:
            return "geometry order";
        default:
            return "unknown";
    }
}
