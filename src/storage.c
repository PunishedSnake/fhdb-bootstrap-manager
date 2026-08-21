/*
 * Storage-device selection and generic file helpers.
 *
 * This module contains no APA or rescue policy. It owns generic fileXio
 * primitives, launch-device/directory selection and the tiny application-local
 * UI theme preference that may live beside the launched ELF.
 */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"
#include "ui_theme_ps2.h"

#define UI_THEME_CONFIG_BYTES 256u

const storage_target_t storage_targets[STORAGE_TARGET_COUNT] = {
    {"mc0", "mc0:", 0},
    {"mc1", "mc1:", 1},
    {"mass", "mass:", -1}
};

static unsigned int selected_storage = 0;
static char launch_directory[STORAGE_LAUNCH_PATH_SIZE];
static int launch_directory_valid;

static const ui_theme_palette_t palettes[UI_THEME_COUNT] = {
    {
        "aqua", "Aqua",
        {7, 10, 17}, {14, 20, 30}, {19, 28, 41}, {54, 78, 101},
        {233, 238, 245}, {145, 164, 187}, {49, 205, 235}, {26, 81, 111},
        {76, 196, 137}, {230, 176, 72}, {230, 92, 92},
        {31, 29, 28}, {163, 121, 56}, {153, 145, 132}
    },
    {
        "amber", "Amber",
        {13, 10, 6}, {28, 21, 12}, {38, 29, 16}, {96, 73, 35},
        {245, 238, 220}, {188, 168, 132}, {238, 169, 57}, {108, 66, 17},
        {106, 201, 128}, {245, 186, 62}, {232, 92, 76},
        {38, 28, 23}, {173, 96, 48}, {169, 145, 127}
    },
    {
        "sakura", "Sakura",
        {14, 8, 14}, {28, 17, 29}, {39, 23, 39}, {91, 56, 88},
        {246, 232, 242}, {186, 153, 179}, {232, 103, 177}, {108, 43, 83},
        {92, 199, 153}, {237, 177, 83}, {235, 88, 107},
        {41, 24, 34}, {163, 81, 104}, {172, 139, 158}
    },
    {
        "mono", "Monochrome",
        {8, 8, 8}, {20, 20, 20}, {29, 29, 29}, {76, 76, 76},
        {239, 239, 239}, {165, 165, 165}, {210, 210, 210}, {78, 78, 78},
        {180, 220, 185}, {226, 205, 133}, {230, 140, 140},
        {34, 34, 34}, {106, 91, 72}, {139, 139, 139}
    }
};

static ui_theme_id_t current_theme = UI_THEME_AQUA;

unsigned int storage_selected(void)
{
    return selected_storage;
}

int storage_set_selected(unsigned int storage)
{
    if (storage >= STORAGE_TARGET_COUNT)
        return -1;
    selected_storage = storage;
    return 0;
}

/* Construct a root-level path for one supported storage device. */
void storage_path(char *destination, unsigned int capacity,
                  unsigned int storage, const char *filename)
{
    snprintf(destination, capacity, "%s/%s",
             storage_targets[storage].prefix, filename);
}

static void capture_launch_directory(const char *path)
{
    const char *slash;
    const char *colon;
    size_t length;

    launch_directory[0] = '\0';
    launch_directory_valid = 0;
    if (path == NULL || path[0] == '\0')
        return;

    colon = strchr(path, ':');
    if (colon == NULL)
        return;

    slash = strrchr(path, '/');
    if (slash != NULL && slash > colon) {
        length = (size_t)(slash - path);
    } else {
        /* A launcher may pass e.g. "mass:APP.ELF". Keep the device prefix as
           the directory instead of guessing a host-side working directory. */
        length = (size_t)(colon - path) + 1u;
    }

    if (length == 0 || length >= sizeof(launch_directory))
        return;
    memcpy(launch_directory, path, length);
    launch_directory[length] = '\0';
    launch_directory_valid = 1;
}

/* Prefer the device that launched the ELF when argv[0] carries that path. */
void select_launch_storage(int argc, char **argv)
{
    unsigned int i;

    launch_directory[0] = '\0';
    launch_directory_valid = 0;
    if (argc <= 0 || argv == NULL || argv[0] == NULL)
        return;

    capture_launch_directory(argv[0]);
    if (strncmp(argv[0], "mass0:", 6) == 0) {
        storage_set_selected(2);
        return;
    }
    for (i = 0; i < STORAGE_TARGET_COUNT; i++) {
        size_t prefix_length = strlen(storage_targets[i].prefix);

        if (strncmp(argv[0], storage_targets[i].prefix, prefix_length) == 0) {
            storage_set_selected(i);
            return;
        }
    }
}

