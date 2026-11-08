/*
 * Portable whole-disk layout classifier: pure APA, APA-Jail, or blocked.
 * See include/apa_layout.h for the recognised APA-Jail contract.
 */

#include "apa_layout.h"

#include <string.h>

#include "apa.h"

static const unsigned char jail_magic[APA_LAYOUT_JAIL_MAGIC_SIZE] = {
    'A', 'P', 'A', 'J', '-', 'A', '2', 0
};

static void block(apa_layout_t *layout, apa_layout_reason_t reason)
{
    layout->kind = APA_LAYOUT_BLOCKED;
    layout->reason = reason;
    layout->fence_lba = 0;
}

void apa_layout_classify(const unsigned char *header, uint32_t disk_sectors,
                         apa_layout_t *layout)
{
    unsigned int i;
    unsigned int container = APA_LAYOUT_PC_ENTRIES;
    uint64_t container_end;
    uint64_t fence = UINT64_MAX;
    uint64_t reserved_end;

    memset(layout, 0, sizeof(*layout));
    layout->fence_lba = UINT32_MAX;
    if (header[PC_MBR_SIGNATURE_OFFSET] != 0x55 ||
        header[PC_MBR_SIGNATURE_OFFSET + 1] != 0xaa) {
        layout->kind = APA_LAYOUT_PURE;
        return;
    }
    if (memcmp(header + APA_LAYOUT_GPT_HEADER_OFFSET, "EFI PART", 8) == 0) {
        block(layout, APA_LAYOUT_REASON_GPT_HEADER);
        return;
    }

    /* Parse the table first so a protective entry is always reported as
       GPT even if somebody also stamped the APA-Jail magic. */
    for (i = 0; i < APA_LAYOUT_PC_ENTRIES; i++) {
        const unsigned char *entry = header + APA_LAYOUT_PC_TABLE_OFFSET +
                                     i * APA_LAYOUT_PC_ENTRY_SIZE;
        apa_layout_pc_entry_t parsed;

        parsed.type = entry[4];
        parsed.start = read_le32(entry + 8);
        parsed.sectors = read_le32(entry + 12);
        if (parsed.type == 0 && parsed.start == 0 && parsed.sectors == 0)
            continue;
        if (parsed.type == APA_LAYOUT_PC_TYPE_GPT_PROTECTIVE) {
            block(layout, APA_LAYOUT_REASON_GPT_PROTECTIVE);
            return;
        }
        layout->entries[layout->entry_count++] = parsed;
    }
    if (memcmp(header + APA_LAYOUT_JAIL_MAGIC_OFFSET, jail_magic,
               sizeof(jail_magic)) != 0) {
        block(layout, APA_LAYOUT_REASON_UNRECOGNISED);
        return;
    }
    if (layout->entry_count < 2u) {
        block(layout, layout->entry_count == 0
                          ? APA_LAYOUT_REASON_EMPTY_TABLE
                          : APA_LAYOUT_REASON_NO_FENCE);
        return;
    }

    for (i = 0; i < layout->entry_count; i++) {
        const apa_layout_pc_entry_t *entry = &layout->entries[i];

        if (entry->type == 0 || entry->start == 0 || entry->sectors == 0 ||
            (uint64_t)entry->start + entry->sectors > UINT32_MAX) {
            block(layout, APA_LAYOUT_REASON_BAD_ENTRY);
            return;
        }
        if (disk_sectors != 0 &&
            (uint64_t)entry->start + entry->sectors > disk_sectors) {
            block(layout, APA_LAYOUT_REASON_BEYOND_DISK);
            return;
        }
        if (container == APA_LAYOUT_PC_ENTRIES ||
            entry->start < layout->entries[container].start)
            container = i;
    }
    if (layout->entries[container].type != APA_LAYOUT_PC_TYPE_HIDDEN_IFS) {
        block(layout, APA_LAYOUT_REASON_NO_CONTAINER);
        return;
    }

    container_end = (uint64_t)layout->entries[container].start +
                    layout->entries[container].sectors;
    for (i = 0; i < layout->entry_count; i++) {
        if (i == container)
            continue;
        if (layout->entries[i].start < fence)
            fence = layout->entries[i].start;
    }
    if (container_end > fence) {
        block(layout, APA_LAYOUT_REASON_OVERLAP);
        return;
    }

    /* The fence must leave the whole __mbr partition and any active
       bootstrap payload strictly inside APA space. */
    reserved_end = read_le32(header + APA_START_OFFSET) +
                   (uint64_t)read_le32(header + APA_LENGTH_OFFSET);
    if ((uint64_t)read_le32(header + APA_OSD_START_OFFSET) +
            read_le32(header + APA_OSD_SIZE_OFFSET) > reserved_end)
        reserved_end = (uint64_t)read_le32(header + APA_OSD_START_OFFSET) +
                       read_le32(header + APA_OSD_SIZE_OFFSET);
    if (fence <= reserved_end) {
        block(layout, APA_LAYOUT_REASON_FENCE_TOO_LOW);
        return;
    }

    layout->kind = APA_LAYOUT_JAIL;
    layout->reason = APA_LAYOUT_REASON_NONE;
    layout->fence_lba = (uint32_t)fence;
}

