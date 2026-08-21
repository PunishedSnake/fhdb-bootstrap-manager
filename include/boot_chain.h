#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_CHAIN_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_CHAIN_H

#include "capsule_format.h"

/* Explicit values keep OSDMenu target evidence stable across host and EE code. */
#define BOOT_AUTO_NONE 0
#define BOOT_AUTO_PSBBN 1
#define BOOT_AUTO_HOSDSYS 2
#define BOOT_AUTO_CUSTOM 3

/*
 * Read-only evidence model shared by the PS2 scanner, report renderer, rescue
 * metadata, and portable classification tests. 0.4 moves this type out of
 * main.c before changing any of the evidence-gathering semantics.
 */
typedef struct {
    char romver[RESCUE_CAPSULE_ROMVER_SIZE];
    char expected_system_folder[20];
    char family[RESCUE_CAPSULE_FAMILY_SIZE];
    char confidence[RESCUE_CAPSULE_CONFIDENCE_SIZE];
    char next_stage[96];
    int pointer_consistent;
    int payload_read_result;
    int payload_kelf_result;
    unsigned int payload_bytes;
    unsigned int kelf_file_bytes;
    unsigned char payload_sha256[32];
    unsigned char kelf_sha256[32];
    int skip_hdd[3];
    int mc_module_count[2];
    unsigned int mc_folder_mask[2];
    unsigned int mc_folder_module_mask[2][4];
    int fhdb_config;
    int fhdb_boot_elf;
    char fhdb_config_path[64];
    char fhdb_auto_path[96];
    int osdmenu_mbr_config;
    int osdmenu_auto_target;
    char osdmenu_auto_path[96];
    int psbbn_boot;
    int psbbn_partition_count;
    int hosdmenu_boot;
    int hddosd_boot;
    char hddosd_path[96];
    int legacy_osdmain;
    int sysconf_mount_result;
    int system_mount_result;
} boot_chain_info_t;

/* Portable CNF/config helpers. */
int config_value(const char *text, const char *key,
                 char *value, unsigned int value_capacity);
int parse_skip_hdd_text(const char *text);
int parse_osdmenu_boot_auto(const char *text, char *value,
                            unsigned int value_capacity);
int find_fhdb_auto_target(const char *text, char *value,
                          unsigned int value_capacity);

/* Portable console-region and evidence-classification policy. */
void expected_system_folder(const char *romver, char *destination,
                            unsigned int capacity);
void classify_boot_chain(boot_chain_info_t *info,
                         unsigned int start, unsigned int sectors);

#endif
