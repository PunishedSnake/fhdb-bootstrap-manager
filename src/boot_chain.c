/*
 * Portable boot-chain parsing and classification policy.
 *
 * No PS2SDK headers or device calls belong here. The hardware scanner feeds
 * observable evidence into boot_chain_info_t; this module turns configuration
 * text and that evidence into deterministic labels that can be regression-
 * tested on a normal host before the EE code is changed.
 */

#include "boot_chain.h"

#include <string.h>

static int ascii_equal_nocase(char a, char b)
{
    if (a >= 'A' && a <= 'Z')
        a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
        b = (char)(b - 'A' + 'a');
    return a == b;
}

/* Bounded assignment for diagnostic labels and paths. The old implementation
 * routed every plain "%s" assignment through snprintf even though no numeric
 * formatting was required. Keep snprintf-like truncation/NUL semantics without
 * dragging the general formatter through the classification path. */
static void copy_text(char *destination, unsigned int capacity,
                      const char *source)
{
    unsigned int copied = 0;

    if (destination == NULL || capacity == 0)
        return;
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    while (copied + 1u < capacity && source[copied] != '\0') {
        destination[copied] = source[copied];
        copied++;
    }
    destination[copied] = '\0';
}

/* Find a simple key/value entry while ignoring comments and whitespace. */
int config_value(const char *text, const char *key,
                 char *value, unsigned int value_capacity)
{
    const char *line;
    size_t key_length;

    if (text == NULL || key == NULL || value == NULL || value_capacity == 0)
        return 0;

    line = text;
    key_length = strlen(key);
    value[0] = '\0';

    while (*line != '\0') {
        const char *cursor = line;
        const char *end = strchr(line, '\n');
        size_t i;

        if (end == NULL)
            end = line + strlen(line);
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                                *cursor == '\r'))
            cursor++;
        if (cursor < end && *cursor != '#' &&
            (size_t)(end - cursor) >= key_length) {
            for (i = 0; i < key_length; i++) {
                if (!ascii_equal_nocase(cursor[i], key[i]))
                    break;
            }
            if (i == key_length) {
                cursor += key_length;
                while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
                    cursor++;
                if (cursor < end && *cursor == '=') {
                    unsigned int copied = 0;

                    cursor++;
                    while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
                        cursor++;
                    while (cursor < end && copied + 1 < value_capacity &&
                           *cursor != '#' && *cursor != '\r')
                        value[copied++] = *cursor++;
                    while (copied > 0 &&
                           (value[copied - 1] == ' ' ||
                            value[copied - 1] == '\t'))
                        copied--;
                    value[copied] = '\0';
                    return 1;
                }
            }
        }
        line = *end == '\0' ? end : end + 1;
    }
    return 0;
}

static int parse_boolean_value(const char *value)
{
    if (value == NULL || value[0] == '\0')
        return -2;
    if (value[0] == '1' || ascii_equal_nocase(value[0], 'y') ||
        ascii_equal_nocase(value[0], 't') ||
        (ascii_equal_nocase(value[0], 'o') &&
         ascii_equal_nocase(value[1], 'n')))
        return 1;
    if (value[0] == '0' || ascii_equal_nocase(value[0], 'n') ||
        ascii_equal_nocase(value[0], 'f') ||
        (ascii_equal_nocase(value[0], 'o') &&
         ascii_equal_nocase(value[1], 'f')))
        return 0;
    return -2;
}

/* Prefer FMCB's current spelling, then accept the historical compatibility key. */
int parse_skip_hdd_text(const char *text)
{
    char value[32];

    if (!config_value(text, "OSDSYS_Skip_HDD", value, sizeof(value)) &&
        !config_value(text, "Skip_HDD", value, sizeof(value)))
        return -2;
    return parse_boolean_value(value);
}

/* Return the same target values used by the stable Torii scanner. */
int parse_osdmenu_boot_auto(const char *text, char *value,
                            unsigned int value_capacity)
{
    if (!config_value(text, "boot_auto", value, value_capacity))
        return BOOT_AUTO_NONE;
    if (strcmp(value, "$PSBBN") == 0)
        return BOOT_AUTO_PSBBN;
    if (strcmp(value, "$HOSDSYS") == 0)
        return BOOT_AUTO_HOSDSYS;
    return BOOT_AUTO_CUSTOM;
}

/* Preserve the stable E1 -> E2 -> E3 lookup order from FREEHDB.CNF. */
int find_fhdb_auto_target(const char *text, char *value,
                          unsigned int value_capacity)
{
    return config_value(text, "LK_Auto_E1", value, value_capacity) ||
           config_value(text, "LK_Auto_E2", value, value_capacity) ||
           config_value(text, "LK_Auto_E3", value, value_capacity);
}

/* Map ROMVER's region character to the memory-card system folder family. */
void expected_system_folder(const char *romver, char *destination,
                            unsigned int capacity)
{
    const char *folder = "unknown";

    if (romver != NULL && strlen(romver) >= 5) {
        switch (romver[4]) {
            case 'J': folder = "BIEXEC-SYSTEM"; break;
            case 'A':
            case 'H': folder = "BAEXEC-SYSTEM"; break;
            case 'E': folder = "BEEXEC-SYSTEM"; break;
            case 'C': folder = "BCEXEC-SYSTEM"; break;
            default: break;
        }
    }
    copy_text(destination, capacity, folder);
}

