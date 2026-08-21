/* Portable regression fixtures for boot-chain parsing and classification. */

#include "boot_chain.h"

#include <stdio.h>
#include <string.h>

static int expect_string(const char *actual, const char *expected,
                         const char *context)
{
    if (strcmp(actual, expected) == 0)
        return 1;
    fprintf(stderr, "%s: expected '%s', got '%s'\n",
            context, expected, actual);
    return 0;
}

static void valid_active_chain(boot_chain_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->pointer_consistent = 1;
    info->payload_read_result = 0;
    info->payload_kelf_result = 0;
}

static int test_config_parser(void)
{
    static const char text[] =
        "# comment\r\n"
        "  OSDSYS_Skip_HDD = ON   # inline comment\r\n"
        "Boot_Auto = $PSBBN\n"
        "LK_Auto_E2 = hdd0:__system:pfs:/BOOT.ELF   \n"
        "Skip_HDD_EXTRA = 0\n";
    char value[64];
    char short_value[5];

    if (!config_value(text, "osdsys_skip_hdd", value, sizeof(value)) ||
        strcmp(value, "ON") != 0)
        return 0;
    if (!config_value(text, "boot_auto", value, sizeof(value)) ||
        strcmp(value, "$PSBBN") != 0)
        return 0;
    if (config_value(text, "Skip_HDD", value, sizeof(value)))
        return 0; /* A longer key must not be accepted as an exact match. */
    if (!config_value(text, "LK_Auto_E2", short_value, sizeof(short_value)) ||
        strcmp(short_value, "hdd0") != 0)
        return 0; /* Bounded copies remain NUL-terminated. */
    if (config_value(NULL, "key", value, sizeof(value)) ||
        config_value(text, NULL, value, sizeof(value)) ||
        config_value(text, "key", NULL, sizeof(value)) ||
        config_value(text, "key", value, 0))
        return 0;
    return 1;
}

static int test_skip_hdd_parser(void)
{
    static const struct {
        const char *text;
        int expected;
    } cases[] = {
        {"OSDSYS_Skip_HDD = 1\n", 1},
        {"OSDSYS_Skip_HDD = yes\n", 1},
        {"OSDSYS_Skip_HDD = TRUE\n", 1},
        {"OSDSYS_Skip_HDD = on\n", 1},
        {"OSDSYS_Skip_HDD = 0\n", 0},
        {"OSDSYS_Skip_HDD = no\n", 0},
        {"OSDSYS_Skip_HDD = false\n", 0},
        {"OSDSYS_Skip_HDD = OFF\n", 0},
        {"Skip_HDD = 1\n", 1},
        {"Skip_HDD = 0\n", 0},
        {"OSDSYS_Skip_HDD = maybe\n", -2},
        {"OTHER = 1\n", -2}
    };
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (parse_skip_hdd_text(cases[i].text) != cases[i].expected)
            return 0;
    }

    /* Current spelling has priority over a contradictory legacy key. */
    return parse_skip_hdd_text("Skip_HDD=1\nOSDSYS_Skip_HDD=0\n") == 0;
}

static int test_boot_auto_parsers(void)
{
    char value[96];

    if (parse_osdmenu_boot_auto("boot_auto=$PSBBN\n", value,
                                sizeof(value)) != BOOT_AUTO_PSBBN ||
        strcmp(value, "$PSBBN") != 0)
        return 0;
    if (parse_osdmenu_boot_auto("boot_auto = $HOSDSYS\n", value,
                                sizeof(value)) != BOOT_AUTO_HOSDSYS)
        return 0;
    if (parse_osdmenu_boot_auto("boot_auto = mass:/BOOT.ELF\n", value,
                                sizeof(value)) != BOOT_AUTO_CUSTOM ||
        strcmp(value, "mass:/BOOT.ELF") != 0)
        return 0;
    if (parse_osdmenu_boot_auto("other=1\n", value,
                                sizeof(value)) != BOOT_AUTO_NONE)
        return 0;

    if (!find_fhdb_auto_target("LK_Auto_E3=three\nLK_Auto_E2=two\n",
                               value, sizeof(value)) ||
        strcmp(value, "two") != 0)
        return 0;
    if (!find_fhdb_auto_target("LK_Auto_E3=three\n", value,
                               sizeof(value)) ||
        strcmp(value, "three") != 0)
        return 0;
    return !find_fhdb_auto_target("name=value\n", value, sizeof(value));
}

static int test_romver_regions(void)
{
    static const struct {
        const char *romver;
        const char *folder;
    } cases[] = {
        {"0220JC20060905", "BIEXEC-SYSTEM"},
        {"0230AC20080220", "BAEXEC-SYSTEM"},
        {"0230HC20080220", "BAEXEC-SYSTEM"},
        {"0230EC20080220", "BEEXEC-SYSTEM"},
        {"0230CC20080220", "BCEXEC-SYSTEM"},
        {"0230XC20080220", "unknown"},
        {"bad", "unknown"}
    };
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char folder[20];
        expected_system_folder(cases[i].romver, folder, sizeof(folder));
        if (strcmp(folder, cases[i].folder) != 0)
            return 0;
    }
    return 1;
}

