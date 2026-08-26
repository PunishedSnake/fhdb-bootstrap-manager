#define HDL_INSTALL_UI_STAGE __attribute__((noinline, optimize("Os")))

static void hdl_status_wait(const char *title, const char *body,
                            gs_ui_tone_t tone)
{
    /* Do not route HDL status/error pages through the compatibility console.
     * A stale/cleared console buffer can otherwise replace the real message
     * with a bare return prompt when wait_for_press() presents it. */
    gs_ui_console_discard();
    gs_ui_render_message(title, body, "X Return", tone);
    for (;;) {
        if (wait_for_press() & PAD_CROSS)
            break;
    }
    gs_ui_console_discard();
}

static int choose_media_type(uint32_t suggested)
{
    static const app_ui_menu_item_t items[] = {
        {"PS2 CD", "Store disc type 0x12", 1},
        {"PS2 DVD", "Store disc type 0x14", 1}
    };
    unsigned int selected = suggested == 0x14 ? 1u : 0u;
    int choice = app_ui_menu_select(
        "Ambiguous media type",
        "ISO9660 cannot distinguish a small DVD from a CD",
        items, 2, &selected);

    return choice < 0 ? -1 : (choice == 0 ? 0x12 : 0x14);
}

static HDL_INSTALL_UI_STAGE void show_result(const hdl_transaction_t *transaction,
                                             int result)
{
    char body[640];
    gs_ui_tone_t tone;

    if (result == 0) {
        snprintf(body, sizeof(body),
                 "Game: %s\nDisc: %s\nTarget: %s\n\n"
                 "Payload and metadata were read back successfully.",
                 transaction->game_title, transaction->startup,
                 transaction->target);
        tone = GS_UI_TONE_SUCCESS;
        hdl_status_wait("HDL installation complete", body, tone);
        return;
    }
    if (result == HDL_INSTALL_CANCELLED) {
        snprintf(body, sizeof(body),
                 "Progress: %llu / %llu ISO sectors\n\n"
                 "Use Resume incomplete install to continue.",
                 (unsigned long long)transaction->completed_sectors,
                 (unsigned long long)transaction->total_sectors);
        hdl_status_wait("HDL installation paused", body, GS_UI_TONE_WARNING);
        return;
    }

    snprintf(body, sizeof(body),
             "Operation stopped with code %d.\n\n"
             "No game metadata is exposed unless the journal reaches "
             "METADATA_COMMITTED. The journal is preserved for review or "
             "a guarded resume.",
             result);
    hdl_status_wait("HDL installation stopped", body, GS_UI_TONE_DANGER);
}