int storage_launch_file_path(char *destination, unsigned int capacity,
                             const char *filename)
{
    int written;

    if (destination == NULL || capacity == 0 || filename == NULL ||
        filename[0] == '\0')
        return -1;

    if (launch_directory_valid) {
        size_t length = strlen(launch_directory);
        const char *separator = length != 0 &&
                                launch_directory[length - 1u] == ':'
                                    ? "" : "/";

        written = snprintf(destination, capacity, "%s%s%s",
                           launch_directory, separator, filename);
    } else {
        written = snprintf(destination, capacity, "%s/%s",
                           storage_targets[storage_selected()].prefix,
                           filename);
    }

    if (written < 0 || (unsigned int)written >= capacity) {
        destination[0] = '\0';
        return -2;
    }
    return launch_directory_valid ? 0 : 1;
}

const char *storage_launch_directory(void)
{
    return launch_directory_valid ? launch_directory : NULL;
}

/* Write all bytes, handling short fileXio transfers correctly. */
int write_whole_file(const char *path, const void *data, int size)
{
    const unsigned char *source = data;
    int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    int total = 0;

    if (fd < 0)
        return fd;
    while (total < size) {
        int written = fileXioWrite(fd, source + total, size - total);

        if (written <= 0) {
            fileXioClose(fd);
            return written < 0 ? written : -1;
        }
        total += written;
    }
    fileXioClose(fd);
    return 0;
}

/* Append a file fragment, optionally replacing the existing file first. */
int append_log_file(const char *path, const void *data, int size, int truncate)
{
    int flags = FIO_O_WRONLY | FIO_O_CREAT;
    const unsigned char *source = data;
    int total = 0;
    int fd;

    flags |= truncate ? FIO_O_TRUNC : FIO_O_APPEND;
    fd = fileXioOpen(path, flags, 0666);
    if (fd < 0)
        return fd;
    while (total < size) {
        int written = fileXioWrite(fd, source + total, size - total);

        if (written <= 0) {
            fileXioClose(fd);
            return written < 0 ? written : -1;
        }
        total += written;
    }
    fileXioClose(fd);
    return 0;
}

/* Read exactly the requested size and reject truncation or trailing bytes. */
int read_exact_file(const char *path, void *data, int size)
{
    unsigned char *destination = data;
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    int total = 0;
    int result;

    if (fd < 0)
        return fd;
    result = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (result != size || fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return -1;
    }
    while (total < size) {
        result = fileXioRead(fd, destination + total, size - total);
        if (result <= 0) {
            fileXioClose(fd);
            return result < 0 ? result : -1;
        }
        total += result;
    }
    fileXioClose(fd);
    return 0;
}

/* Read a complete bounded file into heap memory for validation by its caller. */
int read_bounded_file(const char *path, unsigned int maximum_size,
                      unsigned char **data_out, unsigned int *size_out)
{
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    int size;
    int total = 0;
    unsigned char *data;

    if (fd < 0)
        return fd;
    size = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (size <= 0 || (unsigned int)size > maximum_size ||
        fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return -160;
    }
    data = malloc((unsigned int)size);
    if (data == NULL) {
        fileXioClose(fd);
        return -161;
    }
    while (total < size) {
        int received = fileXioRead(fd, data + total, size - total);

        if (received <= 0) {
            free(data);
            fileXioClose(fd);
            return received < 0 ? received : -162;
        }
        total += received;
    }
    fileXioClose(fd);
    *data_out = data;
    *size_out = (unsigned int)size;
    return 0;
}

/* Return a boolean existence result without exposing driver-specific errors. */
int path_exists(const char *path)
{
    iox_stat_t status;

    memset(&status, 0, sizeof(status));
    return fileXioGetStat(path, &status) >= 0;
}

/* Load a small configuration file and always provide a trailing NUL byte. */
int read_text_file(const char *path, char *buffer, unsigned int capacity)
{
    int fd;
    int size;
    int total = 0;

    if (capacity < 2)
        return -1;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    size = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (size < 0 || (unsigned int)size >= capacity ||
        fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return -2;
    }
    while (total < size) {
        int received = fileXioRead(fd, buffer + total, size - total);

        if (received <= 0) {
            fileXioClose(fd);
            return received < 0 ? received : -3;
        }
        total += received;
    }
    fileXioClose(fd);
    buffer[total] = '\0';
    return total;
}

