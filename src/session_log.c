/* PS2-specific bounded session logging and HDDMAN.LOG persistence. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <delaythread.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "session_log.h"
#include "storage.h"

#define SESSION_LOG_SIZE 16384u
#define SESSION_LOG_ROTATE_SIZE (128u * 1024u)
#define SESSION_LOG_PATH_SIZE 64u
#define USB_WRITE_ATTEMPTS 20
#define USB_WRITE_RETRY_DELAY_US 250000

static char session_log[SESSION_LOG_SIZE];
static unsigned int session_log_length;
static unsigned int session_log_saved[STORAGE_TARGET_COUNT];
static unsigned int session_log_sequence;

static void append_text(char *buffer, unsigned int capacity,
                        unsigned int *length, const char *format, ...)
{
    va_list arguments;
    int written;

    if (*length >= capacity - 1)
        return;
    va_start(arguments, format);
    written = vsnprintf(buffer + *length, capacity - *length,
                        format, arguments);
    va_end(arguments);
    if (written < 0)
        return;
    if ((unsigned int)written >= capacity - *length)
        *length = capacity - 1;
    else
        *length += (unsigned int)written;
}

void session_log_line(const char *format, ...)
{
    char line[256];
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (written < 0)
        return;
    line[sizeof(line) - 1] = '\0';
    append_text(session_log, sizeof(session_log), &session_log_length,
                "[%04u] %s\n", ++session_log_sequence, line);
}

int session_log_flush(unsigned int storage)
{
    char path[SESSION_LOG_PATH_SIZE];
    iox_stat_t existing;
    unsigned int start;
    int attempts;
    int truncate = 0;
    int result;

    if (storage >= STORAGE_TARGET_COUNT || session_log_length == 0)
        return -1;
    storage_path(path, sizeof(path), storage, "HDDMAN.LOG");
    memset(&existing, 0, sizeof(existing));
    if (fileXioGetStat(path, &existing) >= 0 &&
        existing.size >= SESSION_LOG_ROTATE_SIZE) {
        truncate = 1;
        session_log_saved[storage] = 0;
    }
    start = session_log_saved[storage];
    if (start > session_log_length)
        start = 0;
    if (start == session_log_length)
        return 0;
    attempts = storage == 2 ? USB_WRITE_ATTEMPTS : 1;
    do {
        result = append_log_file(path, session_log + start,
                                 (int)(session_log_length - start), truncate);
        if (result >= 0)
            break;
        DelayThread(USB_WRITE_RETRY_DELAY_US);
    } while (--attempts > 0);
    if (result == 0)
        session_log_saved[storage] = session_log_length;
    return result;
}
