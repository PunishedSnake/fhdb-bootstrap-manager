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
 * Keep these symbols tiny and strong so fileXioInit()/Exit() retain their RPC,
 * semaphore and reset semantics while the optional POSIX adapter objects remain
 * unreferenced. Any future application use of fopen/open/stat/opendir/FILE I/O
 * must remove this policy or provide an explicitly reviewed equivalent path.
 */

void _ps2sdk_fileXio_init(void)
{
}

void _ps2sdk_fileXio_deinit(void)
{
}
