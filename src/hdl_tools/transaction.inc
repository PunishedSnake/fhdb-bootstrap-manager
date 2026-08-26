static const char *allocation_name(uint64_t bytes)
{
    static const char *const names[] = {"128M", "256M", "512M",
                                        "1G", "2G", "4G"};
    static const uint64_t sizes[] = {
        128ull << 20, 256ull << 20, 512ull << 20,
        1024ull << 20, 2048ull << 20, 4096ull << 20
    };
    unsigned int i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (bytes == sizes[i])
            return names[i];
    }
    return NULL;
}

static int journal_blocks_game_delete(const char *target)
{
    hdl_transaction_t transaction;
    int result;

    if (!path_exists(HDL_INSTALL_JOURNAL) &&
        !path_exists(HDL_INSTALL_JOURNAL_NEW))
        return 0;
    result = journal_load(&transaction);
    if (result < 0)
        return HDL_INSTALL_JOURNAL_INVALID;
    if (transaction.stage != HDL_TRANSACTION_STAGE_COMPLETE &&
        strcmp(transaction.target, target) == 0)
        return HDL_INSTALL_TARGET_BUSY;
    return 0;
}

static int verify_game_snapshot(const hdl_game_entry_t *game,
                                uint32_t total_sectors)
{
    unsigned char metadata[HDL_METADATA_SIZE] __attribute__((aligned(64)));
    unsigned char digest[32];
    hdl_metadata_info_t parsed;
    iox_stat_t stat;
    char path[64];
    uint32_t lba;
    int result;

    if (game->metadata_state != 0 ||
        target_path("hdd0:", game->id, path, sizeof(path)) < 0)
        return HDL_INSTALL_TARGET_CHANGED;
    result = fileXioGetStat(path, &stat);
    if (result < 0 || stat.mode != HDL_APA_TYPE ||
        (stat.attr & HDL_APA_FLAG_SUB))
        return HDL_INSTALL_TARGET_CHANGED;
    if (game->main_lba > UINT32_MAX - HDL_METADATA_LBA_OFFSET)
        return HDL_INSTALL_TARGET_CHANGED;
    lba = game->main_lba + HDL_METADATA_LBA_OFFSET;
    if (lba >= total_sectors || total_sectors - lba < 2u ||
        hdd_read_raw_sectors(lba, 2, metadata) < 0 ||
        hdl_metadata_parse(metadata, &parsed) < 0 ||
        parsed.partition_count != game->apa_partition_count)
        return HDL_INSTALL_TARGET_CHANGED;
    sha256_buffer(metadata, sizeof(metadata), digest);
    if (memcmp(digest, game->metadata_sha256, sizeof(digest)) != 0)
        return HDL_INSTALL_TARGET_CHANGED;
    return 0;
}

