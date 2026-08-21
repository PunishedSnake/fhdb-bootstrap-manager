#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APA_FORENSIC_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APA_FORENSIC_H

#include <stdint.h>

#include "apa.h"

/* Standard APA allocations are aligned to at least 128 MiB. The forensic
 * scanner also follows every surviving next/prev/main/sub reference, so a
 * referenced header outside the coarse grid is still inspected directly. */
#define APA_FORENSIC_SCAN_STEP 0x40000u
#define APA_FORENSIC_MAX_NODES 512u
#define APA_FORENSIC_MAX_MAPS 3u
#define APA_FORENSIC_MAX_PATCHES APA_FORENSIC_MAX_NODES

/* Independent pieces of evidence retained for UI/reporting and repair gates. */
enum {
    APA_FORENSIC_EVIDENCE_MAGIC = 1u << 0,
    APA_FORENSIC_EVIDENCE_SELF_START = 1u << 1,
    APA_FORENSIC_EVIDENCE_CHECKSUM = 1u << 2,
    APA_FORENSIC_EVIDENCE_LENGTH = 1u << 3,
    APA_FORENSIC_EVIDENCE_NSUB = 1u << 4,
    APA_FORENSIC_EVIDENCE_TYPE = 1u << 5,
    APA_FORENSIC_EVIDENCE_ID = 1u << 6,
    APA_FORENSIC_EVIDENCE_GRID = 1u << 7,
    APA_FORENSIC_EVIDENCE_REFERENCED = 1u << 8
};

typedef int (*apa_forensic_read_fn)(void *context, uint32_t lba,
                                    unsigned int sectors,
                                    unsigned char *destination);
typedef void (*apa_forensic_progress_fn)(void *context, uint32_t lba,
                                         uint32_t total_sectors,
                                         unsigned int nodes_found);

typedef struct {
    uint32_t lba;
    uint32_t stored_checksum;
    uint32_t calculated_checksum;
    uint32_t next;
    uint32_t prev;
    uint32_t start;
    uint32_t length;
    uint32_t main;
    uint32_t nsub;
    uint32_t number;
    uint16_t type;
    uint16_t flags;
    uint32_t evidence;
    unsigned int confidence;
    char id[APA_ID_SIZE + 1u];
    unsigned char header[APA_HEADER_SIZE];
} apa_forensic_node_t;

typedef enum {
    APA_FORENSIC_MAP_FORWARD = 1,
    APA_FORENSIC_MAP_REVERSE = 2,
    APA_FORENSIC_MAP_GEOMETRY = 3
} apa_forensic_map_kind_t;

typedef struct {
    apa_forensic_map_kind_t kind;
    unsigned int node_count;
    unsigned int reciprocal_links;
    unsigned int inferred_links;
    unsigned int conflicts;
    unsigned int overlaps;
    unsigned int confidence;
    int repairable;
    unsigned short order[APA_FORENSIC_MAX_NODES];
} apa_forensic_map_t;

typedef struct {
    uint32_t total_sectors;
    uint32_t grid_step;
    unsigned int grid_reads;
    unsigned int reference_reads;
    unsigned int unreadable_reads;
    unsigned int node_count;
    unsigned int map_count;
    int truncated;
    apa_forensic_node_t nodes[APA_FORENSIC_MAX_NODES];
    apa_forensic_map_t maps[APA_FORENSIC_MAX_MAPS];
} apa_forensic_result_t;

typedef struct {
    unsigned short node_index;
    uint32_t lba;
    uint32_t old_next;
    uint32_t old_prev;
    uint32_t new_next;
    uint32_t new_prev;
    int checksum_corroborated;
} apa_forensic_patch_t;

typedef struct {
    unsigned int map_index;
    unsigned int patch_count;
    unsigned int corroborated_count;
    unsigned int speculative_count;
    unsigned int confidence;
    int automatic_safe;
    int manual_allowed;
    apa_forensic_patch_t patches[APA_FORENSIC_MAX_PATCHES];
} apa_forensic_repair_plan_t;

/*
 * Build candidate partition graphs entirely from raw read-only evidence.
 * The core performs no allocation and no PS2SDK calls. It first samples the
 * standard APA alignment grid, then directly follows surviving graph/subpart
 * references so broken master links do not prevent discovery.
 */
int apa_forensic_scan(apa_forensic_read_fn reader, void *reader_context,
                      uint32_t total_sectors,
                      apa_forensic_progress_fn progress,
                      void *progress_context,
                      apa_forensic_result_t *result);

/* Build a topology-only repair plan for one candidate map. Missing headers,
 * geometry changes and filesystem reconstruction are deliberately excluded. */
int apa_forensic_build_repair_plan(const apa_forensic_result_t *result,
                                   unsigned int map_index,
                                   apa_forensic_repair_plan_t *plan);

/* Produce one patched header without writing it. Only prev/next/checksum may
 * change, allowing host tests and the PS2 writer to share exact bytes. */
int apa_forensic_build_patched_header(
    const apa_forensic_result_t *result,
    const apa_forensic_patch_t *patch,
    unsigned char repaired[APA_HEADER_SIZE]);

const char *apa_forensic_map_name(apa_forensic_map_kind_t kind);

#endif