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

#include "hdl_tools/install_ui.inc"
#include "hdl_tools/game_ui.inc"
