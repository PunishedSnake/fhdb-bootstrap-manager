#include <kernel.h>

/* HDL Tools controller is split by responsibility to keep the large PS2-only
 * implementation reviewable. These fragments form one translation unit. */
#include "hdl_tools/source_ui.inc"

/* source_ui.inc still owns the historical local name while the portable HDL
 * header owns the tested physical geometry used by the raw catalogue. */
#undef HDL_METADATA_LBA_OFFSET
#define HDL_METADATA_LBA_OFFSET HDL_METADATA_PHYSICAL_SECTOR_OFFSET

/*
 * The generic GS menu switches to its compact one-line layout when eight rows
 * are present. That is useful for terse menus, but it hid the per-game startup,
 * allocation size and APA-part count on every full HDL catalogue page while
 * the short final page showed them. Keep eight rows for ISO browsing, but use
 * six game rows so every catalogue entry always gets the same two-line layout.
 */
#define HDL_GAME_BROWSER_PAGE_SIZE 6u

static unsigned int game_page_count(unsigned int count)
{
    return (count + HDL_GAME_BROWSER_PAGE_SIZE - 1u) /
           HDL_GAME_BROWSER_PAGE_SIZE;
}

static unsigned int game_page_move_selection(unsigned int selected,
                                             unsigned int count,
                                             int direction)
{
    unsigned int pages = game_page_count(count);
    unsigned int page = selected / HDL_GAME_BROWSER_PAGE_SIZE;
    unsigned int row = selected % HDL_GAME_BROWSER_PAGE_SIZE;
    unsigned int start;
    unsigned int shown;

    if (pages <= 1u)
        return selected;
    if (direction < 0)
        page = (page + pages - 1u) % pages;
    else
        page = (page + 1u) % pages;
    start = page * HDL_GAME_BROWSER_PAGE_SIZE;
    shown = count - start;
    if (shown > HDL_GAME_BROWSER_PAGE_SIZE)
        shown = HDL_GAME_BROWSER_PAGE_SIZE;
    if (row >= shown)
        row = shown - 1u;
    return start + row;
}

#undef HDL_BROWSER_PAGE_SIZE
#define HDL_BROWSER_PAGE_SIZE HDL_GAME_BROWSER_PAGE_SIZE
#define page_count game_page_count
#define page_move_selection game_page_move_selection
#include "hdl_tools/catalog.inc"
#undef page_move_selection
#undef page_count
#undef HDL_BROWSER_PAGE_SIZE
#define HDL_BROWSER_PAGE_SIZE 8u

/* New-install source path is remembered only long enough to turn useless ISO
 * volume labels such as SLUS_21678 into a human title from the filename. This
 * does not affect source identity, which remains SYSTEM.CNF + fingerprint. */
static char hdl_selected_iso_path[HDL_TRANSACTION_SOURCE_PATH_MAX];

/* The post-confirm raw APA walk is the authoritative collision/free-space
 * preflight. v5 additionally lets a PLANNED journal own a target whose allocator
 * may already have started, so recovery can validate and remove only that exact
 * partial allocation before retrying creation. */
static char hdl_transaction_guard_target[HDL_PARTITION_ID_MAX];
static int hdl_transaction_guard_active;
static int hdl_transaction_guard_checked;
static int hdl_transaction_guard_found;
static int hdl_transaction_guard_lookup_consumed;
static int hdl_transaction_guard_allow_owned_target;
static hdl_transaction_t *hdl_active_transaction;

/* Keep the one hdl0: descriptor owned by execute_transaction visible to the
 * final metadata verification wrapper. Re-opening hdd0:<target> while hdl0:
 * already owns the same APA file slot returns EBUSY in ps2hdd, which is exactly
 * what dev15 hit after a successful METADATA_COMMITTED journal transition. */
static int hdl_active_target_fd = -1;

#include "hdl_tools/fast_io.inc"

/*
 * APA deletion does not erase the old partition payload. If the allocator
 * later reuses the same physical extent for another HDL install, a valid
 * 0xDEADFEED block from the deleted game can therefore still be sitting in the
 * new main partition's metadata area. Invalidate that area immediately after
 * FIO_O_CREAT succeeds, before any payload is copied or the journal advances
 * to PARTITIONS_CREATED. This restores the core invariant that incomplete HDL
 * allocations never look like completed games to OPL or the cleanup guard.
 */
