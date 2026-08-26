#include <timer.h>

#define HDL_GAME_UI_STAGE __attribute__((noinline, optimize("Os")))

static HDL_GAME_UI_STAGE void show_game_details(const hdl_game_entry_t *game)
{
    char digest[65];

    scr_clear();
    scr_printf("Installed HDL partition\n\n");
    scr_printf("Target    : %s\n", game->id);
    scr_printf("Main LBA  : %lu\n", (unsigned long)game->main_lba);
    scr_printf("Allocation: %llu MiB\n",
               (unsigned long long)(game->allocation_bytes / 1024u / 1024u));
    if (game->metadata_state == 0) {
        sha256_hex(game->metadata_sha256, digest);
        scr_printf("Game      : %s\n", game->metadata.game_title);
        scr_printf("Startup   : %s\n", game->metadata.startup);
        scr_printf("Media     : %s\n",
                   game->metadata.disc_type == 0x14 ? "PS2 DVD" : "PS2 CD");
        scr_printf("Parts     : %u metadata / %u APA\n",
                   game->metadata.partition_count, game->apa_partition_count);
        scr_printf("Compat    : HDL %02x | OPL %02x | DMA %u/%u\n",
                   game->metadata.hdl_compat_flags,
                   game->metadata.opl_compat_flags,
                   game->metadata.dma_type, game->metadata.dma_mode);
        scr_printf("Metadata  : SHA-256 %.16s...\n", digest);
    } else {
        scr_printf("Metadata  : unreadable or structurally invalid\n");
        scr_printf("Read code : %d\n\n", game->metadata_state);
        scr_printf("Deletion remains locked because the partition cannot\n");
        scr_printf("be proven to be an installed HDL game.\n");
    }
    app_ui_wait_to_return();
}

static HDL_GAME_UI_STAGE void show_delete_result(const hdl_game_entry_t *game,
                                                 int result)
{
    scr_clear();
    if (result == 0) {
        scr_printf("HDL game removed.\n\n");
        scr_printf("Game  : %s\n", game->metadata.game_title);
        scr_printf("Target: %s\n\n", game->id);
        scr_printf("The main partition and its sub-partitions were removed.\n");
    } else {
        scr_printf("HDL game was not removed (code %d).\n\n", result);
        if (result == HDL_INSTALL_TARGET_CHANGED) {
            scr_printf("The partition identity or metadata changed after\n");
            scr_printf("the list was scanned. Rescan first.\n");
        } else if (result == HDL_INSTALL_TARGET_BUSY) {
            scr_printf("An incomplete transaction owns this exact target.\n");
        } else if (result == HDL_INSTALL_CHAIN_INVALID) {
            scr_printf("The live APA chain failed validation. No delete ran.\n");
        } else {
            scr_printf("A disk, journal or APA safety recheck refused removal.\n");
        }
    }
    app_ui_wait_to_return();
}

static HDL_GAME_UI_STAGE int game_action_menu(hdl_game_entry_t *game,
                                              uint32_t total_sectors)
{
    app_ui_menu_item_t items[] = {
        {"View details", "Inspect partition identity and HDL metadata", 1},
        {"Delete game partition", "Remove the main partition and every sub", 0}
    };
    unsigned int selected = 0;
    char status[192];

    (void)load_game_metadata(game, total_sectors);
    items[1].enabled = game->metadata_state == 0 &&
                       game->metadata.partition_count == game->apa_partition_count;
    snprintf(status, sizeof(status), "%s | %s",
             game->metadata_state == 0 ? game->metadata.game_title : game->id,
             game->metadata_state != 0 ? "metadata invalid; delete locked" :
             game->metadata.partition_count != game->apa_partition_count ?
                 "metadata/APA partition count mismatch; delete locked" :
                 game->metadata.startup);
    for (;;) {
        int choice = app_ui_menu_select("HDL game tools", status,
                                        items, 2, &selected);

        if (choice < 0)
            return 0;
        if (choice == 0) {
            show_game_details(game);
            continue;
        }
        if (choice == 1) {
            int result = delete_installed_game(game, total_sectors);

            if (result == HDL_INSTALL_CANCELLED)
                continue;
            show_delete_result(game, result);
            return result == 0;
        }
    }
}

static void hdl_catalog_cache_discard(hdl_catalog_t *catalog, int *valid)
{
    catalog_free(catalog);
    *valid = 0;
}

