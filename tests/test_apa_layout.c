/*
 * Host tests for the whole-disk layout classifier.
 *
 * The APA-Jail cases reproduce what the PSBBN Definitive / HOSDMenu
 * installers write: the "APAJ-A2\0" magic at 0xE0, an sfdisk DOS table
 * ",<APA>MiB,17 / ,32MiB,17 / ,,07" with default 1 MiB alignment, the sfdisk
 * disk identifier at 0x1B8, and a recomputed APA checksum.
 */

#include <stdio.h>
#include <string.h>

#include "apa.h"
#include "apa_layout.h"

#define MIB_SECTORS 2048u
#define DISK_1TB_SECTORS 1953525168u

static int failures;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
            failures++; \
        } \
    } while (0)

static void put32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void make_master(unsigned char header[APA_HEADER_SIZE])
{
    memset(header, 0, APA_HEADER_SIZE);
    memcpy(header + APA_MAGIC_OFFSET, "APA\0", 4);
    memcpy(header + APA_ID_OFFSET, "__mbr", 5);
    put32(header + APA_LENGTH_OFFSET, 0x40000u);
    header[APA_TYPE_OFFSET] = 1;
    memcpy(header + APA_MBR_MAGIC_OFFSET,
           "Sony Computer Entertainment Inc.", 32);
    put32(header + APA_MBR_VERSION_OFFSET, APA_MASTER_VERSION_VALUE);
    put32(header + APA_OSD_START_OFFSET, 0x2000u);
    put32(header + APA_OSD_SIZE_OFFSET, 0x400u);
}

static void set_entry(unsigned char header[APA_HEADER_SIZE], unsigned int i,
                      uint8_t type, uint32_t start, uint32_t sectors)
{
    unsigned char *entry = header + APA_LAYOUT_PC_TABLE_OFFSET +
                           i * APA_LAYOUT_PC_ENTRY_SIZE;

    memset(entry, 0, APA_LAYOUT_PC_ENTRY_SIZE);
    entry[4] = type;
    put32(entry + 8, start);
    put32(entry + 12, sectors);
}

static void finish(unsigned char header[APA_HEADER_SIZE])
{
    header[PC_MBR_SIGNATURE_OFFSET] = 0x55;
    header[PC_MBR_SIGNATURE_OFFSET + 1] = 0xaa;
    put32(header, apa_checksum(header));
}

/* Exactly the PSBBN installer's sequence on a 1 TB disk, 114 GiB of APA. */
static uint32_t make_psbbn_jail(unsigned char header[APA_HEADER_SIZE],
                                uint32_t disk_sectors)
{
    const uint32_t apa = 114u * 1024u * MIB_SECTORS;
    const uint32_t p2 = MIB_SECTORS + apa;
    const uint32_t p3 = p2 + 32u * MIB_SECTORS;

    make_master(header);
    memcpy(header + APA_LAYOUT_JAIL_MAGIC_OFFSET, "APAJ-A2\0", 8);
    put32(header + 0x1b8, 0x1234abcdu); /* sfdisk disk identifier */
    set_entry(header, 0, 0x17, MIB_SECTORS, apa);
    set_entry(header, 1, 0x17, p2, 32u * MIB_SECTORS);
    set_entry(header, 2, 0x07, p3, disk_sectors - p3);
    finish(header);
    return p2;
}

static void test_pure(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_layout_t layout;

    make_master(header);
    put32(header, apa_checksum(header));
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.kind == APA_LAYOUT_PURE, "pure APA is PURE");
    CHECK(layout.fence_lba == UINT32_MAX, "pure APA has no fence");
    CHECK(apa_layout_extent_allowed(&layout, 0xf0000000u, 0x0fffffffu),
          "pure APA allows any extent");
    CHECK(apa_layout_clip_sectors(&layout, 100, 50) == 50, "pure no clip");
}