static unsigned char hdl_zero_metadata[HDL_METADATA_SIZE]
    __attribute__((aligned(64)));

static int hdl_invalidate_created_metadata(int fd)
{
    unsigned char verify[4] __attribute__((aligned(64)));
    int result;

    disk_status_phase_at("Invalidating stale HDL metadata",
                         "New main partition attribute area before payload copy");
    disk_status_io(DISK_STATUS_WRITE, 0, 2u, 0, 0);
    result = fileXioLseek(fd, 0x100000, FIO_SEEK_SET);
    if (result != 0x100000)
        return HDL_INSTALL_CREATE_FAILED;
    result = fileXioWrite(fd, hdl_zero_metadata, sizeof(hdl_zero_metadata));
    if (result != (int)sizeof(hdl_zero_metadata))
        return HDL_INSTALL_CREATE_FAILED;
    result = fileXioIoctl2(fd, HIOCFLUSH, NULL, 0, NULL, 0);
    if (result < 0)
        return HDL_INSTALL_CREATE_FAILED;
    result = fileXioLseek(fd, 0x100000, FIO_SEEK_SET);
    if (result != 0x100000)
        return HDL_INSTALL_CREATE_FAILED;
    result = fileXioRead(fd, verify, sizeof(verify));
    if (result != (int)sizeof(verify) || verify[0] != 0 || verify[1] != 0 ||
        verify[2] != 0 || verify[3] != 0)
        return HDL_INSTALL_CREATE_FAILED;
    session_log_line("HDL new target stale metadata invalidated and read back");
    return 0;
}

static void hdl_transaction_guard_disarm(void)
{
    hdl_transaction_guard_target[0] = '\0';
    hdl_transaction_guard_active = 0;
    hdl_transaction_guard_checked = 0;
    hdl_transaction_guard_found = 1;
    hdl_transaction_guard_lookup_consumed = 0;
    hdl_transaction_guard_allow_owned_target = 0;
}

static void hdl_transaction_guard_arm(const char *target,
                                      int allow_owned_target)
{
    hdl_transaction_guard_disarm();
    if (target == NULL || target[0] == '\0')
        return;
    snprintf(hdl_transaction_guard_target,
             sizeof(hdl_transaction_guard_target), "%s", target);
    hdl_transaction_guard_active = 1;
    hdl_transaction_guard_allow_owned_target = allow_owned_target != 0;
}

static void normalize_iso_identity(const char *text, char *normalized,
                                   size_t capacity)
{
    size_t used = 0;

    if (normalized == NULL || capacity == 0)
        return;
    if (text != NULL) {
        while (*text != '\0' && used + 1u < capacity) {
            unsigned char ch = (unsigned char)*text++;

            if (ch >= 'a' && ch <= 'z')
                ch = (unsigned char)(ch - 'a' + 'A');
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
                normalized[used++] = (char)ch;
        }
    }
    normalized[used] = '\0';
}

static int iso_volume_title_is_identity(const hdl_iso_info_t *info)
{
    char volume[HDL_ISO_TITLE_MAX];
    char disc[HDL_ISO_DISC_ID_MAX];
    char startup[HDL_ISO_STARTUP_MAX];

    if (info == NULL || info->volume_title[0] == '\0')
        return 1;
    normalize_iso_identity(info->volume_title, volume, sizeof(volume));
    normalize_iso_identity(info->disc_id, disc, sizeof(disc));
    normalize_iso_identity(info->startup, startup, sizeof(startup));
    return volume[0] == '\0' || strcmp(volume, disc) == 0 ||
           strcmp(volume, startup) == 0;
}

