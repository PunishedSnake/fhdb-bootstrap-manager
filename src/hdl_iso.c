/* Portable ISO9660 and PS2 SYSTEM.CNF inspection for the HDL installer. */

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hdl_iso.h"

#define ISO_PVD_SECTOR 16u
#define ISO_SYSTEM_CNF_LIMIT 8192u
#define ISO_CD_CONFIDENT_BYTES (900ull * 1024ull * 1024ull)
#define ISO_DVD5_MAX_BYTES 4700372992ull
#define PS2_DISC_TYPE_CD 0x12u
#define PS2_DISC_TYPE_DVD 0x14u

static uint16_t read_le16(const unsigned char *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t read_be16(const unsigned char *data)
{
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static uint32_t read_le32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t read_be32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static int read_source(const hdl_iso_source_t *source, uint64_t offset,
                       void *destination, size_t size)
{
    if (offset > source->image_bytes ||
        (uint64_t)size > source->image_bytes - offset)
        return HDL_ISO_READ_FAILED;
    if (source->read(source->context, offset, destination, size) != 0)
        return HDL_ISO_READ_FAILED;
    return 0;
}

static int ascii_equal_nocase(const unsigned char *left, const char *right,
                              size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (toupper((unsigned char)left[i]) !=
            toupper((unsigned char)right[i]))
            return 0;
    }
    return 1;
}

static void copy_volume_title(char destination[HDL_ISO_TITLE_MAX],
                              const unsigned char *source)
{
    size_t start = 0;
    size_t end = 32;
    size_t length;

    while (start < end && source[start] == ' ')
        start++;
    while (end > start && source[end - 1] == ' ')
        end--;
    length = end - start;
    memcpy(destination, source + start, length);
    destination[length] = '\0';
}

static int find_system_cnf(const hdl_iso_source_t *source,
                           uint32_t root_extent, uint32_t root_bytes,
                           uint32_t *file_extent, uint32_t *file_bytes)
{
    unsigned char sector[HDL_ISO_SECTOR_SIZE];
    uint64_t directory_offset = (uint64_t)root_extent * HDL_ISO_SECTOR_SIZE;
    uint32_t consumed = 0;

    while (consumed < root_bytes) {
        uint32_t available = root_bytes - consumed;
        uint32_t position = 0;
        int result;

        if (available > HDL_ISO_SECTOR_SIZE)
            available = HDL_ISO_SECTOR_SIZE;
        result = read_source(source, directory_offset + consumed,
                             sector, HDL_ISO_SECTOR_SIZE);
        if (result < 0)
            return result;

        while (position < available) {
            unsigned int record_length = sector[position];
            const unsigned char *record;
            unsigned int name_length;

            if (record_length == 0)
                break;
            if (record_length < 34 || position + record_length > available)
                return HDL_ISO_INVALID_ROOT;
            record = sector + position;
            name_length = record[32];
            if (33u + name_length > record_length)
                return HDL_ISO_INVALID_ROOT;
            if (name_length == 12 &&
                ascii_equal_nocase(record + 33, "SYSTEM.CNF;1", 12)) {
                uint32_t extent = read_le32(record + 2);
                uint32_t extent_be = read_be32(record + 6);
                uint32_t bytes = read_le32(record + 10);
                uint32_t bytes_be = read_be32(record + 14);

                if (extent != extent_be || bytes != bytes_be || bytes == 0)
                    return HDL_ISO_INVALID_ROOT;
                *file_extent = extent;
                *file_bytes = bytes;
                return 0;
            }
            position += record_length;
        }
        consumed += HDL_ISO_SECTOR_SIZE;
    }
    return HDL_ISO_SYSTEM_CNF_MISSING;
}

static int valid_startup(const char *startup)
{
    size_t i;

    if (strlen(startup) != 11 || startup[4] != '_' ||
        startup[8] != '.')
        return 0;
    for (i = 0; i < 4; i++) {
        if (!isalnum((unsigned char)startup[i]))
            return 0;
    }
    for (i = 5; i < 8; i++) {
        if (!isdigit((unsigned char)startup[i]))
            return 0;
    }
    return isdigit((unsigned char)startup[9]) &&
           isdigit((unsigned char)startup[10]);
}

static int parse_system_cnf(const unsigned char *data, size_t size,
                            char startup[HDL_ISO_STARTUP_MAX],
                            char disc_id[HDL_ISO_DISC_ID_MAX])
{
    size_t position = 0;

    while (position < size) {
        size_t begin = position;
        size_t end;
        size_t equals;
        size_t value_begin;
        size_t value_end;
        size_t base;
        size_t length;
        size_t i;

        while (position < size && data[position] != '\r' &&
               data[position] != '\n' && data[position] != '\0')
            position++;
        end = position;
        while (position < size && (data[position] == '\r' ||
               data[position] == '\n' || data[position] == '\0'))
            position++;
        while (begin < end && isspace((unsigned char)data[begin]))
            begin++;
        if (end - begin < 5 ||
            !ascii_equal_nocase(data + begin, "BOOT2", 5))
            continue;
        equals = begin + 5;
        while (equals < end && isspace((unsigned char)data[equals]))
            equals++;
        if (equals >= end || data[equals] != '=')
            continue;
        value_begin = equals + 1;
        while (value_begin < end && isspace((unsigned char)data[value_begin]))
            value_begin++;
        if (value_begin < end && (data[value_begin] == '\'' ||
                                  data[value_begin] == '"'))
            value_begin++;
        value_end = value_begin;
        while (value_end < end && !isspace((unsigned char)data[value_end]) &&
               data[value_end] != '\'' && data[value_end] != '"')
            value_end++;
        base = value_begin;
        for (i = value_begin; i < value_end; i++) {
            if (data[i] == '\\' || data[i] == '/' || data[i] == ':')
                base = i + 1;
        }
        if (value_end >= base + 2 && data[value_end - 2] == ';' &&
            data[value_end - 1] == '1')
            value_end -= 2;
        length = value_end - base;
        if (length >= HDL_ISO_STARTUP_MAX)
            return HDL_ISO_STARTUP_INVALID;
        for (i = 0; i < length; i++)
            startup[i] = (char)toupper((unsigned char)data[base + i]);
        startup[length] = '\0';
        if (!valid_startup(startup))
            return HDL_ISO_STARTUP_INVALID;

        memcpy(disc_id, startup, 4);
        disc_id[4] = '-';
        memcpy(disc_id + 5, startup + 5, 3);
        memcpy(disc_id + 8, startup + 9, 2);
        disc_id[10] = '\0';
        return 0;
    }
    return HDL_ISO_SYSTEM_CNF_INVALID;
}

int hdl_iso_probe(const hdl_iso_source_t *source, hdl_iso_info_t *info)
{
    unsigned char pvd[HDL_ISO_SECTOR_SIZE];
    unsigned char system_cnf[ISO_SYSTEM_CNF_LIMIT + 1];
    const unsigned char *root;
    uint32_t volume_sectors;
    uint32_t root_extent;
    uint32_t root_bytes;
    uint32_t file_extent;
    uint32_t file_bytes;
    size_t read_bytes;
    int result;

    if (source == NULL || info == NULL || source->read == NULL)
        return HDL_ISO_INVALID_ARGUMENT;
    if (source->image_bytes < 18ull * HDL_ISO_SECTOR_SIZE ||
        source->image_bytes % HDL_ISO_SECTOR_SIZE != 0 ||
        source->image_bytes / HDL_ISO_SECTOR_SIZE > UINT32_MAX)
        return HDL_ISO_IMAGE_SIZE_INVALID;

    memset(info, 0, sizeof(*info));
    result = read_source(source, (uint64_t)ISO_PVD_SECTOR *
                         HDL_ISO_SECTOR_SIZE, pvd, sizeof(pvd));
    if (result < 0)
        return result;
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0 || pvd[6] != 1 ||
        read_le16(pvd + 128) != HDL_ISO_SECTOR_SIZE ||
        read_be16(pvd + 130) != HDL_ISO_SECTOR_SIZE)
        return HDL_ISO_INVALID_PVD;
    volume_sectors = read_le32(pvd + 80);
    if (volume_sectors == 0 || volume_sectors != read_be32(pvd + 84) ||
        (uint64_t)volume_sectors * HDL_ISO_SECTOR_SIZE > source->image_bytes)
        return HDL_ISO_INVALID_PVD;

    root = pvd + 156;
    if (root[0] < 34 || root[32] != 1 || root[33] != 0)
        return HDL_ISO_INVALID_ROOT;
    root_extent = read_le32(root + 2);
    root_bytes = read_le32(root + 10);
    if (root_extent != read_be32(root + 6) ||
        root_bytes != read_be32(root + 14) || root_bytes == 0 ||
        (uint64_t)root_extent * HDL_ISO_SECTOR_SIZE + root_bytes >
            source->image_bytes)
        return HDL_ISO_INVALID_ROOT;

    result = find_system_cnf(source, root_extent, root_bytes,
                             &file_extent, &file_bytes);
    if (result < 0)
        return result;
    read_bytes = file_bytes;
    if (read_bytes > ISO_SYSTEM_CNF_LIMIT)
        read_bytes = ISO_SYSTEM_CNF_LIMIT;
    result = read_source(source, (uint64_t)file_extent * HDL_ISO_SECTOR_SIZE,
                         system_cnf, read_bytes);
    if (result < 0)
        return result;
    system_cnf[read_bytes] = '\0';
    result = parse_system_cnf(system_cnf, read_bytes, info->startup,
                              info->disc_id);
    if (result < 0)
        return result;

    info->image_bytes = source->image_bytes;
    info->image_sectors = (uint32_t)(source->image_bytes /
                                     HDL_ISO_SECTOR_SIZE);
    copy_volume_title(info->volume_title, pvd + 40);
    if (source->image_bytes > ISO_CD_CONFIDENT_BYTES) {
        info->disc_type = PS2_DISC_TYPE_DVD;
        info->media_type_confident = 1;
    } else {
        info->disc_type = PS2_DISC_TYPE_CD;
        info->media_type_confident = 0;
    }
    info->requires_layer_break = source->image_bytes > ISO_DVD5_MAX_BYTES;
    return 0;
}