static HDL_INSTALL_UI_STAGE void begin_new_install(void)
{
    hdl_image_entry_t *images = NULL;
    hdl_file_source_t source;
    hdl_iso_source_t iso_source;
    hdl_iso_info_t info;
    hdl_partition_plan_t plan;
    hdl_transaction_t transaction;
    uint32_t maximum;
    unsigned int count;
    int maximum_value;
    int selection;
    int result;

    /* Phase 3 no longer calls the old pre-confirm combined APA scan. Keep its
     * wrapper referenced for this intermediate commit so -Werror proves the
     * behavioral change without forcing unrelated hdl_installer glue cleanup
     * into the journal-before-write safety fix. */
    (void)hdl_install_recheck_disk;

    result = scan_mass_images(&images, &count);
    if (result < 0) {
        char body[384];

        if (result == HDL_INSTALL_NO_IMAGES) {
            snprintf(body, sizeof(body),
                     "No .ISO files were found in the root of mass:/.\n\n"
                     "Only plain ISO files in the USB root are supported "
                     "right now. Subdirectories and split ISO sets are not "
                     "scanned yet.\n\nScan code: %d",
                     result);
        } else {
            snprintf(body, sizeof(body),
                     "The USB root could not be enumerated as mass:/.\n\n"
                     "Check that the USB device is mounted and readable, then "
                     "open Install ISO from USB again.\n\nScan code: %d",
                     result);
        }
        session_log_line("HDL ISO catalogue failed result=%d", result);
        hdl_status_wait("HDL ISO catalogue", body, GS_UI_TONE_WARNING);
        return;
    }
    selection = select_image(images, count);
    if (selection < 0) {
        free(images);
        return;
    }
    result = open_source(images[selection].path,
                         images[selection].bytes, &source);
    if (result < 0) {
        free(images);
        show_result(&(hdl_transaction_t){0}, result);
        return;
    }
    iso_source.read = source_read;
    iso_source.context = &source;
    iso_source.image_bytes = source.bytes;
    result = hdl_iso_probe(&iso_source, &info);
    if (result < 0) {
        char body[384];

        fileXioClose(source.fd);
        snprintf(body, sizeof(body),
                 "The selected file was found, but its PS2 ISO9660 / "
                 "SYSTEM.CNF identity could not be validated.\n\n"
                 "Probe code: %d",
                 result);
        session_log_line("HDL ISO probe failed path=%s result=%d",
                         images[selection].path, result);
        free(images);
        hdl_status_wait("Invalid PS2 ISO", body, GS_UI_TONE_DANGER);
        return;
    }
    if (info.requires_layer_break) {
        fileXioClose(source.fd);
        free(images);
        hdl_status_wait(
            "DVD9 installation blocked",
            "The image is valid, but its layer break has not been proven. "
            "DVD9 remains disabled rather than writing plausible-looking "
            "garbage to the HDD.",
            GS_UI_TONE_WARNING);
        return;
    }

    memset(&transaction, 0, sizeof(transaction));
    transaction.stage = HDL_TRANSACTION_STAGE_PLANNED;
    transaction.source_bytes = info.image_bytes;
    transaction.total_sectors = info.image_sectors;
    snprintf(transaction.source_path, sizeof(transaction.source_path), "%s",
             images[selection].path);
    snprintf(transaction.startup, sizeof(transaction.startup), "%s",
             info.startup);
    snprintf(transaction.game_title, sizeof(transaction.game_title), "%s",
             info.volume_title[0] != '\0' ? info.volume_title : info.disc_id);
    transaction.disc_type = info.disc_type;
    if (!info.media_type_confident) {
        result = choose_media_type(info.disc_type);
        if (result < 0) {
            fileXioClose(source.fd);
            free(images);
            return;
        }
        transaction.disc_type = (uint32_t)result;
    }
    result = hdl_partition_id(info.disc_id, transaction.game_title,
                              transaction.target);
    if (result == 0)
        result = source_fingerprint(&source,
                                    transaction.source_fingerprint);
    fileXioClose(source.fd);
    free(images);
    if (result < 0) {
        show_result(&transaction, result);
        return;
    }

    /* Pre-confirmation only needs enough live device state to describe the
     * planned partition layout. Exact free-space accounting in current ps2hdd
     * itself walks APA, so doing it here and again after arbitrary user think
     * time merely duplicates the expensive scan. execute_transaction() retains
     * the authoritative whole-chain/free-space recheck after confirmation. */
    result = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    if (result == 0) {
        maximum_value = fileXioDevctl("hdd0:", HDIOC_MAXSECTOR,
                                      NULL, 0, NULL, 0);
        if (maximum_value <= 0)
            result = HDL_INSTALL_DISK_UNSAFE;
        else
            maximum = (uint32_t)maximum_value;
    }
    if (result == 0)
        result = hdl_partition_plan(transaction.source_bytes, maximum, &plan);
    if (result == 0) {
        transaction.partition_count = plan.count;
        if (target_exists(transaction.target))
            result = HDL_INSTALL_TARGET_EXISTS;
    }
    if (result < 0) {
        show_result(&transaction, result);
        return;
    }

    {
        char body[640];

        snprintf(body, sizeof(body),
                 "Game       : %s\n"
                 "Startup    : %s\n"
                 "Source     : %llu MiB\n"
                 "Allocation : %llu MiB in %u partition%s\n"
                 "Target     : %s\n\n"
                 "No HDD write has happened yet. Live APA chain and exact "
                 "free space are revalidated immediately before allocation.",
                 transaction.game_title,
                 transaction.startup,
                 (unsigned long long)(transaction.source_bytes /
                                      1024u / 1024u),
                 (unsigned long long)(plan.allocation_bytes /
                                      1024u / 1024u),
                 plan.count, plan.count == 1 ? "" : "s",
                 transaction.target);
        gs_ui_console_discard();
        gs_ui_render_message(
            "Install PS2 ISO as an HDL game",
            body,
            "Hold L1+R1 and press X to install | TRIANGLE Cancel",
            GS_UI_TONE_WARNING);
    }
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_CROSS))
        return;

    /* Durable PLANNED must exist before execute_transaction() can reach the
     * first APA allocator write. The authoritative post-confirm disk/source
     * preflight still runs inside execute_transaction(), so a failed preflight
     * leaves only a harmless journal that the incomplete-transaction UI can
     * review or discard rather than an unjournaled allocation. */
    result = journal_save(&transaction);
    if (result == 0)
        result = execute_transaction(&transaction);
    show_result(&transaction, result);
}

