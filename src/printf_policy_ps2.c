/*
 * Phase-1 bounded formatting policy.
 *
 * CURRENT IMPLEMENTATION (PS2DEV Newlib ee-v4.6.0): vsniprintf() has the same
 * bounded string-buffer contract as vsnprintf() but routes through
 * _svfiprintf_r, the integer-only formatter, instead of _svfprintf_r. The
 * latter retains dtoa/floating formatting support that this application does
 * not use in its audited runtime format strings.
 *
 * The linker wraps only snprintf/vsnprintf references. The Phase-1 format audit
 * is the companion correctness guard: introducing %a/%e/%f/%g formatting must
 * first remove this policy or add an explicitly separate floating formatter.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

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