static void replace_identity_volume_title(hdl_iso_info_t *info)
{
    const char *base;
    const char *colon;
    size_t length;
    size_t begin = 0;
    size_t end;
    size_t i;
    size_t used = 0;

    if (info == NULL || !iso_volume_title_is_identity(info) ||
        hdl_selected_iso_path[0] == '\0')
        return;
    base = strrchr(hdl_selected_iso_path, '/');
    colon = strrchr(hdl_selected_iso_path, ':');
    if (base != NULL)
        base++;
    else if (colon != NULL)
        base = colon + 1;
    else
        base = hdl_selected_iso_path;
    length = strlen(base);
    if (length > 4u && base[length - 4u] == '.' &&
        (base[length - 3u] == 'i' || base[length - 3u] == 'I') &&
        (base[length - 2u] == 's' || base[length - 2u] == 'S') &&
        (base[length - 1u] == 'o' || base[length - 1u] == 'O'))
        length -= 4u;
    while (begin < length && base[begin] == ' ')
        begin++;
    end = length;
    while (end > begin && base[end - 1u] == ' ')
        end--;
    if (end <= begin)
        return;
    for (i = begin; i < end && used + 1u < sizeof(info->volume_title); i++) {
        unsigned char ch = (unsigned char)base[i];

        info->volume_title[used++] =
            (ch < 0x20u || ch == 0x7fu) ? '_' : (char)ch;
    }
    info->volume_title[used] = '\0';
    session_log_line("HDL title fallback from ISO filename: %s",
                     info->volume_title);
}

static int hdl_install_open_source(const char *path, uint64_t expected,
                                   hdl_file_source_t *source)
{
    int result = open_source(path, expected, source);

    hdl_selected_iso_path[0] = '\0';
    if (result >= 0 && path != NULL)
        snprintf(hdl_selected_iso_path, sizeof(hdl_selected_iso_path),
                 "%s", path);
    return result;
}

/* Keep the currently silent source-validation stages visible on real hardware.
 * If a removable-media driver stalls, the last rendered page now identifies
 * the exact stage instead of leaving an unrelated HDD monitor frame behind. */
static int hdl_install_iso_probe(const hdl_iso_source_t *source,
                                 hdl_iso_info_t *info)
{
    int result;

    app_ui_activity_message("HDL ISO validation",
                            "Reading ISO9660 and SYSTEM.CNF from the selected mass:/ image.");
    result = hdl_iso_probe(source, info);
    if (result == 0)
        replace_identity_volume_title(info);
    return result;
}

static int hdl_install_source_fingerprint(hdl_file_source_t *source,
                                          unsigned char digest[32])
{
    app_ui_activity_message("HDL ISO validation",
                            "ISO identity accepted. Fingerprinting the source before HDD planning.");
    return source_fingerprint(source, digest);
}

static int hdl_remove_incomplete_target_journal(const char *target);

static int hdl_transaction_recheck_disk(uint32_t *max_partition_sectors,
                                        uint32_t *free_sectors)
{
    int found = 0;
    int result;

    if (!hdl_transaction_guard_active)
        return recheck_disk(max_partition_sectors, free_sectors);

    disk_status_phase_at(
        hdl_transaction_guard_allow_owned_target
            ? "Write preflight: validating journal-owned allocation"
            : "Write preflight: revalidating target absence",
        "Raw APA chain, free space and generated target ID");
    result = recheck_disk_target(hdl_transaction_guard_target, &found,
                                 max_partition_sectors, free_sectors);
    if (result == 0) {
        hdl_transaction_guard_checked = 1;
        hdl_transaction_guard_found = found;
        session_log_line(
            "HDL post-confirm raw guard target=%s collision=%d owned=%d",
            hdl_transaction_guard_target, found,
            hdl_transaction_guard_allow_owned_target);
        if (found && !hdl_transaction_guard_allow_owned_target)
            return HDL_INSTALL_TARGET_EXISTS;
        if (found && hdl_transaction_guard_allow_owned_target &&
            free_sectors != NULL) {
            /* The owned partial allocation will be removed after source
             * identity is revalidated. Avoid rejecting recovery merely because
             * those owned sectors are temporarily absent from free-space totals;
             * the stock allocator remains the final space authority on retry. */
            *free_sectors = UINT32_MAX;
        }
    }
    return result;
}

static int hdl_transaction_source_identity_matches(
    hdl_file_source_t *source, const hdl_transaction_t *transaction)
{
    int result = source_identity_matches(source, transaction);

    if (result == 0 && hdl_transaction_guard_active &&
        hdl_transaction_guard_allow_owned_target &&
        hdl_transaction_guard_checked && hdl_transaction_guard_found &&
        transaction != NULL &&
        strcmp(transaction->target, hdl_transaction_guard_target) == 0) {
        result = hdl_remove_incomplete_target_journal(transaction->target);
        if (result == 0) {
            hdl_transaction_guard_found = 0;
            hdl_transaction_guard_lookup_consumed = 0;
            session_log_line(
                "HDL removed journal-owned partial allocation before retry target=%s",
                transaction->target);
        }
    }
    return result;
}