static int hdl_catalog_cache_load(hdl_catalog_t *catalog, int *valid)
{
    u64 started;
    u64 elapsed;
    u32 seconds = 0;
    u32 microseconds = 0;
    int result;

    if (*valid)
        return 0;

    app_ui_activity_message("Installed HDL games",
                            "Reading the APA chain directly from the HDD.");
    started = GetTimerSystemTime();
    result = scan_apa_catalog(catalog, 1);
    elapsed = GetTimerSystemTime() - started;
    TimerBusClock2USec(elapsed, &seconds, &microseconds);
    session_log_line(
        "HDL APA catalogue scan result=%d games=%u seconds=%u usec=%u",
        result, result == 0 ? catalog->count : 0u, seconds, microseconds);
    if (result < 0) {
        hdl_catalog_cache_discard(catalog, valid);
        return result;
    }

    qsort(catalog->games, catalog->count, sizeof(*catalog->games), compare_games);
    *valid = 1;
    session_log_line("HDL raw catalogue: games=%u subs=%u expected_subs=%u used=%llu free=%llu total=%u",
                     catalog->count, catalog->hdl_sub_count,
                     catalog->expected_sub_count,
                     (unsigned long long)catalog->used_sectors,
                     (unsigned long long)catalog->free_sectors,
                     catalog->total_sectors);
    return 0;
}

static int hdl_catalog_cache_remove_deleted(hdl_catalog_t *catalog,
                                            unsigned int index)
{
    const hdl_game_entry_t *game;
    uint64_t sectors;
    unsigned int subs;

    if (catalog == NULL || index >= catalog->count ||
        catalog->hdl_sub_count != catalog->expected_sub_count)
        return -1;

    game = &catalog->games[index];
    if (game->apa_partition_count == 0u ||
        (game->allocation_bytes & 511u) != 0u)
        return -1;
    subs = game->apa_partition_count - 1u;
    sectors = game->allocation_bytes / 512u;
    if (catalog->hdl_sub_count < subs || catalog->expected_sub_count < subs ||
        catalog->used_sectors < sectors ||
        catalog->free_sectors > UINT64_MAX - sectors)
        return -1;

    if (index + 1u < catalog->count) {
        memmove(&catalog->games[index], &catalog->games[index + 1u],
                (catalog->count - index - 1u) * sizeof(*catalog->games));
    }
    catalog->count--;
    catalog->hdl_sub_count -= subs;
    catalog->expected_sub_count -= subs;
    catalog->used_sectors -= sectors;
    catalog->free_sectors += sectors;
    session_log_line(
        "HDL session catalogue updated after delete games=%u used=%llu free=%llu",
        catalog->count,
        (unsigned long long)catalog->used_sectors,
        (unsigned long long)catalog->free_sectors);
    return 0;
}

static HDL_GAME_UI_STAGE void installed_games_menu(hdl_catalog_t *catalog,
                                                   int *catalog_valid)
{
    int result = hdl_catalog_cache_load(catalog, catalog_valid);

    if (result < 0) {
        scr_clear();
        if (result == HDL_INSTALL_NO_GAMES)
            scr_printf("No installed HDL game partitions were found.\n");
        else
            scr_printf("HDL game catalogue scan failed (code %d).\n", result);
        app_ui_wait_to_return();
        return;
    }

    for (;;) {
        int choice = select_installed_game(catalog);

        if (choice < 0)
            break;
        if (game_action_menu(&catalog->games[choice], catalog->total_sectors)) {
            if (hdl_catalog_cache_remove_deleted(catalog,
                                                 (unsigned int)choice) < 0) {
                session_log_line(
                    "HDL session catalogue invalidated after delete; full scan deferred until next entry");
                hdl_catalog_cache_discard(catalog, catalog_valid);
                break;
            }
            if (catalog->count == 0u)
                break;
        }
    }
}

/* Root manager dispatch is intentionally kept separate from this complete
 * workspace. With whole-program LTO the three HDL subtrees were profitable to
 * inline by generic compiler heuristics, but doing so makes the always-hot root
 * manager compete with ISO/APA tooling for the R5900's 16 KiB I-cache. A human
 * menu transition is not a call-overhead benchmark. */
HDL_GAME_UI_STAGE void hdl_tools_menu(void)
{
    static const app_ui_menu_item_t items[] = {
        {"Install ISO from USB", "Choose a mass:/ ISO and create a verified HDL game", 1},
        {"Installed HDL games", "Fast raw-APA catalogue, details and guarded deletion", 1},
        {"Incomplete transaction", "Resume or remove a journal-owned allocation", 1}
    };
    hdl_catalog_t catalog;
    int catalog_valid = 0;
    unsigned int selected = 0;

    memset(&catalog, 0, sizeof(catalog));
    for (;;) {
        int choice = app_ui_menu_select(
            "HDL Tools", "Install and manage APA type 0x1337 game partitions",
            items, 3, &selected);

        if (choice < 0) {
            hdl_catalog_cache_discard(&catalog, &catalog_valid);
            return;
        }
        if (choice == 0) {
            open_new_install();
            hdl_catalog_cache_discard(&catalog, &catalog_valid);
        }
        if (choice == 1)
            installed_games_menu(&catalog, &catalog_valid);
        if (choice == 2) {
            open_incomplete_transaction();
            hdl_catalog_cache_discard(&catalog, &catalog_valid);
        }
    }
}

#undef HDL_GAME_UI_STAGE
