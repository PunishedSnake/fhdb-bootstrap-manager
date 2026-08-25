static int valid_apa_node(const unsigned char header[APA_HEADER_SIZE],
                          uint32_t lba, uint32_t total_sectors)
{
    uint32_t length = read_le32(header + APA_LENGTH_OFFSET);
    uint64_t end = (uint64_t)lba + length;

    if (memcmp(header + APA_MAGIC_OFFSET, "APA\0", 4) != 0 ||
        read_le32(header) != apa_checksum(header) ||
        read_le32(header + APA_START_OFFSET) != lba || length == 0 ||
        end > total_sectors || read_le32(header + APA_NSUB_OFFSET) > APA_MAX_SUBS)
        return 0;
    return 1;
}

static uint64_t apa_main_allocation(const unsigned char header[APA_HEADER_SIZE])
{
    uint64_t sectors = read_le32(header + APA_LENGTH_OFFSET);
    unsigned int nsub = read_le32(header + APA_NSUB_OFFSET);
    unsigned int i;

    for (i = 0; i < nsub; i++)
        sectors += read_le32(header + APA_SUBS_OFFSET +
                             i * APA_SUB_ENTRY_SIZE + 4u);
    return sectors * 512u;
}

static int apa_id_matches(const unsigned char header[APA_HEADER_SIZE],
                          const char *target)
{
    char id[HDL_PARTITION_ID_MAX];

    if (target == NULL || target[0] == '\0')
        return 0;
    memset(id, 0, sizeof(id));
    memcpy(id, header + APA_ID_OFFSET,
           APA_ID_SIZE < sizeof(id) - 1u ? APA_ID_SIZE : sizeof(id) - 1u);
    return strcmp(id, target) == 0;
}

static int catalog_grow(hdl_catalog_t *catalog)
{
    unsigned int next = catalog->capacity == 0 ? 64u : catalog->capacity * 2u;
    hdl_game_entry_t *grown;

    if (next < catalog->capacity)
        return HDL_INSTALL_MEMORY_FAILED;
    grown = realloc(catalog->games, next * sizeof(*grown));
    if (grown == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    catalog->games = grown;
    memset(catalog->games + catalog->capacity, 0,
           (next - catalog->capacity) * sizeof(*grown));
    catalog->capacity = next;
    return 0;
}

static int scan_apa_catalog_for_target(hdl_catalog_t *catalog,
                                       int collect_games,
                                       const char *target,
                                       int *target_found)
{
    unsigned char header[APA_HEADER_SIZE] __attribute__((aligned(64)));
    uint32_t total_sectors;
    uint32_t current;
    uint32_t previous = 0;
    unsigned int nodes = 0;
    int total_value;
    int result;

    memset(catalog, 0, sizeof(*catalog));
    if (target_found != NULL)
        *target_found = 0;
    result = hdd_read_raw_sectors(0, 2, admission_header);
    if (result < 0 || !is_standard_apa_header(admission_header) ||
        is_hybrid_gpt(admission_header))
        return HDL_INSTALL_DISK_UNSAFE;
    /* HDIOC_TOTALSECTOR is transported through a signed int by fileXio.
     * The bit pattern is still the driver's u32 sector count, so do not
     * reject large disks merely because bit 31 is set. */
    total_value = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR,
                                 NULL, 0, NULL, 0);
    if (total_value < 0 && total_value > -4096)
        return HDL_INSTALL_DISK_UNSAFE;
    total_sectors = (uint32_t)total_value;
    if (total_sectors < 2u)
        return HDL_INSTALL_DISK_TOO_LARGE;
    catalog->total_sectors = total_sectors;
    catalog->used_sectors = read_le32(admission_header + APA_LENGTH_OFFSET);
    current = read_le32(admission_header + APA_NEXT_OFFSET);

    while (current != 0) {
        uint16_t type;
        uint16_t flags;
        uint32_t next;
        uint32_t length;

        if (++nodes > HDL_CHAIN_NODE_LIMIT || current >= total_sectors ||
            total_sectors - current < 2u)
            return HDL_INSTALL_CHAIN_INVALID;
        result = hdd_read_raw_sectors(current, 2, header);
        if (result < 0 || !valid_apa_node(header, current, total_sectors) ||
            read_le32(header + APA_PREV_OFFSET) != previous)
            return HDL_INSTALL_CHAIN_INVALID;
        length = read_le32(header + APA_LENGTH_OFFSET);
        type = read_le16(header + APA_TYPE_OFFSET);
        if (type == 0) {
            if (catalog->free_sectors > UINT64_MAX - length)
                return HDL_INSTALL_CHAIN_INVALID;
            catalog->free_sectors += length;
        } else {
            if (catalog->used_sectors > UINT64_MAX - length)
                return HDL_INSTALL_CHAIN_INVALID;
            catalog->used_sectors += length;
            if (target_found != NULL && apa_id_matches(header, target))
                *target_found = 1;
        }
        flags = read_le16(header + APA_FLAGS_OFFSET);
        if (type == HDL_APA_TYPE) {
            if (flags & HDL_APA_FLAG_SUB) {
                catalog->hdl_sub_count++;
            } else if (collect_games) {
                hdl_game_entry_t *game;
                unsigned int nsub = read_le32(header + APA_NSUB_OFFSET);

                if (catalog->count == catalog->capacity) {
                    result = catalog_grow(catalog);
                    if (result < 0)
                        return result;
                }
                game = &catalog->games[catalog->count++];
                memset(game, 0, sizeof(*game));
                memcpy(game->id, header + APA_ID_OFFSET, APA_ID_SIZE);
                game->id[HDL_PARTITION_ID_MAX - 1u] = '\0';
                game->main_lba = current;
                game->allocation_bytes = apa_main_allocation(header);
                game->apa_partition_count = nsub + 1u;
                game->metadata_state = HDL_METADATA_NOT_LOADED;
                catalog->expected_sub_count += nsub;
            }
        }
        next = read_le32(header + APA_NEXT_OFFSET);
        if (next == current || (next != 0 && next >= total_sectors))
            return HDL_INSTALL_CHAIN_INVALID;
        previous = current;
        current = next;
    }
    if (catalog->used_sectors > total_sectors ||
        catalog->free_sectors > total_sectors ||
        catalog->used_sectors + catalog->free_sectors > total_sectors)
        return HDL_INSTALL_CHAIN_INVALID;
    return collect_games && catalog->count == 0 ? HDL_INSTALL_NO_GAMES : 0;
}

