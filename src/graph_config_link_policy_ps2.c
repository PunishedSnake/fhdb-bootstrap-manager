/*
 * Phase-1 link policy for the pinned PS2SDK graph library.
 *
 * CURRENT IMPLEMENTATION (PS2SDK v2.0.0 / b12f8af): graph_mode.o carries a
 * reference to graph_make_config(), so the linker extracts the monolithic
 * graph_config.o archive member even though this application never calls the
 * graph configuration-file API. That member also references fopen/fread/fwrite
 * and formatted I/O, which can retain unrelated Newlib stdio code.
 *
 * The pinned graph_make_config() implementation overwrites the same output
 * pointer for every field; its final observable output is therefore only
 * "<y>:" and it returns 0. Preserve that exact current behaviour here without
 * general-purpose stdio. If the application ever starts using graph_get_config,
 * graph_set_config, graph_load_config or graph_save_config, this policy must be
 * re-audited against the then-current PS2SDK source rather than silently
 * becoming an application API implementation.
 */

#include <graph_config.h>

static char *write_signed_decimal(char *out, int value)
{
    char digits[10];
    unsigned int magnitude;
    unsigned int count = 0;

    if (value < 0) {
        *out++ = '-';
        magnitude = 0u - (unsigned int)value;
    } else {
        magnitude = (unsigned int)value;
    }

    do {
        digits[count++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u);

    while (count != 0u)
        *out++ = digits[--count];
    return out;
}

int graph_make_config(int mode, int interlace, int ffmd, int x, int y,
                      int flicker_filter, char *config)
{
    char *end;

    (void)mode;
    (void)interlace;
    (void)ffmd;
    (void)x;
    (void)flicker_filter;

    end = write_signed_decimal(config, y);
    *end++ = ':';
    *end = '\0';
    return 0;
}