int apa_layout_extent_allowed(const apa_layout_t *layout,
                              uint32_t start, uint32_t sectors)
{
    if (layout == NULL || layout->kind == APA_LAYOUT_BLOCKED)
        return 0;
    if (layout->kind == APA_LAYOUT_PURE)
        return 1;
    return (uint64_t)start + sectors <= layout->fence_lba;
}

uint32_t apa_layout_clip_sectors(const apa_layout_t *layout,
                                 uint32_t start, uint32_t sectors)
{
    uint64_t end = (uint64_t)start + sectors;

    if (layout == NULL || layout->kind == APA_LAYOUT_BLOCKED)
        return 0;
    if (layout->kind == APA_LAYOUT_PURE)
        return sectors;
    if (start >= layout->fence_lba)
        return 0;
    if (end > layout->fence_lba)
        end = layout->fence_lba;
    return (uint32_t)(end - start);
}

static apa_layout_t session_layout = {
    APA_LAYOUT_PURE, APA_LAYOUT_REASON_NONE, UINT32_MAX, 0, {{0, 0, 0}}
};

void apa_layout_session_set(const apa_layout_t *layout)
{
    if (layout != NULL)
        session_layout = *layout;
}

const apa_layout_t *apa_layout_session(void)
{
    return &session_layout;
}

const char *apa_layout_kind_name(apa_layout_kind_t kind)
{
    switch (kind) {
        case APA_LAYOUT_PURE: return "APA";
        case APA_LAYOUT_JAIL: return "APA-Jail A2";
        case APA_LAYOUT_BLOCKED: return "blocked hybrid";
        default: return "unknown";
    }
}

const char *apa_layout_reason_name(apa_layout_reason_t reason)
{
    switch (reason) {
        case APA_LAYOUT_REASON_NONE: return "none";
        case APA_LAYOUT_REASON_GPT_HEADER: return "GPT header at LBA 1";
        case APA_LAYOUT_REASON_GPT_PROTECTIVE: return "protective MBR entry 0xEE";
        case APA_LAYOUT_REASON_UNRECOGNISED: return "PC signature without APA-Jail magic";
        case APA_LAYOUT_REASON_EMPTY_TABLE: return "APA-Jail magic with empty PC table";
        case APA_LAYOUT_REASON_BAD_ENTRY: return "malformed PC partition entry";
        case APA_LAYOUT_REASON_NO_CONTAINER: return "lowest PC entry is not the APA container";
        case APA_LAYOUT_REASON_NO_FENCE: return "no PC partition after APA container";
        case APA_LAYOUT_REASON_OVERLAP: return "APA container overlaps a PC partition";
        case APA_LAYOUT_REASON_FENCE_TOO_LOW: return "PC partition intrudes on __mbr";
        case APA_LAYOUT_REASON_BEYOND_DISK: return "PC partition ends beyond disk";
        default: return "unknown";
    }
}
