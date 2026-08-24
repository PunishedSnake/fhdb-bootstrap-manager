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

/* A new install used to run the complete raw APA walker in recheck_disk() and
 * then immediately run it a second time solely to answer "does this generated
 * target name already exist?". Cache the generated target before planning so
 * the one required admission walk can answer free-space and collision
 * questions at the same time. */
static char hdl_pending_target[HDL_PARTITION_ID_MAX];
static int hdl_pending_target_result_valid;
static int hdl_pending_target_exists;

/* New-install source path is remembered only long enough to turn useless ISO
 * volume labels such as SLUS_21678 into a human title from the filename. This
 * does not affect source identity, which remains SYSTEM.CNF + fingerprint. */
static char hdl_selected_iso_path[HDL_TRANSACTION_SOURCE_PATH_MAX];

/* The destructive transaction performs a second admission check after the
 * confirmation chord. For a brand-new install that pass must prove the exact
 * generated target is still absent. Once it has done so, the immediately
 * following legacy fileXioOpen(hdd0:<target>, O_RDONLY) check is redundant and
 * particularly expensive on very large APA chains, so it is satisfied from
 * this raw-walker result instead of traversing the chain a third time. */
static char hdl_transaction_guard_target[HDL_PARTITION_ID_MAX];
static int hdl_transaction_guard_active;
static int hdl_transaction_guard_checked;
static int hdl_transaction_guard_found;
static int hdl_transaction_guard_lookup_consumed;

/* Keep the one hdl0: descriptor owned by execute_transaction visible to the
 * final metadata verification wrapper. Re-opening hdd0:<target> while hdl0:
 * already owns the same APA file slot returns EBUSY in ps2hdd, which is exactly
 * what dev15 hit after a successful METADATA_COMMITTED journal transition. */
static int hdl_active_target_fd = -1;

static void hdl_transaction_guard_disarm(void)
{
    hdl_transaction_guard_target[0] = '\0';
    hdl_transaction_guard_active = 0;
    hdl_transaction_guard_checked = 0;
    hdl_transaction_guard_found = 1;
    hdl_transaction_guard_lookup_consumed = 0;
}

static void hdl_transaction_guard_arm(const char *target)
{
    hdl_transaction_guard_disarm();
    if (target == NULL || target[0] == '\0')
        return;
    snprintf(hdl_transaction_guard_target,
             sizeof(hdl_transaction_guard_target), "%s", target);
    hdl_transaction_guard_active = 1;
}