static int scan_apa_catalog(hdl_catalog_t *catalog, int collect_games)
{
    return scan_apa_catalog_for_target(catalog, collect_games, NULL, NULL);
}

static void catalog_free(hdl_catalog_t *catalog)
{
    free(catalog->games);
    memset(catalog, 0, sizeof(*catalog));
}

static int compare_games(const void *left_value, const void *right_value)
{
    const hdl_game_entry_t *left = left_value;
    const hdl_game_entry_t *right = right_value;

    return strcmp(left->id, right->id);
}

static int load_game_metadata(hdl_game_entry_t *game, uint32_t total_sectors)
{
    unsigned char metadata[HDL_METADATA_SIZE] __attribute__((aligned(64)));
    uint32_t lba;
    int result;

    if (game->metadata_state != HDL_METADATA_NOT_LOADED)
        return game->metadata_state;
    if (game->main_lba > UINT32_MAX - HDL_METADATA_LBA_OFFSET)
        return game->metadata_state = HDL_INSTALL_CHAIN_INVALID;
    lba = game->main_lba + HDL_METADATA_LBA_OFFSET;
    if (lba >= total_sectors || total_sectors - lba < 2u)
        return game->metadata_state = HDL_INSTALL_CHAIN_INVALID;
    result = hdd_read_raw_sectors(lba, 2, metadata);
    if (result == 0) {
        sha256_buffer(metadata, sizeof(metadata), game->metadata_sha256);
        result = hdl_metadata_parse(metadata, &game->metadata);
    }
    game->metadata_state = result;
    return result;
}

static void load_page_metadata(hdl_catalog_t *catalog, unsigned int selected)
{
    unsigned int page = selected / HDL_BROWSER_PAGE_SIZE;
    unsigned int start = page * HDL_BROWSER_PAGE_SIZE;
    unsigned int shown = catalog->count - start;
    unsigned int i;

    if (shown > HDL_BROWSER_PAGE_SIZE)
        shown = HDL_BROWSER_PAGE_SIZE;
    for (i = 0; i < shown; i++)
        (void)load_game_metadata(&catalog->games[start + i],
                                 catalog->total_sectors);
}