static void test_psbbn_jail(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_layout_t layout;
    uint32_t fence = make_psbbn_jail(header, DISK_1TB_SECTORS);

    CHECK(is_standard_apa_header(header),
          "APA-Jail master stays a standard checksum-valid APA master");
    CHECK(is_hybrid_gpt(header), "legacy guard still sees the PC signature");
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.kind == APA_LAYOUT_JAIL, "PSBBN layout is APA-Jail");
    CHECK(layout.fence_lba == fence, "fence is RECOVERY partition start");
    CHECK(layout.entry_count == 3, "three PC entries parsed");
    CHECK(apa_layout_extent_allowed(&layout, fence - 0x80000u, 0x80000u),
          "extent ending exactly at fence allowed");
    CHECK(!apa_layout_extent_allowed(&layout, fence - 0x80000u, 0x80001u),
          "extent crossing fence refused");
    CHECK(!apa_layout_extent_allowed(&layout, fence, 1),
          "extent at fence refused");
    CHECK(apa_layout_clip_sectors(&layout, fence - 10u, 100u) == 10u,
          "free extent clipped to fence");
    CHECK(apa_layout_clip_sectors(&layout, fence + 10u, 100u) == 0u,
          "free extent above fence contributes nothing");

    apa_layout_classify(header, 0, &layout);
    CHECK(layout.kind == APA_LAYOUT_JAIL, "unknown geometry still accepted");
    apa_layout_classify(header, DISK_1TB_SECTORS - 1u, &layout);
    CHECK(layout.kind == APA_LAYOUT_BLOCKED &&
              layout.reason == APA_LAYOUT_REASON_BEYOND_DISK,
          "table larger than disk blocked");
}

static void test_blocked(void)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_layout_t layout;
    uint32_t fence;

    /* Signature without magic: the historical apa_pc_signature_only case. */
    make_master(header);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.kind == APA_LAYOUT_BLOCKED &&
              layout.reason == APA_LAYOUT_REASON_UNRECOGNISED,
          "signature without magic blocked");

    fence = make_psbbn_jail(header, DISK_1TB_SECTORS);
    memcpy(header + APA_LAYOUT_GPT_HEADER_OFFSET, "EFI PART", 8);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_GPT_HEADER, "GPT header blocked");

    make_psbbn_jail(header, DISK_1TB_SECTORS);
    set_entry(header, 3, 0xee, 1, 0xffffffffu - 1u);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_GPT_PROTECTIVE,
          "protective entry blocked even with magic");

    make_psbbn_jail(header, DISK_1TB_SECTORS);
    set_entry(header, 1, 0, 0, 0);
    set_entry(header, 2, 0, 0, 0);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_NO_FENCE, "container alone blocked");

    make_psbbn_jail(header, DISK_1TB_SECTORS);
    set_entry(header, 0, 0x17, MIB_SECTORS, fence);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_OVERLAP, "overlap blocked");

    make_psbbn_jail(header, DISK_1TB_SECTORS);
    set_entry(header, 0, 0x07, MIB_SECTORS, fence - MIB_SECTORS);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_NO_CONTAINER,
          "non-0x17 container blocked");

    make_psbbn_jail(header, DISK_1TB_SECTORS);
    set_entry(header, 2, 0x07, 0x30000u, 0x1000u);
    set_entry(header, 0, 0x17, MIB_SECTORS, 0x30000u - MIB_SECTORS);
    set_entry(header, 1, 0, 0, 0);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_FENCE_TOO_LOW,
          "fence inside __mbr blocked");

    make_psbbn_jail(header, DISK_1TB_SECTORS);
    set_entry(header, 1, 0x17, fence, 0);
    finish(header);
    apa_layout_classify(header, DISK_1TB_SECTORS, &layout);
    CHECK(layout.reason == APA_LAYOUT_REASON_BAD_ENTRY, "zero-length entry");

    CHECK(!apa_layout_extent_allowed(&layout, 0, 1), "blocked allows nothing");
    CHECK(apa_layout_clip_sectors(&layout, 0, 1) == 0, "blocked clips all");
}

int main(void)
{
    test_pure();
    test_psbbn_jail();
    test_blocked();
    if (failures != 0) {
        fprintf(stderr, "%d APA layout test(s) failed.\n", failures);
        return 1;
    }
    puts("All APA layout classifier tests passed.");
    return 0;
}
