#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APA_LAYOUT_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APA_LAYOUT_H

/*
 * Whole-disk layout classification for the APA master header.
 *
 * Historically the manager treated any PC 0x55AA signature in sector zero as
 * an unsupported hybrid APA/GPT disk. Modern PSBBN Definitive Project and
 * HOSDMenu installations deliberately use Berion's "APA-Jail" scheme instead:
 * a normal APA chain occupies the beginning of the disk and a PC MBR table in
 * the APA header padding exposes an exFAT partition after it.
 *
 * APA-Jail type A2, as written by the PSBBN Definitive installers:
 *
 *   header[0x0e0..0x0e7] = "APAJ-A2\0"
 *   PC MBR entry 1        type 0x17, starts near LBA 2048, covers APA space
 *   PC MBR entry 2        type 0x17, 32 MiB ext2 "RECOVERY"
 *   PC MBR entry 3        type 0x07, exFAT game/app storage
 *   APA checksum          recomputed over the modified header
 *
 * The first non-container PC partition is the "jail fence": APA must never
 * allocate at or beyond it, because ps2hdd would happily append into exFAT.
 *
 * This module is pure and host-testable. It never trusts a layout it does not
 * positively recognise: real GPT/protective MBRs, unrecognised hybrids and
 * overlapping tables remain blocked exactly as before.
 */

#include <stdint.h>

#define APA_LAYOUT_JAIL_MAGIC_OFFSET 0x0e0u
#define APA_LAYOUT_JAIL_MAGIC_SIZE 8u
#define APA_LAYOUT_PC_TABLE_OFFSET 0x1beu
#define APA_LAYOUT_PC_ENTRY_SIZE 16u
#define APA_LAYOUT_PC_ENTRIES 4u
#define APA_LAYOUT_GPT_HEADER_OFFSET 0x200u
#define APA_LAYOUT_PC_TYPE_HIDDEN_IFS 0x17u
#define APA_LAYOUT_PC_TYPE_GPT_PROTECTIVE 0xeeu

typedef enum {
    APA_LAYOUT_PURE = 0,     /* no PC signature: classic APA disk */
    APA_LAYOUT_JAIL = 1,     /* recognised APA-Jail type A2 */
    APA_LAYOUT_BLOCKED = 2   /* anything mixed we do not positively accept */
} apa_layout_kind_t;

typedef enum {
    APA_LAYOUT_REASON_NONE = 0,
    APA_LAYOUT_REASON_GPT_HEADER,        /* "EFI PART" at LBA 1 */
    APA_LAYOUT_REASON_GPT_PROTECTIVE,    /* PC entry type 0xEE */
    APA_LAYOUT_REASON_UNRECOGNISED,      /* 0x55AA without APA-Jail magic */
    APA_LAYOUT_REASON_EMPTY_TABLE,       /* magic present, no usable entries */
    APA_LAYOUT_REASON_BAD_ENTRY,         /* zero start/length or 32-bit wrap */
    APA_LAYOUT_REASON_NO_CONTAINER,      /* lowest entry is not type 0x17 */
    APA_LAYOUT_REASON_NO_FENCE,          /* nothing after the APA container */
    APA_LAYOUT_REASON_OVERLAP,           /* container overlaps a later entry */
    APA_LAYOUT_REASON_FENCE_TOO_LOW,     /* fence intrudes on __mbr/bootstrap */
    APA_LAYOUT_REASON_BEYOND_DISK        /* entry ends past the known disk */
} apa_layout_reason_t;

typedef struct {
    uint8_t type;
    uint32_t start;
    uint32_t sectors;
} apa_layout_pc_entry_t;

typedef struct {
    apa_layout_kind_t kind;
    apa_layout_reason_t reason;
    /* First LBA APA must not reach. UINT32_MAX for a pure APA disk. */
    uint32_t fence_lba;
    unsigned int entry_count;
    apa_layout_pc_entry_t entries[APA_LAYOUT_PC_ENTRIES];
} apa_layout_t;

/*
 * Classify a complete 1024-byte master header. disk_sectors may be 0 when the
 * driver geometry is not yet known; it is then simply not checked.
 */
void apa_layout_classify(const unsigned char *header, uint32_t disk_sectors,
                         apa_layout_t *layout);

/* 1 when [start, start + sectors) is entirely usable by APA. */
int apa_layout_extent_allowed(const apa_layout_t *layout,
                              uint32_t start, uint32_t sectors);

/* Sectors of [start, start + sectors) that lie below the fence. */
uint32_t apa_layout_clip_sectors(const apa_layout_t *layout,
                                 uint32_t start, uint32_t sectors);

/*
 * Session copy of the layout admitted at startup. Every later writer re-reads
 * and re-classifies the live header itself; this copy is for reporting and
 * for code that has no header buffer of its own.
 */
void apa_layout_session_set(const apa_layout_t *layout);
const apa_layout_t *apa_layout_session(void);

const char *apa_layout_kind_name(apa_layout_kind_t kind);
const char *apa_layout_reason_name(apa_layout_reason_t reason);

#endif
