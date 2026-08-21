/*
 * Transitional compatibility surface for controller screens that still build
 * their text incrementally with the historical libdebug API.
 *
 * These symbols intentionally satisfy scr_clear/scr_printf/scr_vprintf before
 * libdebug is searched by the linker. The library remains linked only for the
 * built-in MSX font asset used by gs_ui_ps2; it no longer renders application
 * pixels or owns video state.
 */

#include <debug.h>
#include <stdarg.h>

#include "gs_ui_ps2.h"

void scr_clear(void)
{
    gs_ui_console_clear();
}

void scr_vprintf(const char *format, va_list arguments)
{
    gs_ui_console_vprintf(format, arguments);
}

void scr_printf(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    gs_ui_console_vprintf(format, arguments);
    va_end(arguments);
}