/*
 * fileXio performs APA bookkeeping writes inside the IOP, so those writes do
 * not naturally pass through hdd_read_raw_sectors() or the payload streamer's
 * explicit telemetry. Publish the operation kind immediately before the
 * handful of write-sensitive calls used by HDL transactions. This keeps the
 * live monitor truthful instead of leaving the last preflight READ event on
 * screen while the driver is actually allocating, flushing or deleting.
 */
static int hdl_status_fileXioOpen(const char *path, int flags, int mode)
{
    int result;
    int created_hdd = path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
                      (flags & FIO_O_CREAT) != 0;

    if (path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
        flags == FIO_O_RDONLY && hdl_transaction_guard_active &&
        hdl_transaction_guard_checked && !hdl_transaction_guard_found &&
        !hdl_transaction_guard_lookup_consumed) {
        char expected[64];

        snprintf(expected, sizeof(expected), "hdd0:%s",
                 hdl_transaction_guard_target);
        if (strcmp(path, expected) == 0) {
            hdl_transaction_guard_lookup_consumed = 1;
            session_log_line(
                "HDL skipped redundant stock APA pre-create lookup target=%s",
                hdl_transaction_guard_target);
            return -1;
        }
    }

    if (created_hdd) {
        /* This is the last point before current ps2hdd may mutate APA. Persist
         * allocation ownership first. A failed journal commit returns without
         * calling the allocator, so no unjournaled partition can be created. */
        if (hdl_active_transaction != NULL &&
            hdl_active_transaction->stage == HDL_TRANSACTION_STAGE_PLANNED &&
            !hdl_active_transaction->allocation_armed) {
            hdl_active_transaction->allocation_armed = 1;
            hdl_active_transaction->record_version =
                HDL_TRANSACTION_RECORD_VERSION_CURRENT;
            result = journal_save(hdl_active_transaction);
            if (result < 0) {
                hdl_active_transaction->allocation_armed = 0;
                session_log_line(
                    "HDL allocator ownership journal failed before APA create result=%d",
                    result);
                return HDL_INSTALL_JOURNAL_INVALID;
            }
            session_log_line(
                "HDL allocator ownership armed target=%s before first APA create",
                hdl_active_transaction->target);
        }

        /* apaOpen() performs its own ID walk as part of the actual create. The
         * raw post-confirm guard has already established target ownership; from
         * this point the stock call is the allocator itself, not another
         * independent collision probe. */
        hdl_transaction_guard_active = 0;
        disk_status_phase_at("Creating HDL main partition",
                             "Stock APA allocator after durable ownership handoff");
        disk_status_io(DISK_STATUS_WRITE, 0, 0, 0, 0);
    } else if (path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
               flags == FIO_O_RDONLY) {
        disk_status_phase_at("Rechecking target collision before allocation",
                             "APA partition-name lookup");
    }

    result = fileXioOpen(path, flags, mode);
    if (result >= 0 && created_hdd) {
        int invalidate = hdl_invalidate_created_metadata(result);

        if (invalidate < 0) {
            char cleanup[64];

            fileXioClose(result);
            if (hdl_transaction_guard_target[0] != '\0') {
                snprintf(cleanup, sizeof(cleanup), "hdd0:%s",
                         hdl_transaction_guard_target);
                (void)fileXioRemove(cleanup);
            }
            session_log_line("HDL new target metadata invalidation failed result=%d",
                             invalidate);
            return HDL_INSTALL_CREATE_FAILED;
        }
    }
    if (result >= 0 && path != NULL && strncmp(path, "hdl0:", 5) == 0) {
        hdl_active_target_fd = result;
        session_log_line("HDL active stream opened fd=%d path=%s", result, path);
    }
    return result;
}

static int hdl_status_fileXioClose(int fd)
{
    if (fd == hdl_active_target_fd) {
        session_log_line("HDL active stream closed fd=%d", fd);
        hdl_fast_io_reset();
        hdl_active_target_fd = -1;
    }
    return fileXioClose(fd);
}