static int test_pointer_and_payload_states(void)
{
    boot_chain_info_t info;

    valid_active_chain(&info);
    classify_boot_chain(&info, 0x2000, 0);
    if (!expect_string(info.family, "Invalid pointer state", "pointer mismatch"))
        return 0;

    valid_active_chain(&info);
    classify_boot_chain(&info, 0, 0);
    if (!expect_string(info.family, "No active payload", "disabled pointer"))
        return 0;

    valid_active_chain(&info);
    info.payload_read_result = -173;
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "Unreadable payload", "unreadable payload"))
        return 0;

    valid_active_chain(&info);
    info.payload_kelf_result = -4;
    classify_boot_chain(&info, 0x2000, 4);
    return expect_string(info.family, "Other / invalid KELF", "invalid KELF");
}

static int test_explicit_osdmenu_priority(void)
{
    boot_chain_info_t info;

    valid_active_chain(&info);
    info.osdmenu_mbr_config = 1;
    info.osdmenu_auto_target = BOOT_AUTO_PSBBN;
    info.psbbn_boot = 1;
    info.hddosd_boot = 1;             /* Deliberately conflicting stale evidence. */
    info.psbbn_partition_count = 0;
    strcpy(info.hddosd_path, "stale-hddosd");
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "PSBBN / OSDMenu MBR", "explicit PSBBN"))
        return 0;
    if (!expect_string(info.confidence, "high", "explicit PSBBN confidence"))
        return 0;

    valid_active_chain(&info);
    info.osdmenu_mbr_config = 1;
    info.osdmenu_auto_target = BOOT_AUTO_HOSDSYS;
    info.hosdmenu_boot = 1;
    info.psbbn_boot = 1;              /* Must not outrank explicit $HOSDSYS. */
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "HDD-OSD / OSDMenu MBR", "explicit HOSDSYS"))
        return 0;
    if (!expect_string(info.next_stage,
                       "hdd0:__system:pfs:/osdmenu/hosdmenu.elf",
                       "HOSDSYS stage priority"))
        return 0;

    valid_active_chain(&info);
    info.osdmenu_mbr_config = 1;
    info.osdmenu_auto_target = BOOT_AUTO_CUSTOM;
    strcpy(info.osdmenu_auto_path, "mass:/CUSTOM.ELF");
    info.psbbn_partition_count = 8;
    classify_boot_chain(&info, 0x2000, 4);
    return expect_string(info.family, "Custom OSDMenu MBR", "custom target") &&
           expect_string(info.next_stage, "mass:/CUSTOM.ELF", "custom stage");
}

static int test_heuristic_family_fallbacks(void)
{
    boot_chain_info_t info;

    valid_active_chain(&info);
    info.fhdb_config = 1;
    info.legacy_osdmain = 1;
    info.psbbn_partition_count = 8;
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "FHDB-compatible MBR", "FHDB evidence"))
        return 0;

    valid_active_chain(&info);
    info.hosdmenu_boot = 1;
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "HOSDMenu / OSDMenu MBR", "HOSDMenu evidence"))
        return 0;

    valid_active_chain(&info);
    info.psbbn_partition_count = 4;
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "PSBBN-compatible MBR", "PSBBN partitions"))
        return 0;

    valid_active_chain(&info);
    info.hddosd_boot = 1;
    strcpy(info.hddosd_path, "hdd0:__system:pfs:/osd100/hosdsys.elf");
    classify_boot_chain(&info, 0x2000, 4);
    if (!expect_string(info.family, "HDD-OSD-compatible MBR", "HDD-OSD evidence"))
        return 0;

    valid_active_chain(&info);
    classify_boot_chain(&info, 0x2000, 4);
    return expect_string(info.family, "Other / unknown KELF", "unknown KELF") &&
           expect_string(info.confidence, "low", "unknown confidence");
}

int main(void)
{
    if (!test_config_parser()) {
        fprintf(stderr, "Boot-chain config parser tests failed.\n");
        return 1;
    }
    if (!test_skip_hdd_parser()) {
        fprintf(stderr, "Skip_HDD parser tests failed.\n");
        return 2;
    }
    if (!test_boot_auto_parsers()) {
        fprintf(stderr, "Boot target parser tests failed.\n");
        return 3;
    }
    if (!test_romver_regions()) {
        fprintf(stderr, "ROMVER region mapping tests failed.\n");
        return 4;
    }
    if (!test_pointer_and_payload_states()) {
        fprintf(stderr, "Pointer/payload classification tests failed.\n");
        return 5;
    }
    if (!test_explicit_osdmenu_priority()) {
        fprintf(stderr, "Explicit OSDMenu priority tests failed.\n");
        return 6;
    }
    if (!test_heuristic_family_fallbacks()) {
        fprintf(stderr, "Heuristic family classification tests failed.\n");
        return 7;
    }

    puts("All boot-chain parser and classification tests passed.");
    return 0;
}