static int select_installed_game(hdl_catalog_t *catalog)
{
    unsigned int selected = 0;

    for (;;) {
        const char *labels[HDL_BROWSER_PAGE_SIZE];
        const char *hints[HDL_BROWSER_PAGE_SIZE];
        unsigned char enabled[HDL_BROWSER_PAGE_SIZE];
        unsigned int page;
        unsigned int start;
        unsigned int shown;
        unsigned int local;
        unsigned int i;
        char hint_text[HDL_BROWSER_PAGE_SIZE][80];
        char status[192];
        hdl_game_entry_t *game;
        u32 pressed;

        load_page_metadata(catalog, selected);
        page = selected / HDL_BROWSER_PAGE_SIZE;
        start = page * HDL_BROWSER_PAGE_SIZE;
        shown = catalog->count - start;
        if (shown > HDL_BROWSER_PAGE_SIZE)
            shown = HDL_BROWSER_PAGE_SIZE;
        local = selected - start;
        for (i = 0; i < shown; i++) {
            game = &catalog->games[start + i];
            labels[i] = game->metadata_state == 0 ?
                        game->metadata.game_title : game->id;
            if (game->metadata_state == 0) {
                snprintf(hint_text[i], sizeof(hint_text[i]),
                         "%s | %llu MiB | %u parts",
                         game->metadata.startup,
                         (unsigned long long)(game->allocation_bytes /
                                              1024u / 1024u),
                         game->apa_partition_count);
            } else {
                snprintf(hint_text[i], sizeof(hint_text[i]),
                         "metadata unavailable (%d)", game->metadata_state);
            }
            hints[i] = hint_text[i];
            enabled[i] = 1u;
        }
        game = &catalog->games[selected];
        if (game->metadata_state == 0) {
            snprintf(status, sizeof(status),
                     "%u games%s | page %u/%u | %s | %llu MiB | LEFT/RIGHT page",
                     catalog->count,
                     catalog->hdl_sub_count != catalog->expected_sub_count ?
                         " | sub mismatch" : "",
                     page + 1u, page_count(catalog->count),
                     game->metadata.startup,
                     (unsigned long long)(game->allocation_bytes /
                                          1024u / 1024u));
        } else {
            snprintf(status, sizeof(status),
                     "%u games%s | page %u/%u | metadata %d | LEFT/RIGHT page",
                     catalog->count,
                     catalog->hdl_sub_count != catalog->expected_sub_count ?
                         " | sub mismatch" : "",
                     page + 1u, page_count(catalog->count),
                     game->metadata_state);
        }
        gs_ui_render_menu("Installed HDL games", status, labels, hints,
                          enabled, shown, local);
        pressed = wait_for_press();
        if (pressed & PAD_UP) {
            if (local == 0)
                selected = start + shown - 1u;
            else
                selected--;
        }
        if (pressed & PAD_DOWN) {
            if (local + 1u >= shown)
                selected = start;
            else
                selected++;
        }
        if (pressed & PAD_LEFT)
            selected = page_move_selection(selected, catalog->count, -1);
        if (pressed & PAD_RIGHT)
            selected = page_move_selection(selected, catalog->count, 1);
        if (pressed & PAD_CROSS)
            return (int)selected;
        if (pressed & PAD_TRIANGLE)
            return -1;
    }
}

static int recheck_disk_target(const char *target, int *target_found,
                               uint32_t *max_partition_sectors,
                               uint32_t *free_sectors)
{
    hdl_catalog_t catalog;
    uint64_t free64;
    int found = 0;
    int status = fileXioDevctl("hdd0:", HDIOC_STATUS,
                               NULL, 0, NULL, 0);
    int maximum;
    int result;

    if (target_found != NULL)
        *target_found = 0;
    if (status != 0)
        return HDL_INSTALL_DISK_UNSAFE;
    result = scan_apa_catalog_for_target(&catalog, 0, target,
                                         target_found != NULL ? &found : NULL);
    if (result < 0) {
        catalog_free(&catalog);
        return result;
    }
    maximum = fileXioDevctl("hdd0:", HDIOC_MAXSECTOR,
                            NULL, 0, NULL, 0);
    if (maximum <= 0) {
        catalog_free(&catalog);
        return HDL_INSTALL_DISK_UNSAFE;
    }
    free64 = catalog.free_sectors;
    *max_partition_sectors = (uint32_t)maximum;
    *free_sectors = free64 > UINT32_MAX ? UINT32_MAX : (uint32_t)free64;
    if (target_found != NULL)
        *target_found = found;
    catalog_free(&catalog);
    return 0;
}

static int recheck_disk(uint32_t *max_partition_sectors,
                        uint32_t *free_sectors)
{
    return recheck_disk_target(NULL, NULL,
                               max_partition_sectors, free_sectors);
}
