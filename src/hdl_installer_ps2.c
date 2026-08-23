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
 * target name already exist?". The second back-to-back traversal stalls on the
 * real 2 TB test disk. Cache the generated target before planning so the one
 * required admission walk can answer free-space and collision questions at
 * the same time. */
static char hdl_pending_target[HDL_PARTITION_ID_MAX];
static int hdl_pending_target_result_valid;
static int hdl_pending_target_exists;

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

/* Keep the currently silent source-validation stages visible on real hardware.
 * If a removable-media driver stalls, the last rendered page now identifies
 * the exact stage instead of leaving an unrelated HDD monitor frame behind. */
static int hdl_install_iso_probe(const hdl_iso_source_t *source,
                                 hdl_iso_info_t *info)
{
    app_ui_activity_message("HDL ISO validation",
                            "Reading ISO9660 and SYSTEM.CNF from the selected mass:/ image.");
    return hdl_iso_probe(source, info);
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
    app_ui_activity_message(
        "HDL target check",
        found
            ? "The completed APA planning pass found an existing partition with this name."
            : "The completed APA planning pass found no target-name collision; no second HDD scan is needed.");
    hdl_pending_target_result_valid = 0;
    hdl_pending_target[0] = '\0';
    return found;
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
    if (path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
        (flags & FIO_O_CREAT) != 0)
        disk_status_io(DISK_STATUS_WRITE, 0, 0, 0, 0);
    else if (path != NULL && strncmp(path, "hdd0:", 5) == 0 &&
             flags == FIO_O_RDONLY)
        disk_status_phase_at("Rechecking target collision before allocation",
                             "APA partition-name lookup");
    return fileXioOpen(path, flags, mode);
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

    return fileXioIoctl2(fd, command, argument, argument_length,
                         buffer, buffer_length);
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
#define fileXioIoctl2 hdl_status_fileXioIoctl2
#define fileXioRemove hdl_status_fileXioRemove
#include "hdl_tools/transaction.inc"
#undef fileXioRemove
#undef fileXioIoctl2
#undef fileXioOpen

/* Function-like wrappers avoid rewriting struct members such as
 * transaction.source_fingerprint while still intercepting the calls. */
#define hdl_iso_probe(...) hdl_install_iso_probe(__VA_ARGS__)
#define source_fingerprint(...) hdl_install_source_fingerprint(__VA_ARGS__)
#define hdl_partition_id(...) hdl_install_partition_id(__VA_ARGS__)
#define recheck_disk(...) hdl_install_recheck_disk(__VA_ARGS__)
#define target_exists(...) hdl_cached_target_exists(__VA_ARGS__)
#include "hdl_tools/install_ui.inc"
#undef target_exists
#undef recheck_disk
#undef hdl_partition_id
#undef source_fingerprint
#undef hdl_iso_probe

#include "hdl_tools/game_ui.inc"
