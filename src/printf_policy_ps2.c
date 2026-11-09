/*
 * Phase-1 bounded formatting policy.
 *
 * CURRENT IMPLEMENTATION (PS2DEV Newlib ee-v4.6.0): vsniprintf() has the same
 * bounded string-buffer contract as vsnprintf() but routes through
 * _svfiprintf_r, the integer-only formatter, instead of _svfprintf_r. The
 * latter retains dtoa/floating formatting support that this application does
 * not use in its audited runtime format strings.
 *
 * The linker wraps snprintf/vsnprintf references, plus printf: PS2SDK's
 * libcdvd (_CdCheckSCmd) reports an integer-only S-command diagnostic through
 * printf, which would otherwise link Newlib's floating vfprintf/dtoa/mprec
 * (~40 KiB of .text) for one message. Library printf output therefore goes
 * through the same integer-only vsniprintf into HDDMAN.LOG, where a retail
 * user can actually see it, instead of to an invisible stdout.
 * The Phase-1 format audit
 * is the companion correctness guard: introducing %a/%e/%f/%g formatting must
 * first remove this policy or add an explicitly separate floating formatter.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "session_log.h"

int __wrap_vsnprintf(char *buffer, size_t capacity,
                     const char *format, va_list arguments)
{
    return vsniprintf(buffer, capacity, format, arguments);
}

int __wrap_snprintf(char *buffer, size_t capacity,
                    const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsniprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return result;
}

int __wrap_printf(const char *format, ...)
{
    char line[128];
    va_list arguments;
    int result;
    size_t length;

    va_start(arguments, format);
    result = vsniprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (result < 0)
        return result;
    length = (size_t)result < sizeof(line) ? (size_t)result : sizeof(line) - 1;
    while (length != 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = '\0';
    session_log_line("SDK: %s", line);
    return result;
}
