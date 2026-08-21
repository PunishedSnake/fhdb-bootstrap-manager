/*
 * MBR source filename compatibility for manual bootstrap installation.
 *
 * Sony software and other PS2 HDD tooling use MBR.XIN for the installable
 * MBR KELF. Some community installers historically shipped the same kind of
 * payload as MBR.XLF. Berion on PSX-Place pointed out the naming mismatch.
 *
 * Keep the 0.3.0 installation path untouched and interpose only fileXioOpen().
 * When that path attempts to open a root-level MBR.XLF, prefer a sibling
 * MBR.XIN if it actually exists. If MBR.XIN exists but cannot be opened, its
 * error is returned instead of silently falling back; this preserves the
 * manager's fail-closed behaviour. If MBR.XIN is absent, MBR.XLF continues to
 * work exactly as it did before 0.3.1.
 */

#include <fileXio_rpc.h>
#include <io_common.h>

#include <stddef.h>
#include <string.h>

#define MBR_PATH_CAPACITY 64

/* GNU ld supplies this symbol when linking with --wrap=fileXioOpen. */
extern int __real_fileXioOpen(const char *name, int flags, int mode);

static int is_legacy_mbr_source_path(const char *path, size_t length)
{
    static const char suffix[] = "/MBR.XLF";
    const size_t suffix_length = sizeof(suffix) - 1;

    if (path == NULL || length < suffix_length)
        return 0;
    return memcmp(path + length - suffix_length, suffix, suffix_length) == 0;
}

int __wrap_fileXioOpen(const char *name, int flags, int mode)
{
    size_t length;

    if (name == NULL)
        return __real_fileXioOpen(name, flags, mode);

    length = strlen(name);
    if (is_legacy_mbr_source_path(name, length) &&
        length + 1 <= MBR_PATH_CAPACITY) {
        char preferred[MBR_PATH_CAPACITY];
        iox_stat_t status;

        memcpy(preferred, name, length + 1);
        memcpy(preferred + length - 3, "XIN", 3);
        memset(&status, 0, sizeof(status));

        if (fileXioGetStat(preferred, &status) >= 0) {
            /*
             * install_bootstrap() passes a writable stack path here. Updating
             * that buffer makes later diagnostics identify the file that was
             * actually selected, while leaving every non-MBR open untouched.
             */
            memcpy((char *)name + length - 3, "XIN", 3);
            return __real_fileXioOpen(preferred, flags, mode);
        }
    }

    return __real_fileXioOpen(name, flags, mode);
}
