/*
 * Narrow startup recovery hook around the first hdd0: HDIOC_STATUS query.
 *
 * main.c intentionally keeps its fail-closed status/header gates unchanged.
 * This wrapper runs immediately before that gate returns to main: after pad,
 * fileXio, storage selection, DEV9/ATA and ps2hdd are initialized, but before
 * the manager rejects a damaged master header. All non-startup devctl calls are
 * forwarded byte-for-byte to fileXioDevctl.
 */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <kernel.h>
#include <tamtypes.h>

#include <string.h>

#include "apa.h"
#include "repair_controller_ps2.h"

#define HDIOC_READSECTOR_LOCAL 0x6836
#define RECOVERY_CANCELLED_OR_BLOCKED (-341)

typedef struct {
    u32 lba;
    u32 size;
} recovery_read_t;

static unsigned char recovery_header[APA_HEADER_SIZE]
    __attribute__((aligned(64)));
static int startup_status_seen;

int __real_fileXioDevctl(const char *name, int cmd,
                         void *arg, unsigned int arglen,
                         void *bufp, unsigned int buflen);

static int read_raw_master(void)
{
    recovery_read_t transfer;

    transfer.lba = 0;
    transfer.size = 2;
    memset(recovery_header, 0, sizeof(recovery_header));
    return __real_fileXioDevctl("hdd0:", HDIOC_READSECTOR_LOCAL,
                                &transfer, sizeof(transfer),
                                recovery_header, sizeof(recovery_header));
}

static void restart_after_repair(void)
{
    static char *browser_args[] = {"BootBrowser", NULL};

    __real_fileXioDevctl("hdd0:", HDIOC_DEV9OFF,
                         NULL, 0, NULL, 0);
    ExecOSD(1, browser_args);
    SleepThread();
}

int __wrap_fileXioDevctl(const char *name, int cmd,
                         void *arg, unsigned int arglen,
                         void *bufp, unsigned int buflen)
{
    int status;
    int result;

    if (startup_status_seen || name == NULL ||
        strcmp(name, "hdd0:") != 0 || cmd != HDIOC_STATUS)
        return __real_fileXioDevctl(name, cmd, arg, arglen, bufp, buflen);

    /* Intercept exactly once. Nested devctl calls from the recovery UI go
       straight to the real fileXio implementation. */
    startup_status_seen = 1;
    status = __real_fileXioDevctl(name, cmd, arg, arglen, bufp, buflen);

    result = read_raw_master();
    if (result < 0)
        return status;

    result = repair_controller_startup(recovery_header, status);
    if (result == REPAIR_CONTROLLER_RESTART_REQUIRED)
        restart_after_repair();
    if (result == REPAIR_CONTROLLER_BLOCKED)
        return RECOVERY_CANCELLED_OR_BLOCKED;

    return status;
}