/*
 * Classify probable family separately from the evidence collector. Explicit
 * OSDMenu configuration intentionally outranks stale partitions left by an old
 * installation, matching the safety fix introduced in Torii.
 */
void classify_boot_chain(boot_chain_info_t *info,
                         unsigned int start, unsigned int sectors)
{
    if (info == NULL)
        return;

    if ((start == 0) != (sectors == 0)) {
        copy_text(info->family, sizeof(info->family), "Invalid pointer state");
        copy_text(info->confidence, sizeof(info->confidence), "certain");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  "none safely inferable");
        return;
    }
    if (start == 0) {
        copy_text(info->family, sizeof(info->family), "No active payload");
        copy_text(info->confidence, sizeof(info->confidence), "certain");
        copy_text(info->next_stage, sizeof(info->next_stage), "none");
        return;
    }
    if (info->payload_read_result < 0) {
        copy_text(info->family, sizeof(info->family), "Unreadable payload");
        copy_text(info->confidence, sizeof(info->confidence), "certain");
        copy_text(info->next_stage, sizeof(info->next_stage), "unknown");
        return;
    }
    if (info->payload_kelf_result < 0) {
        copy_text(info->family, sizeof(info->family), "Other / invalid KELF");
        copy_text(info->confidence, sizeof(info->confidence), "high");
        copy_text(info->next_stage, sizeof(info->next_stage), "unknown");
        return;
    }

    if (info->osdmenu_mbr_config &&
        info->osdmenu_auto_target == BOOT_AUTO_PSBBN) {
        copy_text(info->family, sizeof(info->family), "PSBBN / OSDMenu MBR");
        copy_text(info->confidence, sizeof(info->confidence),
                  info->psbbn_boot ? "high" : "medium");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  "hdd0:__system:pfs:/p2lboot/osdboot.elf");
    } else if (info->osdmenu_mbr_config &&
               info->osdmenu_auto_target == BOOT_AUTO_HOSDSYS) {
        copy_text(info->family, sizeof(info->family),
                  "HDD-OSD / OSDMenu MBR");
        copy_text(info->confidence, sizeof(info->confidence),
                  (info->hosdmenu_boot || info->hddosd_boot) ?
                      "high" : "medium");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  info->hosdmenu_boot ?
                      "hdd0:__system:pfs:/osdmenu/hosdmenu.elf" :
                  info->hddosd_boot ? info->hddosd_path :
                      "OSDMenu built-in $HOSDSYS target");
    } else if (info->osdmenu_mbr_config &&
               info->osdmenu_auto_target == BOOT_AUTO_CUSTOM) {
        copy_text(info->family, sizeof(info->family), "Custom OSDMenu MBR");
        copy_text(info->confidence, sizeof(info->confidence), "high");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  info->osdmenu_auto_path[0] != '\0' ?
                      info->osdmenu_auto_path : "custom target");
    } else if (info->fhdb_config) {
        copy_text(info->family, sizeof(info->family), "FHDB-compatible MBR");
        copy_text(info->confidence, sizeof(info->confidence), "medium");
        if (info->legacy_osdmain)
            copy_text(info->next_stage, sizeof(info->next_stage),
                      "hdd0:__system:pfs:/osd/osdmain.elf");
        else if (info->hddosd_boot)
            copy_text(info->next_stage, sizeof(info->next_stage),
                      "hdd0:__system:pfs:/osd100 + __sysconf:/FMCB");
        else
            copy_text(info->next_stage, sizeof(info->next_stage),
                      "hdd0:__sysconf:pfs:/FMCB (OSD stage unknown)");
    } else if (info->hosdmenu_boot || info->osdmenu_mbr_config) {
        copy_text(info->family, sizeof(info->family), "HOSDMenu / OSDMenu MBR");
        copy_text(info->confidence, sizeof(info->confidence), "high");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  "hdd0:__system:pfs:/osdmenu/hosdmenu.elf");
    } else if (info->psbbn_boot || info->psbbn_partition_count >= 4) {
        copy_text(info->family, sizeof(info->family), "PSBBN-compatible MBR");
        copy_text(info->confidence, sizeof(info->confidence), "medium");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  "hdd0:__system:pfs:/p2lboot/osdboot.elf");
    } else if (info->hddosd_boot || info->legacy_osdmain) {
        copy_text(info->family, sizeof(info->family),
                  "HDD-OSD-compatible MBR");
        copy_text(info->confidence, sizeof(info->confidence), "medium");
        copy_text(info->next_stage, sizeof(info->next_stage),
                  info->hddosd_boot ? info->hddosd_path :
                      "hdd0:__system:pfs:/osd/osdmain.elf");
    } else {
        copy_text(info->family, sizeof(info->family), "Other / unknown KELF");
        copy_text(info->confidence, sizeof(info->confidence), "low");
        copy_text(info->next_stage, sizeof(info->next_stage), "not detected");
    }
}