/* Read ROMVER through the already initialized fileXio stack. */
int read_romver(char destination[RESCUE_CAPSULE_ROMVER_SIZE])
{
    int fd = fileXioOpen("rom0:ROMVER", FIO_O_RDONLY, 0);
    int received;

    memset(destination, 0, RESCUE_CAPSULE_ROMVER_SIZE);
    if (fd < 0)
        return fd;
    received = fileXioRead(fd, destination,
                           RESCUE_CAPSULE_ROMVER_SIZE - 1);
    fileXioClose(fd);
    if (received <= 0)
        return received < 0 ? received : -1;
    if (received >= (int)RESCUE_CAPSULE_ROMVER_SIZE)
        received = RESCUE_CAPSULE_ROMVER_SIZE - 1;
    destination[received] = '\0';
    return received;
}

static int identifier_equal(const char *left, const char *right)
{
    unsigned char a;
    unsigned char b;

    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *right != '\0') {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (tolower(a) != tolower(b))
            return 0;
    }
    return *left == '\0' && *right == '\0';
}

const ui_theme_palette_t *ui_theme_current(void)
{
    return &palettes[(unsigned int)current_theme];
}

ui_theme_id_t ui_theme_current_id(void)
{
    return current_theme;
}

const char *ui_theme_name(ui_theme_id_t id)
{
    if ((unsigned int)id >= UI_THEME_COUNT)
        return "Unknown";
    return palettes[(unsigned int)id].name;
}

const char *ui_theme_identifier(ui_theme_id_t id)
{
    if ((unsigned int)id >= UI_THEME_COUNT)
        return "unknown";
    return palettes[(unsigned int)id].id;
}

int ui_theme_set(ui_theme_id_t id)
{
    if ((unsigned int)id >= UI_THEME_COUNT)
        return -1;
    current_theme = id;
    return 0;
}

int ui_theme_set_by_identifier(const char *identifier)
{
    unsigned int i;

    for (i = 0; i < UI_THEME_COUNT; i++) {
        if (identifier_equal(identifier, palettes[i].id)) {
            current_theme = (ui_theme_id_t)i;
            return 0;
        }
    }
    return -1;
}

int ui_theme_config_path(char *destination, unsigned int capacity)
{
    return storage_launch_file_path(destination, capacity,
                                    UI_THEME_CONFIG_FILENAME);
}

static int parse_theme_config(char *buffer)
{
    char *line;

    line = strtok(buffer, "\r\n");
    while (line != NULL) {
        char *equals;

        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '#' || *line == ';' || *line == '\0') {
            line = strtok(NULL, "\r\n");
            continue;
        }
        equals = strchr(line, '=');
        if (equals != NULL) {
            char *key_end;
            char *value;
            char *value_end;

            *equals = '\0';
            key_end = equals - 1;
            while (key_end >= line && (*key_end == ' ' || *key_end == '\t'))
                *key_end-- = '\0';
            value = equals + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            value_end = value + strlen(value);
            while (value_end > value &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t'))
                *--value_end = '\0';

            if (identifier_equal(line, "theme"))
                return ui_theme_set_by_identifier(value);
        }
        line = strtok(NULL, "\r\n");
    }
    return -2;
}

int ui_theme_load_config(void)
{
    char path[STORAGE_LAUNCH_PATH_SIZE];
    char legacy_path[STORAGE_LAUNCH_PATH_SIZE];
    char buffer[UI_THEME_CONFIG_BYTES];
    int result;

    if (ui_theme_config_path(path, sizeof(path)) < 0)
        return -1;

    result = read_text_file(path, buffer, sizeof(buffer));
    if (result >= 0)
        return parse_theme_config(buffer);

    /* A present HDDMAN.CFG is authoritative. Do not hide a malformed or
       unreadable current config behind an older development filename. */
    if (path_exists(path))
        return result;

    if (storage_launch_file_path(legacy_path, sizeof(legacy_path),
                                 UI_THEME_LEGACY_CONFIG_FILENAME) < 0)
        return result;
    if (read_text_file(legacy_path, buffer, sizeof(buffer)) < 0)
        return result;

    /* Migration is deliberately read-only: do not rewrite/delete the old file
       during startup. The next explicit save always creates HDDMAN.CFG. */
    return parse_theme_config(buffer);
}

int ui_theme_save_config(void)
{
    char path[STORAGE_LAUNCH_PATH_SIZE];
    char buffer[128];
    int length;

    if (ui_theme_config_path(path, sizeof(path)) < 0)
        return -1;
    length = snprintf(buffer, sizeof(buffer),
                      "# PS2 HDD Bootstrap Manager UI\n"
                      "theme=%s\n",
                      ui_theme_identifier(current_theme));
    if (length < 0 || (unsigned int)length >= sizeof(buffer))
        return -2;
    return write_whole_file(path, buffer, length);
}