static int delete_installed_game(const hdl_game_entry_t *game,
                                 uint32_t total_sectors)
{
    uint32_t maximum;
    uint32_t free_sectors;
    char path[64];
    int result;

    /* Before the user arms deletion these are genuinely read-only safety
     * checks, so keep the ordinary READ monitor semantics here. */
    result = recheck_disk(&maximum, &free_sectors);
    if (result == 0)
        result = journal_blocks_game_delete(game->id);
    if (result == 0)
        result = verify_game_snapshot(game, total_sectors);
    if (result < 0)
        return result;
    (void)maximum;
    (void)free_sectors;

    scr_clear();
    scr_printf("Delete installed HDL game\n\n");
    scr_printf("Game   : %s\n", game->metadata.game_title);
    scr_printf("Startup: %s\n", game->metadata.startup);
    scr_printf("Target : %s\n", game->id);
    scr_printf("Size   : %llu MiB allocated\n\n",
               (unsigned long long)(game->allocation_bytes / 1024u / 1024u));
    scr_printf("This removes the main partition and all of its subs.\n");
    scr_printf("There is no undo operation.\n\n");
    scr_printf("Hold L1+R1 and press SQUARE to delete.\n");
    scr_printf("TRIANGLE returns without changing the HDD.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE))
        return HDL_INSTALL_CANCELLED;

    /* Once destructive confirmation has been accepted, validation reads are
     * no longer an independent read-only operation: they are the preflight of
     * an armed write path. Keep ACTION truthful, but never display the green
     * read-only footer during this window. */
    disk_status_begin_at("HDL game removal",
                         "Write path armed; revalidating selected target",
                         "Selected HDL main/sub partition chain");
    disk_status_set_write_intent(1);
    result = recheck_disk(&maximum, &free_sectors);
    if (result == 0)
        result = journal_blocks_game_delete(game->id);
    if (result == 0)
        result = verify_game_snapshot(game, total_sectors);
    if (result < 0) {
        disk_status_end();
        return result;
    }
    if (target_path("hdd0:", game->id, path, sizeof(path)) < 0) {
        disk_status_end();
        return HDL_INSTALL_TARGET_CHANGED;
    }

    disk_status_phase_at("Removing selected APA allocation",
                         "APA metadata and linked HDL sub-partitions");
    disk_status_io(DISK_STATUS_WRITE, game->main_lba, 2u, 0, 0);
    result = fileXioRemove(path);
    disk_status_end();
    session_log_line("HDL game delete target=%s startup=%s result=%d",
                     game->id, game->metadata.startup, result);
    return result < 0 ? result : 0;
}

static int create_partitions(const hdl_transaction_t *transaction,
                             const hdl_partition_plan_t *plan)
{
    char existing[64];
    char create[96];
    int fd;
    unsigned int i;

    target_path("hdd0:", transaction->target, existing, sizeof(existing));
    fd = fileXioOpen(existing, FIO_O_RDONLY, 0);
    if (fd >= 0) {
        fileXioClose(fd);
        return HDL_INSTALL_TARGET_EXISTS;
    }
    disk_status_phase_at("Creating HDL main partition",
                         "APA allocation metadata");
    disk_status_io(DISK_STATUS_WRITE, 0, 0, 0, 0);
    snprintf(create, sizeof(create), "hdd0:%s,,,%s,HDL",
             transaction->target,
             allocation_name(plan->slices[0].allocation_bytes));
    fd = fileXioOpen(create, FIO_O_RDWR | FIO_O_CREAT, 0666);
    if (fd < 0)
        return fd;
    for (i = 1; i < plan->count; i++) {
        const char *size = allocation_name(plan->slices[i].allocation_bytes);
        int result;

        disk_status_phase_at("Adding HDL sub-partition",
                             "APA allocation metadata");
        disk_status_io(DISK_STATUS_WRITE, 0, 0, i, plan->count);
        result = fileXioIoctl2(fd, HIOCADDSUB, (void *)size,
                               strlen(size) + 1u, NULL, 0);

        if (result < 0) {
            fileXioClose(fd);
            (void)remove_incomplete_target(transaction->target);
            return HDL_INSTALL_CREATE_FAILED;
        }
    }
    disk_status_phase_at("Flushing APA allocation metadata",
                         "New HDL main/sub partition chain");
    disk_status_io(DISK_STATUS_FLUSH, 0, 0, plan->count, plan->count);
    if (fileXioIoctl2(fd, HIOCFLUSH, NULL, 0, NULL, 0) < 0) {
        fileXioClose(fd);
        (void)remove_incomplete_target(transaction->target);
        return HDL_INSTALL_CREATE_FAILED;
    }
    fileXioClose(fd);
    return 0;
}

static int open_target(const hdl_transaction_t *transaction,
                       const hdl_partition_plan_t *plan,
                       hdl_stream_layout_t *layout)
{
    char path[64];
    int fd;
    unsigned int i;

    if (target_path("hdl0:", transaction->target, path, sizeof(path)) < 0)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    fd = fileXioOpen(path, FIO_O_RDWR, 0);
    if (fd < 0)
        return fd;
    if (fileXioIoctl2(fd, HDL_STREAM_IOCTL2_GET_LAYOUT,
                      NULL, 0, layout, sizeof(*layout)) < 0 ||
        layout->count != plan->count) {
        fileXioClose(fd);
        return HDL_INSTALL_LAYOUT_MISMATCH;
    }
    for (i = 0; i < plan->count; i++) {
        if ((uint64_t)layout->lengths[i] * 512u !=
                plan->slices[i].allocation_bytes || layout->starts[i] == 0) {
            fileXioClose(fd);
            return HDL_INSTALL_LAYOUT_MISMATCH;
        }
    }
    return fd;
}

static int read_exact_fd(int fd, unsigned char *buffer, unsigned int bytes)
{
    unsigned int complete = 0;

    while (complete < bytes) {
        int result = fileXioRead(fd, buffer + complete, bytes - complete);

        if (result <= 0)
            return result < 0 ? result : HDL_INSTALL_COPY_FAILED;
        complete += (unsigned int)result;
    }
    return 0;
}

static int write_exact_fd(int fd, const unsigned char *buffer,
                          unsigned int bytes)
{
    unsigned int complete = 0;

    while (complete < bytes) {
        int result = fileXioWrite(fd, buffer + complete, bytes - complete);

        if (result <= 0)
            return result < 0 ? result : HDL_INSTALL_COPY_FAILED;
        complete += (unsigned int)result;
    }
    return 0;
}

static uint32_t physical_lba(const hdl_partition_plan_t *plan,
                             const hdl_stream_layout_t *layout,
                             uint64_t payload_offset)
{
    unsigned int i;

    for (i = 0; i < plan->count; i++) {
        uint64_t begin = plan->slices[i].payload_offset;
        uint64_t end = begin + plan->slices[i].payload_bytes;

        if (payload_offset >= begin && payload_offset < end)
            return layout->starts[i] + (i == 0 ? 0x2000u : 0x0800u) +
                   (uint32_t)((payload_offset - begin) / 512u);
    }
    return 0;
}

/*
 * Hash a source ISO without involving the HDD. This is primarily used when a
 * previously PAYLOAD_VERIFIED journal is resumed: the old journal format does
 * not persist a full payload SHA-256, so one source pass is still required to
 * reconstruct the expected digest. Fresh installs do not pay this second USB
 * pass because copy_payload() hashes source bytes while they are already being
 * transferred.
 */
static int hash_source_payload(const hdl_transaction_t *transaction,
                               int source_fd,
                               unsigned char digest[32])
{
    unsigned char *buffer;
    sha256_context_t hash;
    uint64_t offset = 0;
    int result = 0;

    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    if (fileXioLseek64(source_fd, 0, FIO_SEEK_SET) != 0) {
        result = HDL_INSTALL_VERIFY_FAILED;
        goto done;
    }
    sha256_init(&hash);
    while (offset < transaction->source_bytes) {
        uint64_t remaining = transaction->source_bytes - offset;
        unsigned int bytes = remaining > HDL_INSTALL_IO_BYTES ?
                             HDL_INSTALL_IO_BYTES : (unsigned int)remaining;

        if (read_exact_fd(source_fd, buffer, bytes) < 0) {
            result = HDL_INSTALL_VERIFY_FAILED;
            goto done;
        }
        sha256_update(&hash, buffer, bytes);
        offset += bytes;
        disk_status_io(DISK_STATUS_VERIFY, 0, 0,
                       (unsigned int)(offset / 2048u),
                       (unsigned int)transaction->total_sectors);
    }
    sha256_final(&hash, digest);
done:
    free(buffer);
    return result;
}

static int copy_payload(hdl_transaction_t *transaction,
                        const hdl_partition_plan_t *plan,
                        const hdl_stream_layout_t *layout,
                        int source_fd, int target_fd,
                        unsigned char source_digest[32])
{
    unsigned char *buffer;
    sha256_context_t source_hash;
    uint64_t offset = transaction->completed_sectors * 2048u;
    uint64_t hash_offset = 0;
    uint64_t next_journal = transaction->completed_sectors +
                            HDL_INSTALL_JOURNAL_INTERVAL_SECTORS;
    int result = 0;

    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    sha256_init(&source_hash);

    /* Resume needs the SHA state for the already-copied prefix. Re-read only
     * that prefix from USB, then continue hashing naturally while copying the
     * remaining bytes. A fresh install starts at zero and skips this pass. */
    if (offset > 0) {
        disk_status_phase_at("Rebuilding source digest for resumed copy",
                             "Already-copied prefix on mass:");
        if (fileXioLseek64(source_fd, 0, FIO_SEEK_SET) != 0) {
            result = HDL_INSTALL_COPY_FAILED;
            goto done;
        }
        while (hash_offset < offset) {
            uint64_t remaining = offset - hash_offset;
            unsigned int bytes = remaining > HDL_INSTALL_IO_BYTES ?
                                 HDL_INSTALL_IO_BYTES : (unsigned int)remaining;

            result = read_exact_fd(source_fd, buffer, bytes);
            if (result < 0)
                goto done;
            sha256_update(&source_hash, buffer, bytes);
            hash_offset += bytes;
            disk_status_io(DISK_STATUS_VERIFY, 0, 0,
                           (unsigned int)(hash_offset / 2048u),
                           (unsigned int)transaction->total_sectors);
        }
    }

    disk_status_phase_at("Copying ISO payload from mass:",
                         "HDL payload data area");
    if (fileXioLseek64(source_fd, (s64)offset, FIO_SEEK_SET) != (s64)offset ||
        fileXioLseek64(target_fd, (s64)offset, FIO_SEEK_SET) != (s64)offset) {
        result = HDL_INSTALL_COPY_FAILED;
        goto done;
    }
    while (offset < transaction->source_bytes) {
        uint64_t remaining = transaction->source_bytes - offset;
        unsigned int bytes = remaining > HDL_INSTALL_IO_BYTES ?
                             HDL_INSTALL_IO_BYTES : (unsigned int)remaining;
        u32 pressed = 0;

        result = read_exact_fd(source_fd, buffer, bytes);
        if (result < 0)
            goto done;
        sha256_update(&source_hash, buffer, bytes);
        result = write_exact_fd(target_fd, buffer, bytes);
        if (result < 0)
            goto done;
        offset += bytes;
        disk_status_io(DISK_STATUS_WRITE,
                       physical_lba(plan, layout, offset - bytes), bytes / 512u,
                       (unsigned int)(offset / 2048u),
                       (unsigned int)transaction->total_sectors);
        transaction->completed_sectors = offset / 2048u;
        if (transaction->completed_sectors >= next_journal ||
            offset == transaction->source_bytes) {
            result = journal_save(transaction);
            if (result < 0)
                goto done;
            next_journal = transaction->completed_sectors +
                           HDL_INSTALL_JOURNAL_INTERVAL_SECTORS;
        }
        if (poll_for_press(&pressed) && (pressed & PAD_TRIANGLE)) {
            result = journal_save(transaction);
            if (result == 0)
                result = HDL_INSTALL_CANCELLED;
            goto done;
        }
    }
    sha256_final(&source_hash, source_digest);
done:
    free(buffer);
    return result;
}

/*
 * Verify the installed payload from HDD only. The source digest is either
 * accumulated for free while fresh bytes are copied, or reconstructed by one
 * source-only pass when resuming a PAYLOAD_VERIFIED journal. This removes the
 * old second full USB read from the normal install path while preserving full
 * end-to-end SHA-256 verification of every payload byte.
 */
static int verify_target_digest(const hdl_transaction_t *transaction,
                                const hdl_partition_plan_t *plan,
                                const hdl_stream_layout_t *layout,
                                int target_fd,
                                const unsigned char expected_digest[32])
{
    unsigned char *target_buffer;
    unsigned char target_digest[32];
    sha256_context_t target_hash;
    uint64_t offset = 0;
    int result = 0;

    target_buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (target_buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    if (fileXioLseek64(target_fd, 0, FIO_SEEK_SET) != 0) {
        result = HDL_INSTALL_VERIFY_FAILED;
        goto done;
    }
    sha256_init(&target_hash);
    while (offset < transaction->source_bytes) {
        uint64_t remaining = transaction->source_bytes - offset;
        unsigned int bytes = remaining > HDL_INSTALL_IO_BYTES ?
                             HDL_INSTALL_IO_BYTES : (unsigned int)remaining;

        if (read_exact_fd(target_fd, target_buffer, bytes) < 0) {
            result = HDL_INSTALL_VERIFY_FAILED;
            goto done;
        }
        sha256_update(&target_hash, target_buffer, bytes);
        disk_status_io(DISK_STATUS_VERIFY,
                       physical_lba(plan, layout, offset), bytes / 512u,
                       (unsigned int)(offset / 2048u),
                       (unsigned int)transaction->total_sectors);
        offset += bytes;
    }
    sha256_final(&target_hash, target_digest);
    if (memcmp(expected_digest, target_digest, sizeof(target_digest)) != 0)
        result = HDL_INSTALL_VERIFY_FAILED;
done:
    free(target_buffer);
    return result;
}

static int execute_transaction(hdl_transaction_t *transaction)
{
    hdl_partition_plan_t plan;
    hdl_stream_layout_t layout;
    hdl_metadata_options_t metadata_options;
    unsigned char metadata[HDL_METADATA_SIZE];
    hdl_file_source_t source;
    unsigned char fingerprint[32];
    unsigned char source_payload_digest[32];
    uint32_t maximum;
    uint32_t free_sectors;
    int target_fd = -1;
    int verified_this_run = 0;
    int source_digest_valid = 0;
    int metadata_valid = 0;
    int result;

    source.fd = -1;
    source.bytes = transaction->source_bytes;
    result = recheck_disk(&maximum, &free_sectors);
    if (result < 0)
        return result;
    result = hdl_partition_plan(transaction->source_bytes, maximum, &plan);
    if (result < 0)
        return result;
    if (transaction->partition_count != plan.count)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    if (plan.allocation_bytes / 512u > free_sectors &&
        transaction->stage == HDL_TRANSACTION_STAGE_PLANNED)
        return HDL_INSTALL_NO_SPACE;

    /* Once METADATA_COMMITTED is durable, payload and source identity have
     * already passed full verification. Completion recovery only needs the
     * target layout plus metadata read-back, so do not require the USB ISO to
     * remain connected merely to move stage 5 -> COMPLETE. */
    if (transaction->stage < HDL_TRANSACTION_STAGE_METADATA_COMMITTED) {
        result = open_source(transaction->source_path,
                             transaction->source_bytes, &source);
        if (result < 0)
            return result;
        result = source_fingerprint(&source, fingerprint);
        if (result == 0 && memcmp(fingerprint, transaction->source_fingerprint,
                                  sizeof(fingerprint)) != 0)
            result = HDL_INSTALL_SOURCE_CHANGED;
        if (result == 0)
            result = source_identity_matches(&source, transaction);
        if (result < 0) {
            fileXioClose(source.fd);
            return result;
        }
    }

    pad_activity_begin();
    disk_status_begin_at("HDL game installation",
                         "Preparing guarded APA allocation",
                         "Standard HDL main/sub partitions");
    /* Reaching execute_transaction means the user has already authorized an
     * install or resume. Validation may still read first, but the transaction
     * owns an armed write path until it exits. */
    disk_status_set_write_intent(1);
    if (transaction->stage == HDL_TRANSACTION_STAGE_PLANNED) {
        result = create_partitions(transaction, &plan);
        if (result < 0)
            goto done;
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    target_fd = open_target(transaction, &plan, &layout);
    if (target_fd < 0) {
        result = target_fd;
        goto done;
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_PARTITIONS_CREATED) {
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_COPYING, 0);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING) {
        result = copy_payload(transaction, &plan, &layout,
                              source.fd, target_fd, source_payload_digest);
        if (result < 0)
            goto done;
        source_digest_valid = 1;
        disk_status_phase_at("Flushing copied HDL payload",
                             "HDL payload data area");
        disk_status_io(DISK_STATUS_FLUSH,
                       layout.starts[0], 0, transaction->total_sectors,
                       transaction->total_sectors);
        if (fileXioIoctl2(target_fd, HDL_STREAM_IOCTL2_FLUSH,
                          NULL, 0, NULL, 0) < 0) {
            result = HDL_INSTALL_COPY_FAILED;
            goto done;
        }
        disk_status_phase_at("Verifying installed payload on HDD",
                             "HDD-only SHA-256 read-back against source digest");
        result = verify_target_digest(transaction, &plan, &layout,
                                      target_fd, source_payload_digest);
        if (result < 0)
            goto done;
        verified_this_run = 1;
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED,
            transaction->total_sectors);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
        if (!verified_this_run) {
            disk_status_phase_at("Hashing source for resumed verification",
                                 "One source pass required by legacy journal format");
            result = hash_source_payload(transaction, source.fd,
                                         source_payload_digest);
            if (result < 0)
                goto done;
            source_digest_valid = 1;
            disk_status_phase_at("Re-verifying resumed payload on HDD",
                                 "HDD-only SHA-256 read-back against source digest");
            result = verify_target_digest(transaction, &plan, &layout,
                                          target_fd, source_payload_digest);
            if (result < 0)
                goto done;
        }
        if (!source_digest_valid && !verified_this_run) {
            result = HDL_INSTALL_VERIFY_FAILED;
            goto done;
        }
        metadata_options.game_title = transaction->game_title;
        metadata_options.startup = transaction->startup;
        metadata_options.disc_type = transaction->disc_type;
        metadata_options.layer1_start = transaction->layer1_start;
        metadata_options.hdl_compat_flags = transaction->hdl_compat_flags;
        metadata_options.opl_compat_flags = transaction->opl_compat_flags;
        metadata_options.dma_type = transaction->dma_type;
        metadata_options.dma_mode = transaction->dma_mode;
        result = hdl_metadata_build(&plan, layout.starts, layout.count,
                                    &metadata_options, metadata);
        if (result < 0)
            goto done;
        metadata_valid = 1;
        disk_status_phase_at("Committing verified game metadata last",
                             "Main partition attribute area / HDL metadata");
        disk_status_io(DISK_STATUS_WRITE,
                       layout.starts[0] + HDL_METADATA_LBA_OFFSET,
                       2u, transaction->total_sectors,
                       transaction->total_sectors);
        result = fileXioIoctl2(target_fd,
                               HDL_STREAM_IOCTL2_COMMIT_METADATA,
                               metadata, sizeof(metadata), NULL, 0);
        if (result < 0) {
            result = HDL_INSTALL_METADATA_FAILED;
            goto done;
        }
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_METADATA_COMMITTED,
            transaction->total_sectors);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_METADATA_COMMITTED) {
        /* A stage-4 -> stage-5 transition in this invocation already built the
         * exact canonical block that was committed. Keep using that same
         * transaction-owned buffer for read-back comparison. Only a resumed
         * stage-5 transaction needs to reconstruct it. */
        if (!metadata_valid) {
            metadata_options.game_title = transaction->game_title;
            metadata_options.startup = transaction->startup;
            metadata_options.disc_type = transaction->disc_type;
            metadata_options.layer1_start = transaction->layer1_start;
            metadata_options.hdl_compat_flags = transaction->hdl_compat_flags;
            metadata_options.opl_compat_flags = transaction->opl_compat_flags;
            metadata_options.dma_type = transaction->dma_type;
            metadata_options.dma_mode = transaction->dma_mode;
            result = hdl_metadata_build(&plan, layout.starts, layout.count,
                                        &metadata_options, metadata);
            if (result < 0) {
                result = HDL_INSTALL_METADATA_FAILED;
                goto done;
            }
        }
        if (!target_metadata_matches(transaction->target, metadata)) {
            result = HDL_INSTALL_METADATA_FAILED;
            goto done;
        }
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_COMPLETE,
            transaction->total_sectors);
        if (result < 0)
            goto done;
        journal_remove();
    }
    result = 0;
done:
    if (target_fd >= 0)
        fileXioClose(target_fd);
    if (source.fd >= 0)
        fileXioClose(source.fd);
    disk_status_end();
    pad_activity_end();
    session_log_line("HDL transaction target=%s stage=%u progress=%llu/%llu result=%d",
                     transaction->target, (unsigned int)transaction->stage,
                     (unsigned long long)transaction->completed_sectors,
                     (unsigned long long)transaction->total_sectors, result);
    return result;
}
