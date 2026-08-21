/*
 * PS2-specific, read-only boot-chain evidence collection.
 *
 * This module may mount PFS partitions and inspect memory-card/HDD files, but
 * it deliberately has no raw-sector write, APA pointer update, rescue restore,
 * or MagicGate installation responsibility.
 */

#include <kernel.h>
#include <delaythread.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_chain.h"
#include "boot_chain_ps2.h"
#include "storage.h"

#define TEXT_FILE_LIMIT 32768u

/* Parse FMCB's current or legacy Skip_HDD setting from one configuration file. */
static int read_skip_hdd_setting(const char *path)
{
    char *text = malloc(TEXT_FILE_LIMIT);
    int attempts = (selected_storage == 2 &&
                    strncmp(path, "mass:", 5) == 0) ? 20 : 1;
    int result;

    if (text == NULL)
        return -2;
    do {
        result = read_text_file(path, text, TEXT_FILE_LIMIT);
        if (result >= 0)
            break;
        DelayThread(250000);
    } while (--attempts > 0);
    if (result < 0) {
        free(text);
        return -1;
    }
    result = parse_skip_hdd_text(text);
    free(text);
    return result;
}

/* Inspect all known regional FMCB folders because cross-model cards can mix them. */
void scan_memory_card_boot_files(boot_chain_info_t *info)
{
    static const char *const folders[] = {
        "BIEXEC-SYSTEM", "BAEXEC-SYSTEM",
        "BEEXEC-SYSTEM", "BCEXEC-SYSTEM"
    };
    static const char *const modules[] = {
        "hddload.irx", "dev9.irx", "atad.irx"
    };
    unsigned int port;

    for (port = 0; port < 2; port++) {
        unsigned int folder;

        for (folder = 0; folder < sizeof(folders) / sizeof(folders[0]);
             folder++) {
            unsigned int module;
            int folder_has_module = 0;

            for (module = 0; module < sizeof(modules) / sizeof(modules[0]);
                 module++) {
                char path[96];

                snprintf(path, sizeof(path), "mc%u:/%s/%s", port,
                         folders[folder], modules[module]);
                if (path_exists(path)) {
                    info->mc_folder_module_mask[port][folder] |= 1u << module;
                    info->mc_module_count[port]++;
                    folder_has_module = 1;
                }
            }
            if (folder_has_module)
                info->mc_folder_mask[port] |= 1u << folder;
        }
    }
}

/* Read the three locations from which FMCB configurations are commonly used. */
void scan_skip_hdd_settings(boot_chain_info_t *info)
{
    info->skip_hdd[0] =
        read_skip_hdd_setting("mc0:/SYS-CONF/FREEMCB.CNF");
    info->skip_hdd[1] =
        read_skip_hdd_setting("mc1:/SYS-CONF/FREEMCB.CNF");
    info->skip_hdd[2] = read_skip_hdd_setting("mass:/FREEMCB.CNF");
}

/* Inspect __sysconf for FHDB and modern OSDMenu configuration evidence. */
void scan_sysconf_partition(boot_chain_info_t *info)
{
    char *configuration;
    char value[96];
    const char *fhdb_configuration = NULL;

    fileXioUmount("pfs0:");
    info->sysconf_mount_result =
        fileXioMount("pfs0:", "hdd0:__sysconf", FIO_MT_RDONLY);
    if (info->sysconf_mount_result < 0)
        return;

    if (path_exists("pfs0:/FMCB/FREEHDB.CNF"))
        fhdb_configuration = "pfs0:/FMCB/FREEHDB.CNF";
    else if (path_exists("pfs0:/FHDB/FREEHDB.CNF"))
        fhdb_configuration = "pfs0:/FHDB/FREEHDB.CNF";
    info->fhdb_config = fhdb_configuration != NULL;
    if (fhdb_configuration != NULL)
        snprintf(info->fhdb_config_path, sizeof(info->fhdb_config_path),
                 "%s", fhdb_configuration);
    info->fhdb_boot_elf =
        path_exists("pfs0:/FMCB/BOOT.ELF") ||
        path_exists("pfs0:/FHDB/BOOT.ELF");
    info->osdmenu_mbr_config = path_exists("pfs0:/osdmenu/OSDMBR.CNF");

    configuration = malloc(TEXT_FILE_LIMIT);
    memset(value, 0, sizeof(value));
    if (configuration != NULL && fhdb_configuration != NULL &&
        read_text_file(fhdb_configuration, configuration,
                       TEXT_FILE_LIMIT) >= 0 &&
        find_fhdb_auto_target(configuration, value, sizeof(value))) {
        snprintf(info->fhdb_auto_path, sizeof(info->fhdb_auto_path),
                 "%s", value);
    }

    memset(value, 0, sizeof(value));
    if (configuration != NULL && info->osdmenu_mbr_config &&
        read_text_file("pfs0:/osdmenu/OSDMBR.CNF", configuration,
                       TEXT_FILE_LIMIT) >= 0) {
        info->osdmenu_auto_target =
            parse_osdmenu_boot_auto(configuration, value, sizeof(value));
        if (info->osdmenu_auto_target != BOOT_AUTO_NONE)
            snprintf(info->osdmenu_auto_path,
                     sizeof(info->osdmenu_auto_path), "%s", value);
    }
    free(configuration);
    fileXioUmount("pfs0:");
}

/* Inspect __system for the executable that a bootstrap is likely to launch. */
void scan_system_partition(boot_chain_info_t *info)
{
    static const char *const psbbn_partitions[] = {
        "__linux.1", "__linux.4", "__linux.5", "__linux.6",
        "__linux.7", "__linux.8", "__linux.9", "__contents"
    };
    unsigned int i;

    fileXioUmount("pfs0:");
    info->system_mount_result =
        fileXioMount("pfs0:", "hdd0:__system", FIO_MT_RDONLY);
    if (info->system_mount_result >= 0) {
        info->psbbn_boot = path_exists("pfs0:/p2lboot/osdboot.elf");
        info->hosdmenu_boot = path_exists("pfs0:/osdmenu/hosdmenu.elf");
        if (path_exists("pfs0:/osd100/hosdsys.elf"))
            snprintf(info->hddosd_path, sizeof(info->hddosd_path),
                     "hdd0:__system:pfs:/osd100/hosdsys.elf");
        else if (path_exists("pfs0:/osd100/OSDSYS_A.XLF"))
            snprintf(info->hddosd_path, sizeof(info->hddosd_path),
                     "hdd0:__system:pfs:/osd100/OSDSYS_A.XLF");
        info->hddosd_boot = info->hddosd_path[0] != '\0';
        info->legacy_osdmain = path_exists("pfs0:/osd/osdmain.elf");
        fileXioUmount("pfs0:");
    }

    for (i = 0; i < sizeof(psbbn_partitions) /
                    sizeof(psbbn_partitions[0]); i++) {
        char path[48];

        snprintf(path, sizeof(path), "hdd0:%s", psbbn_partitions[i]);
        if (path_exists(path))
            info->psbbn_partition_count++;
    }
}
