/*
 * Phase-1 application policy for PS2SDK fileXio/newlib integration.
 *
 * CURRENT IMPLEMENTATION (PS2SDK v2.0.0 / b12f8af): fileXioInit() calls the
 * internal _ps2sdk_fileXio_init() hook. That hook switches Newlib's generic
 * POSIX fd-manager path table to fileXio and, through the companion constructor,
 * retains every POSIX adapter including __fileXioGetstatHelper(). The stat
 * adapter converts three timestamps through mktime(), which in this pinned
 * Newlib also retains timezone parsing and scanf machinery.
 *
 * fhdb-bootstrap-manager's storage contract is intentionally direct fileXio:
 * application filesystem operations use fileXioOpen/Read/Write/Lseek/etc. The
 * Newlib formatting used by the UI/log paths is memory/string formatting, not
 * fopen/open/stat-based file access. Therefore this build does not need fileXio
 * to replace Newlib's generic POSIX pathname backend.
 *
 * The startup libcglue backend still installs the older fio POSIX adapter for
 * stdin/stdout/stderr. Its generic __fioGetstatHelper() performs the same costly
 * mktime conversion even though this application never consumes POSIX file
 * timestamps. Keep mode and size semantics for incidental library probes, but
 * deliberately report zero timestamps. CI rejects application fopen/open/stat
 * call sites, so a future consumer cannot silently depend on this reduced
 * timestamp contract.
 */

#include <errno.h>
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <io_common.h>
#include <iox_stat.h>
#include <string.h>
#include <sys/stat.h>

void _ps2sdk_fileXio_init(void)
{
}

void _ps2sdk_fileXio_deinit(void)
{
}

static mode_t fio_mode_to_posix(unsigned int mode)
{
    mode_t result = 0;

    if (mode & FIO_SO_IFREG)
        result |= S_IFREG;
    if (mode & FIO_SO_IFDIR)
        result |= S_IFDIR;
    if (mode & FIO_SO_IROTH)
        result |= S_IRUSR | S_IRGRP | S_IROTH;
    if (mode & FIO_SO_IWOTH)
        result |= S_IWUSR | S_IWGRP | S_IWOTH;
    if (mode & FIO_SO_IXOTH)
        result |= S_IXUSR | S_IXGRP | S_IXOTH;
    return result;
}

int __fioGetstatHelper(const char *path, struct stat *status)
{
    io_stat_t iop_status;

    if (path == NULL || status == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strncmp(path, "tty", 3) == 0 && path[3] >= '0' && path[3] <= '9' &&
        path[4] == ':') {
        memset(status, 0, sizeof(*status));
        status->st_mode = S_IFCHR;
        return 0;
    }

    if (fioGetstat(path, &iop_status) < 0) {
        errno = ENOENT;
        return -1;
    }

    memset(status, 0, sizeof(*status));
    status->st_mode = fio_mode_to_posix(iop_status.mode);
    status->st_size = ((off_t)iop_status.hisize << 32) | (off_t)iop_status.size;
    status->st_blksize = 16 * 1024;
    status->st_blocks = status->st_size / 512;
    return 0;
}