static int hdl_install_partition_id(const char *disc_id, const char *title,
                                    char destination[HDL_PARTITION_ID_MAX])
{
    int result = hdl_partition_id(disc_id, title, destination);

    hdl_pending_target[0] = '\0';
    hdl_pending_target_result_valid = 0;
    hdl_pending_target_exists = 1;
    if (result == 0)
        snprintf(hdl_pending_target, sizeof(hdl_pending_target), "%s",
                 destination);
    return result;
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

static int hdl_install_recheck_disk(uint32_t *max_partition_sectors,
                                    uint32_t *free_sectors)
{
    int result;
    int found = 0;

    app_ui_activity_message(
        "HDL HDD planning",
        hdl_pending_target[0] != '\0'
            ? "Validating APA, free space and the generated target in one raw pass."
            : "Validating the APA chain and calculating available allocation space.");
    if (hdl_pending_target[0] == '\0')
        return recheck_disk(max_partition_sectors, free_sectors);

    result = recheck_disk_target(hdl_pending_target, &found,
                                 max_partition_sectors, free_sectors);
    if (result == 0) {
        hdl_pending_target_exists = found;
        hdl_pending_target_result_valid = 1;
        session_log_line("HDL combined planning target=%s collision=%d",
                         hdl_pending_target, found);
    }
    return result;
}

static int hdl_cached_target_exists(const char *target)
{
    int found;

    /* Keep the historical helper referenced for builds where this fragment is
     * compiled with aggressive -Werror unused-function checking. The install
     * path deliberately does not call it because that would invoke the stock
     * APA name lookup we are replacing. */
    (void)target_exists;

    if (target == NULL || !hdl_pending_target_result_valid ||
        strcmp(target, hdl_pending_target) != 0) {
        session_log_line("HDL cached target lookup unavailable target=%s",
                         target != NULL ? target : "(null)");
        return 1;
    }
    found = hdl_pending_target_exists;
    session_log_line("HDL cached target lookup target=%s collision=%d",
                     target, found);

    /* This is now a pure cached lookup with no physical I/O. Do not render a
     * fake activity page here: on real hardware that direct GS frame could
     * remain visible while the following confirmation was already waiting for
     * L1+R1+X. The actual confirmation is rendered explicitly by install_ui. */
    hdl_pending_target_result_valid = 0;
    hdl_pending_target[0] = '\0';
    return found;
}

static int hdl_transaction_recheck_disk(uint32_t *max_partition_sectors,
                                        uint32_t *free_sectors)
{
    int found = 0;
    int result;

    if (!hdl_transaction_guard_active)
        return recheck_disk(max_partition_sectors, free_sectors);

    disk_status_phase_at("Write preflight: revalidating target absence",
                         "Raw APA chain, free space and generated target ID");
    result = recheck_disk_target(hdl_transaction_guard_target, &found,
                                 max_partition_sectors, free_sectors);
    if (result == 0) {
        hdl_transaction_guard_checked = 1;
        hdl_transaction_guard_found = found;
        session_log_line("HDL post-confirm raw guard target=%s collision=%d",
                         hdl_transaction_guard_target, found);
        if (found)
            return HDL_INSTALL_TARGET_EXISTS;
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

    if (path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
        (flags & FIO_O_CREAT) != 0) {
        /* apaOpen() performs its own ID walk as part of the actual create. The
         * raw post-confirm guard above has just proven the target absent; from
         * this point the stock call is the allocator itself, not a third
         * read-only collision probe. */
        hdl_transaction_guard_active = 0;
        disk_status_phase_at("Creating HDL main partition",
                             "Stock APA allocator after raw target guard");
        disk_status_io(DISK_STATUS_WRITE, 0, 0, 0, 0);
    } else if (path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
               flags == FIO_O_RDONLY) {
        disk_status_phase_at("Rechecking target collision before allocation",
                             "APA partition-name lookup");
    }

    result = fileXioOpen(path, flags, mode);
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

#define fileXioOpen hdl_status_fileXioOpen
#define fileXioClose hdl_status_fileXioClose
#define fileXioIoctl2 hdl_status_fileXioIoctl2
#define fileXioRemove hdl_status_fileXioRemove
#define target_metadata_matches(...) hdl_target_metadata_matches_active(__VA_ARGS__)
#define recheck_disk(...) hdl_transaction_recheck_disk(__VA_ARGS__)
#include "hdl_tools/transaction.inc"
#undef recheck_disk
#undef target_metadata_matches
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
    if (planned)
        hdl_transaction_guard_arm(transaction->target);
    else
        hdl_transaction_guard_disarm();

    /* The user has already confirmed an install/resume at this point. Make
     * any early raw validation READs show WRITE PREFLIGHT semantics even
     * before execute_transaction() opens its detailed activity scope. */
    disk_status_set_write_intent(1);
    result = execute_transaction(transaction);
    disk_status_set_write_intent(0);
    hdl_active_target_fd = -1;
    hdl_transaction_guard_disarm();
    return result;
}

/* Function-like wrappers avoid rewriting struct members such as
 * transaction.source_fingerprint while still intercepting the calls. */
#define open_source(...) hdl_install_open_source(__VA_ARGS__)
#define hdl_iso_probe(...) hdl_install_iso_probe(__VA_ARGS__)
#define source_fingerprint(...) hdl_install_source_fingerprint(__VA_ARGS__)
#define hdl_partition_id(...) hdl_install_partition_id(__VA_ARGS__)
#define recheck_disk(...) hdl_install_recheck_disk(__VA_ARGS__)
#define target_exists(...) hdl_cached_target_exists(__VA_ARGS__)
#define execute_transaction(...) hdl_execute_transaction_guarded(__VA_ARGS__)
#include "hdl_tools/install_ui.inc"
#undef execute_transaction
#undef target_exists
#undef recheck_disk
#undef hdl_partition_id
#undef source_fingerprint
#undef hdl_iso_probe
#undef open_source

#include "hdl_tools/game_ui.inc"