static int hdl_status_fileXioIoctl2(int fd, int command,
                                    void *argument, int argument_length,
                                    void *buffer, int buffer_length)
{
    if (command == HIOCADDSUB ||
        command == HDL_STREAM_IOCTL2_COMMIT_METADATA)
        disk_status_io(DISK_STATUS_WRITE, 0, 0, 0, 0);
    else if (command == HIOCFLUSH ||
             command == HDL_STREAM_IOCTL2_FLUSH)
        disk_status_io(DISK_STATUS_FLUSH, 0, 0, 0, 0);
    else if (command == HDL_STREAM_IOCTL2_READ_METADATA)
        disk_status_io(DISK_STATUS_VERIFY, 0, 2u, 0, 0);

    return fileXioIoctl2(fd, command, argument, argument_length,
                         buffer, buffer_length);
}

static int hdl_target_metadata_matches_active(
    const char *target, const unsigned char expected[HDL_METADATA_SIZE])
{
    unsigned char actual[HDL_METADATA_SIZE] __attribute__((aligned(64)));
    int result;
    int matches;

    /* Keep the old helper referenced, but do not call it here: it re-opens the
     * same hdd0: APA ID while hdl0: already owns that ps2hdd file slot. */
    (void)target_metadata_matches;

    if (hdl_active_target_fd < 0) {
        session_log_line("HDL final metadata readback missing active stream target=%s",
                         target != NULL ? target : "(null)");
        return 0;
    }

    disk_status_phase_at("Reading back committed HDL metadata",
                         "Existing hdl0: stream / no second APA open");
    result = fileXioIoctl2(hdl_active_target_fd,
                           HDL_STREAM_IOCTL2_READ_METADATA,
                           NULL, 0, actual, sizeof(actual));
    if (result < 0) {
        session_log_line("HDL final metadata readback target=%s fd=%d result=%d",
                         target != NULL ? target : "(null)",
                         hdl_active_target_fd, result);
        return 0;
    }
    matches = memcmp(actual, expected, sizeof(actual)) == 0;
    session_log_line("HDL final metadata readback target=%s fd=%d match=%d",
                     target != NULL ? target : "(null)",
                     hdl_active_target_fd, matches);
    return matches;
}

static int hdl_status_fileXioRemove(const char *path)
{
    int track_hdd = path != NULL && strncmp(path, "hdd0:", 5) == 0;
    int result;

    if (track_hdd) {
        pad_activity_begin();
        disk_status_begin_at("HDL game removal",
                             "Removing selected APA allocation",
                             "APA partition chain metadata and linked sub-partitions");
        disk_status_io(DISK_STATUS_WRITE, 0, 0, 0, 0);
    }

    result = fileXioRemove(path);

    if (track_hdd) {
        disk_status_end();
        pad_activity_end();
    }
    return result;
}

/*
 * APA deletion does not erase payload bytes, so an interrupted allocation can
 * inherit stale HDL metadata from an earlier occupant. A journal-owned target
 * may be deleted only after a complete raw-chain validation and exact journal
 * ownership check. During an armed resume the raw guard has already done that
 * chain walk; explicit cleanup performs it here before touching the HDD.
 */
