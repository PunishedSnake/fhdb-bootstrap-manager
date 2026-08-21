/*
 * Compatibility surface for controller screens that still build text through
 * the historical libdebug API.
 *
 * libdebug is now allowed to provide init_scr() as the hardware-proven CRT /
 * read-circuit bootstrap. The linker wraps its drawing entry points, however,
 * so application text and clears still go exclusively through gs_ui_ps2.
 */

#include <debug.h>
#include <stdarg.h>

#include "gs_ui_ps2.h"

void __wrap_scr_clear(void)
{
    gs_ui_console_clear();
}

void __wrap_scr_vprintf(const char *format, va_list arguments)
{
    gs_ui_console_vprintf(format, arguments);
}

void __wrap_scr_printf(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    gs_ui_console_vprintf(format, arguments);
    va_end(arguments);
}
