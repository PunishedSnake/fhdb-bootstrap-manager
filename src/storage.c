/*
 * Storage-device selection and generic file helpers.
 *
 * This module contains no APA or rescue policy. It only provides the same
 * fileXio primitives and launch-device selection that lived in main.c in the
 * stable Torii line.
 */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

const storage_target_t storage_targets[STORAGE_TARGET_COUNT] = {
    {"mc0", "mc0:", 0},
    {"mc1", "mc1:", 1},
    {"mass", "mass:", -1}
};

static unsigned int selected_storage = 0;

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

/* Prefer the device that launched the ELF when argv[0] carries that path. */
void select_launch_storage(int argc, char **argv)
{
    unsigned int i;

    if (argc <= 0 || argv == NULL || argv[0] == NULL)
        return;
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