static HDL_INSTALL_UI_STAGE void incomplete_menu(hdl_transaction_t *transaction)
{
    app_ui_menu_item_t items[] = {
        {"Resume incomplete install", "Recheck source, disk and exact target layout", 1},
        {"Remove incomplete allocation", "Only a journal-confirmed allocation; valid games are refused", 0},
        {"Keep it for later", "Return without changing journal or HDD", 1}
    };
    unsigned int selected = 0;
    char status[160];
    int choice;
    int result;

    items[1].enabled =
        transaction->stage >= HDL_TRANSACTION_STAGE_PARTITIONS_CREATED &&
        transaction->stage < HDL_TRANSACTION_STAGE_METADATA_COMMITTED;
    snprintf(status, sizeof(status), "%s | stage %u | %llu/%llu sectors",
             transaction->target, (unsigned int)transaction->stage,
             (unsigned long long)transaction->completed_sectors,
             (unsigned long long)transaction->total_sectors);
    choice = app_ui_menu_select("Incomplete HDL transaction", status,
                                items, 3, &selected);
    if (choice == 0) {
        result = execute_transaction(transaction);
        show_result(transaction, result);
    }
    if (choice == 1) {
        char body[384];

        snprintf(body, sizeof(body),
                 "Target: %s\n\n"
                 "A valid installed game will be refused.\n"
                 "Only the journal-confirmed incomplete allocation can be removed.",
                 transaction->target);
        gs_ui_console_discard();
        gs_ui_render_message(
            "Remove incomplete HDL allocation",
            body,
            "Hold L1+R1 and press SQUARE to remove | TRIANGLE Cancel",
            GS_UI_TONE_DANGER);
        if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE))
            return;
        result = remove_incomplete_target(transaction->target);
        if (result >= 0) {
            journal_remove();
            result = 0;
        }
        show_result(transaction, result);
    }
}

static HDL_INSTALL_UI_STAGE void show_corrupt_journal(void)
{
    hdl_status_wait(
        "Corrupt HDL transaction journal",
        "No HDD target can be trusted from this record, so the manager will "
        "not delete or modify any partition.\n\nRemove HDLINSTALL.TXN/NEW "
        "manually after inspection.",
        GS_UI_TONE_DANGER);
}

static HDL_INSTALL_UI_STAGE void open_new_install(void)
{
    hdl_transaction_t transaction;
    int result = journal_load(&transaction);

    if (result == 0 && transaction.stage != HDL_TRANSACTION_STAGE_COMPLETE) {
        incomplete_menu(&transaction);
        return;
    }
    if (path_exists(HDL_INSTALL_JOURNAL) ||
        path_exists(HDL_INSTALL_JOURNAL_NEW)) {
        show_corrupt_journal();
        return;
    }
    begin_new_install();
}

static HDL_INSTALL_UI_STAGE void open_incomplete_transaction(void)
{
    hdl_transaction_t transaction;
    int result = journal_load(&transaction);

    if (result == 0 && transaction.stage != HDL_TRANSACTION_STAGE_COMPLETE) {
        incomplete_menu(&transaction);
        return;
    }
    if (path_exists(HDL_INSTALL_JOURNAL) ||
        path_exists(HDL_INSTALL_JOURNAL_NEW)) {
        show_corrupt_journal();
        return;
    }
    hdl_status_wait("Incomplete HDL transaction",
                    "No incomplete HDL transaction was found. There is "
                    "currently nothing to resume or clean up.",
                    GS_UI_TONE_INFO);
}

#undef HDL_INSTALL_UI_STAGE