static int hdl_remove_incomplete_target_journal(const char *target)
{
    hdl_transaction_t transaction;
    unsigned char metadata[HDL_METADATA_SIZE];
    hdl_metadata_info_t parsed;
    uint32_t maximum;
    uint32_t free_sectors;
    char path[64];
    int result;
    int has_magic;
    int found;
    int planned_owned;

    result = journal_load(&transaction);
    if (result < 0)
        return HDL_INSTALL_JOURNAL_INVALID;
    planned_owned = transaction.stage == HDL_TRANSACTION_STAGE_PLANNED &&
                    transaction.allocation_armed;
    if (target == NULL || strcmp(transaction.target, target) != 0 ||
        (!planned_owned &&
         (transaction.stage < HDL_TRANSACTION_STAGE_PARTITIONS_CREATED ||
          transaction.stage >= HDL_TRANSACTION_STAGE_METADATA_COMMITTED)))
        return HDL_INSTALL_TARGET_CHANGED;

    if (hdl_transaction_guard_active && hdl_transaction_guard_checked &&
        strcmp(target, hdl_transaction_guard_target) == 0) {
        found = hdl_transaction_guard_found;
    } else {
        found = 0;
        result = recheck_disk_target(target, &found, &maximum, &free_sectors);
        if (result < 0)
            return result;
    }
    if (!found)
        return 0;

    result = read_target_metadata(target, metadata);
    if (result < 0)
        return result;
    has_magic = metadata[0] == 0xed && metadata[1] == 0xfe &&
                metadata[2] == 0xad && metadata[3] == 0xde;
    if (has_magic) {
        result = hdl_metadata_parse(metadata, &parsed);
        if (result < 0 || strcmp(parsed.startup, transaction.startup) != 0 ||
            strcmp(parsed.game_title, transaction.game_title) != 0 ||
            parsed.disc_type != transaction.disc_type ||
            parsed.partition_count != transaction.partition_count) {
            session_log_line("HDL incomplete cleanup refused foreign metadata target=%s",
                             target);
            return HDL_INSTALL_TARGET_CHANGED;
        }
        session_log_line(
            "HDL incomplete cleanup accepted stale matching metadata target=%s stage=%u armed=%u",
            target, (unsigned int)transaction.stage,
            (unsigned int)transaction.allocation_armed);
    }

    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    return hdl_status_fileXioRemove(path);
}

#define fileXioOpen hdl_status_fileXioOpen
#define fileXioClose hdl_status_fileXioClose
#define fileXioIoctl2 hdl_status_fileXioIoctl2
#define fileXioRemove hdl_status_fileXioRemove
#define fileXioRead hdl_fast_fileXioRead
#define fileXioWrite hdl_fast_fileXioWrite
#define fileXioLseek64 hdl_fast_fileXioLseek64
#define target_metadata_matches(...) hdl_target_metadata_matches_active(__VA_ARGS__)
#define recheck_disk(...) hdl_transaction_recheck_disk(__VA_ARGS__)
#define source_identity_matches(...) hdl_transaction_source_identity_matches(__VA_ARGS__)
#include "hdl_tools/transaction.inc"
#undef source_identity_matches
#undef recheck_disk
#undef target_metadata_matches
#undef fileXioLseek64
#undef fileXioWrite
#undef fileXioRead
#undef fileXioRemove
#undef fileXioIoctl2
#undef fileXioClose
#undef fileXioOpen

static int hdl_execute_transaction_guarded(hdl_transaction_t *transaction)
{
    int planned = transaction != NULL &&
                  transaction->stage == HDL_TRANSACTION_STAGE_PLANNED;
    int result;

    hdl_active_target_fd = -1;
    hdl_active_transaction = transaction;
    hdl_fast_io_reset();
    if (planned)
        hdl_transaction_guard_arm(transaction->target,
                                  transaction->allocation_armed);
    else
        hdl_transaction_guard_disarm();

    /* The user has already confirmed an install/resume at this point. Make
     * any early raw validation READs show WRITE PREFLIGHT semantics even
     * before execute_transaction() opens its detailed activity scope. */
    disk_status_set_write_intent(1);
    result = execute_transaction(transaction);
    disk_status_set_write_intent(0);
    hdl_fast_io_reset();
    hdl_active_target_fd = -1;
    hdl_active_transaction = NULL;
    hdl_transaction_guard_disarm();
    return result;
}

/* Function-like wrappers avoid rewriting struct members such as
 * transaction.source_fingerprint while still intercepting the calls. */
#define open_source(...) hdl_install_open_source(__VA_ARGS__)
#define hdl_iso_probe(...) hdl_install_iso_probe(__VA_ARGS__)
#define source_fingerprint(...) hdl_install_source_fingerprint(__VA_ARGS__)
#define execute_transaction(...) hdl_execute_transaction_guarded(__VA_ARGS__)
#define remove_incomplete_target(...) hdl_remove_incomplete_target_journal(__VA_ARGS__)
#include "hdl_tools/install_ui.inc"
#undef remove_incomplete_target
#undef execute_transaction
#undef source_fingerprint
#undef hdl_iso_probe
#undef open_source

#include "hdl_tools/game_ui.inc"
