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

static void show_result(const hdl_transaction_t *transaction, int result)
{
    scr_clear();
    if (result == 0) {
        scr_printf("HDL installation complete.\n\n");
        scr_printf("Game  : %s\n", transaction->game_title);
        scr_printf("Disc  : %s\n", transaction->startup);
        scr_printf("Target: %s\n\n", transaction->target);
        scr_printf("Payload and metadata were read back successfully.\n");
    } else if (result == HDL_INSTALL_CANCELLED) {
        scr_printf("HDL installation paused.\n\n");
        scr_printf("Progress: %llu / %llu ISO sectors\n",
                   (unsigned long long)transaction->completed_sectors,
                   (unsigned long long)transaction->total_sectors);
        scr_printf("Use Resume incomplete install to continue.\n");
    } else {
        scr_printf("HDL installation stopped (code %d).\n\n", result);
        scr_printf("No game metadata was exposed unless the journal reached\n");
        scr_printf("METADATA_COMMITTED. The journal was preserved for review\n");
        scr_printf("or a guarded resume.\n");
    }
    app_ui_wait_to_return();
}

static void begin_new_install(void)
{
    hdl_image_entry_t *images = NULL;
    hdl_file_source_t source;
    hdl_iso_source_t iso_source;
    hdl_iso_info_t info;
    hdl_partition_plan_t plan;
    hdl_transaction_t transaction;
    uint32_t maximum;
    uint32_t free_sectors;
    unsigned int count;
    int selection;
    int result;

    result = scan_mass_images(&images, &count);
    if (result < 0) {
        scr_clear();
        scr_printf("No usable .ISO files were found in mass:/.\n");
        scr_printf("Scan code: %d\n", result);
        app_ui_wait_to_return();
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
        fileXioClose(source.fd);
        free(images);
        show_result(&(hdl_transaction_t){0}, result);
        return;
    }
    if (info.requires_layer_break) {
        fileXioClose(source.fd);
        free(images);
        scr_clear();
        scr_printf("DVD9 installation is not enabled yet.\n\n");
        scr_printf("The image is valid, but its layer break has not been\n");
        scr_printf("proven. Guessing would create plausible-looking garbage.\n");
        app_ui_wait_to_return();
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
    result = recheck_disk(&maximum, &free_sectors);
    if (result == 0)
        result = hdl_partition_plan(transaction.source_bytes, maximum, &plan);
    if (result == 0 && plan.allocation_bytes / 512u > free_sectors)
        result = HDL_INSTALL_NO_SPACE;
    if (result == 0) {
        transaction.partition_count = plan.count;
        if (target_exists(transaction.target))
            result = HDL_INSTALL_TARGET_EXISTS;
    }
    if (result < 0) {
        show_result(&transaction, result);
        return;
    }

    scr_clear();
    scr_printf("Install PS2 ISO as an HDL game\n\n");
    scr_printf("Game       : %s\n", transaction.game_title);
    scr_printf("Startup    : %s\n", transaction.startup);
    scr_printf("Source     : %llu MiB\n",
               (unsigned long long)(transaction.source_bytes / 1024u / 1024u));
    scr_printf("Allocation : %llu MiB in %u partition%s\n",
               (unsigned long long)(plan.allocation_bytes / 1024u / 1024u),
               plan.count, plan.count == 1 ? "" : "s");
    scr_printf("Target     : %s\n\n", transaction.target);
    scr_printf("Hold L1+R1 and press X to allocate and install.\n");
    scr_printf("TRIANGLE cancels without changing the HDD.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_CROSS))
        return;
    result = journal_save(&transaction);
    if (result == 0)
        result = execute_transaction(&transaction);
    show_result(&transaction, result);
}

static void incomplete_menu(hdl_transaction_t *transaction)
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
        scr_clear();
        scr_printf("Remove incomplete HDL allocation\n\n");
        scr_printf("Target: %s\n\n", transaction->target);
        scr_printf("A valid installed game will be refused.\n");
        scr_printf("Hold L1+R1 and press SQUARE to remove.\n");
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

static void show_corrupt_journal(void)
{
    scr_clear();
    scr_printf("The HDL transaction journal is corrupt.\n\n");
    scr_printf("No HDD target can be trusted from this record, so the\n");
    scr_printf("manager will not delete or modify any partition.\n\n");
    scr_printf("Remove HDLINSTALL.TXN/NEW manually after inspection.\n");
    app_ui_wait_to_return();
}

static void open_new_install(void)
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

static void open_incomplete_transaction(void)
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
    scr_clear();
    scr_printf("No incomplete HDL transaction was found.\n\n");
    scr_printf("There is currently nothing to resume or clean up.\n");
    app_ui_wait_to_return();
}
