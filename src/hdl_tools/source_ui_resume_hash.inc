/* Guarded on-console HDLoader installer and large-disk HDL catalogue. */

#include <debug.h>
#include <libpad.h>
#include <malloc.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>
#include <iox_stat.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apa.h"
#include "app_ui_ps2.h"
#include "disk_status_ps2.h"
#include "gs_ui_ps2.h"
#include "hdd_read.h"
#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
#include "hdl_hash_checkpoint.h"
#endif
#include "hdl_installer_ps2.h"
#include "hdl_iso.h"
#include "hdl_partition.h"
#include "hdl_stream_rpc.h"
#include "hdl_transaction.h"
#include "platform.h"
#include "session_log.h"
#include "sha256.h"
#include "storage.h"

#define HDL_INSTALL_JOURNAL "mass:/HDLINSTALL.TXN"
#define HDL_INSTALL_JOURNAL_NEW "mass:/HDLINSTALL.NEW"
#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
#define HDL_INSTALL_HASH_CHECKPOINT "mass:/HDLINSTALL.SHA"
#define HDL_INSTALL_HASH_CHECKPOINT_NEW "mass:/HDLINSTALL.SHN"
#endif
#define HDL_BROWSER_PAGE_SIZE 8u
#define HDL_INSTALL_IO_BYTES (64u * 1024u)
#define HDL_INSTALL_JOURNAL_INTERVAL_SECTORS 16384u
#define HDL_APA_TYPE 0x1337u
#define HDL_APA_FLAG_SUB 0x0001u
#define HDL_METADATA_LBA_OFFSET 0x0800u
#define HDL_CHAIN_NODE_LIMIT 16384u
#define HDL_METADATA_NOT_LOADED INT_MIN

enum {
    HDL_INSTALL_CANCELLED = -560,
    HDL_INSTALL_NO_IMAGES = -561,
    HDL_INSTALL_JOURNAL_INVALID = -562,
    HDL_INSTALL_DISK_UNSAFE = -563,
    HDL_INSTALL_DISK_TOO_LARGE = -564,
    HDL_INSTALL_NO_SPACE = -565,
    HDL_INSTALL_TARGET_EXISTS = -566,
    HDL_INSTALL_CREATE_FAILED = -567,
    HDL_INSTALL_LAYOUT_MISMATCH = -568,
    HDL_INSTALL_SOURCE_CHANGED = -569,
    HDL_INSTALL_COPY_FAILED = -570,
    HDL_INSTALL_VERIFY_FAILED = -571,
    HDL_INSTALL_METADATA_FAILED = -572,
    HDL_INSTALL_MEMORY_FAILED = -573,
    HDL_INSTALL_NO_GAMES = -574,
    HDL_INSTALL_TARGET_CHANGED = -575,
    HDL_INSTALL_TARGET_BUSY = -576,
    HDL_INSTALL_CHAIN_INVALID = -577
};

typedef struct {
    char name[128];
    char path[HDL_TRANSACTION_SOURCE_PATH_MAX];
    uint64_t bytes;
} hdl_image_entry_t;

typedef struct {
    int fd;
    uint64_t bytes;
} hdl_file_source_t;

typedef struct {
    char id[HDL_PARTITION_ID_MAX];
    hdl_metadata_info_t metadata;
    unsigned char metadata_sha256[32];
    uint64_t allocation_bytes;
    uint32_t main_lba;
    unsigned int apa_partition_count;
    int metadata_state;
} hdl_game_entry_t;

typedef struct {
    hdl_game_entry_t *games;
    unsigned int count;
    unsigned int capacity;
    unsigned int hdl_sub_count;
    unsigned int expected_sub_count;
    uint32_t total_sectors;
    uint64_t used_sectors;
    uint64_t free_sectors;
} hdl_catalog_t;

static unsigned char admission_header[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

static uint64_t stat_bytes(const iox_stat_t *stat)
{
    return ((uint64_t)stat->hisize << 32) | stat->size;
}

static int iso_name(const char *name)
{
    size_t length = strlen(name);

    return length > 4 && name[length - 4] == '.' &&
           (name[length - 3] == 'i' || name[length - 3] == 'I') &&
           (name[length - 2] == 's' || name[length - 2] == 'S') &&
           (name[length - 1] == 'o' || name[length - 1] == 'O');
}

static int compare_images(const void *left_value, const void *right_value)
{
    const hdl_image_entry_t *left = left_value;
    const hdl_image_entry_t *right = right_value;

    return strcmp(left->name, right->name);
}

static int scan_mass_images(hdl_image_entry_t **images_out,
                            unsigned int *count_out)
{
    iox_dirent_t entry;
    hdl_image_entry_t *images;
    unsigned int capacity = 32u;
    unsigned int count = 0;
    int directory;
    int result;

    if (images_out == NULL || count_out == NULL)
        return HDL_INSTALL_NO_IMAGES;
    *images_out = NULL;
    *count_out = 0;
    images = calloc(capacity, sizeof(*images));
    if (images == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    directory = fileXioDopen("mass:/");
    if (directory < 0) {
        free(images);
        return directory;
    }
    while ((result = fileXioDread(directory, &entry)) > 0) {
        hdl_image_entry_t *image;
        int written;

        if (!FIO_S_ISREG(entry.stat.mode) || !iso_name(entry.name))
            continue;
        if (count == capacity) {
            hdl_image_entry_t *grown;
            unsigned int next = capacity < 1024u ? capacity * 2u
                                                  : capacity + 1024u;

            grown = realloc(images, next * sizeof(*images));
            if (grown == NULL) {
                fileXioDclose(directory);
                free(images);
                return HDL_INSTALL_MEMORY_FAILED;
            }
            images = grown;
            memset(images + capacity, 0,
                   (next - capacity) * sizeof(*images));
            capacity = next;
        }
        image = &images[count];
        snprintf(image->name, sizeof(image->name), "%s", entry.name);
        written = snprintf(image->path, sizeof(image->path),
                           "mass:/%s", entry.name);
        if (written < 0 || (unsigned int)written >= sizeof(image->path))
            continue;
        image->bytes = stat_bytes(&entry.stat);
        count++;
    }
    fileXioDclose(directory);
    if (result < 0) {
        free(images);
        return result;
    }
    if (count == 0) {
        free(images);
        return HDL_INSTALL_NO_IMAGES;
    }
    qsort(images, count, sizeof(*images), compare_images);
    *images_out = images;
    *count_out = count;
    return 0;
}

static unsigned int page_count(unsigned int count)
{
    return (count + HDL_BROWSER_PAGE_SIZE - 1u) / HDL_BROWSER_PAGE_SIZE;
}

static unsigned int page_move_selection(unsigned int selected,
                                        unsigned int count, int direction)
{
    unsigned int pages = page_count(count);
    unsigned int page = selected / HDL_BROWSER_PAGE_SIZE;
    unsigned int row = selected % HDL_BROWSER_PAGE_SIZE;
    unsigned int start;
    unsigned int shown;

    if (pages <= 1u)
        return selected;
    if (direction < 0)
        page = (page + pages - 1u) % pages;
    else
        page = (page + 1u) % pages;
    start = page * HDL_BROWSER_PAGE_SIZE;
    shown = count - start;
    if (shown > HDL_BROWSER_PAGE_SIZE)
        shown = HDL_BROWSER_PAGE_SIZE;
    if (row >= shown)
        row = shown - 1u;
    return start + row;
}

static int select_image(const hdl_image_entry_t *images, unsigned int count)
{
    unsigned int selected = 0;

    for (;;) {
        const char *labels[HDL_BROWSER_PAGE_SIZE];
        const char *hints[HDL_BROWSER_PAGE_SIZE];
        unsigned char enabled[HDL_BROWSER_PAGE_SIZE];
        unsigned int page = selected / HDL_BROWSER_PAGE_SIZE;
        unsigned int start = page * HDL_BROWSER_PAGE_SIZE;
        unsigned int shown = count - start;
        unsigned int local = selected - start;
        unsigned int i;
        char size_hint[HDL_BROWSER_PAGE_SIZE][48];
        char status[160];
        u32 pressed;

        if (shown > HDL_BROWSER_PAGE_SIZE)
            shown = HDL_BROWSER_PAGE_SIZE;
        for (i = 0; i < shown; i++) {
            labels[i] = images[start + i].name;
            snprintf(size_hint[i], sizeof(size_hint[i]), "%llu MiB",
                     (unsigned long long)(images[start + i].bytes /
                                          1024u / 1024u));
            hints[i] = size_hint[i];
            enabled[i] = 1u;
        }
        snprintf(status, sizeof(status),
                 "%u ISO%s | page %u/%u | %llu MiB | LEFT/RIGHT page",
                 count, count == 1 ? "" : "s", page + 1u, page_count(count),
                 (unsigned long long)(images[selected].bytes /
                                      1024u / 1024u));
        gs_ui_render_menu("HDL installer: choose ISO", status,
                          labels, hints, enabled, shown, local);
        pressed = wait_for_press();
        if (pressed & PAD_UP) {
            if (local == 0)
                selected = start + shown - 1u;
            else
                selected--;
        }
        if (pressed & PAD_DOWN) {
            if (local + 1u >= shown)
                selected = start;
            else
                selected++;
        }
        if (pressed & PAD_LEFT)
            selected = page_move_selection(selected, count, -1);
        if (pressed & PAD_RIGHT)
            selected = page_move_selection(selected, count, 1);
        if (pressed & PAD_CROSS)
            return (int)selected;
        if (pressed & PAD_TRIANGLE)
            return -1;
    }
}

static int source_read(void *context, uint64_t offset,
                       void *destination, size_t size)
{
    hdl_file_source_t *source = context;
    unsigned char *cursor = destination;
    size_t complete = 0;

    if (source == NULL || offset > source->bytes ||
        (uint64_t)size > source->bytes - offset ||
        fileXioLseek64(source->fd, (s64)offset, FIO_SEEK_SET) != (s64)offset)
        return -1;
    while (complete < size) {
        int result = fileXioRead(source->fd, cursor + complete,
                                 (int)(size - complete));

        if (result <= 0)
            return -1;
        complete += (unsigned int)result;
    }
    return 0;
}

static int open_source(const char *path, uint64_t expected,
                       hdl_file_source_t *source)
{
    iox_stat_t stat;

    if (fileXioGetStat(path, &stat) < 0 || stat_bytes(&stat) != expected)
        return HDL_INSTALL_SOURCE_CHANGED;
    source->fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (source->fd < 0)
        return source->fd;
    source->bytes = expected;
    return 0;
}

static int source_fingerprint(hdl_file_source_t *source,
                              unsigned char digest[32])
{
    unsigned char *buffer;
    unsigned char size_bytes[8];
    sha256_context_t hash;
    uint64_t tail;
    unsigned int amount;
    unsigned int i;
    int result = 0;

    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    sha256_init(&hash);
    for (i = 0; i < sizeof(size_bytes); i++)
        size_bytes[i] = (unsigned char)(source->bytes >> (i * 8u));
    sha256_update(&hash, size_bytes, sizeof(size_bytes));
    amount = source->bytes < HDL_INSTALL_IO_BYTES ?
             (unsigned int)source->bytes : HDL_INSTALL_IO_BYTES;
    if (source_read(source, 0, buffer, amount) < 0) {
        result = HDL_INSTALL_SOURCE_CHANGED;
        goto done;
    }
    sha256_update(&hash, buffer, amount);
    tail = source->bytes > HDL_INSTALL_IO_BYTES ?
           source->bytes - HDL_INSTALL_IO_BYTES : 0;
    if (source_read(source, tail, buffer, amount) < 0) {
        result = HDL_INSTALL_SOURCE_CHANGED;
        goto done;
    }
    sha256_update(&hash, buffer, amount);
    sha256_final(&hash, digest);
done:
    free(buffer);
    return result;
}

static int source_identity_matches(hdl_file_source_t *source,
                                   const hdl_transaction_t *transaction)
{
    hdl_iso_source_t iso_source;
    hdl_iso_info_t info;
    int result;

    iso_source.read = source_read;
    iso_source.context = source;
    iso_source.image_bytes = source->bytes;
    result = hdl_iso_probe(&iso_source, &info);
    if (result < 0 || info.requires_layer_break ||
        info.image_bytes != transaction->source_bytes ||
        strcmp(info.startup, transaction->startup) != 0)
        return HDL_INSTALL_SOURCE_CHANGED;
    return 0;
}

#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
static void hash_checkpoint_remove(void)
{
    (void)fileXioRemove(HDL_INSTALL_HASH_CHECKPOINT);
    (void)fileXioRemove(HDL_INSTALL_HASH_CHECKPOINT_NEW);
}

static int hash_checkpoint_save(const hdl_transaction_t *transaction,
                                const sha256_context_t *context)
{
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    unsigned char verify[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    sha256_context_t restored;
    int result;

    result = hdl_hash_checkpoint_encode(transaction, context, record);
    if (result < 0)
        return result;
    result = write_whole_file(HDL_INSTALL_HASH_CHECKPOINT_NEW,
                              record, sizeof(record));
    if (result < 0)
        return result;
    result = read_exact_file(HDL_INSTALL_HASH_CHECKPOINT_NEW,
                             verify, sizeof(verify));
    if (result < 0 || memcmp(record, verify, sizeof(record)) != 0 ||
        hdl_hash_checkpoint_restore(verify, transaction, &restored) < 0)
        return HDL_INSTALL_JOURNAL_INVALID;
    (void)fileXioRemove(HDL_INSTALL_HASH_CHECKPOINT);
    result = fileXioRename(HDL_INSTALL_HASH_CHECKPOINT_NEW,
                           HDL_INSTALL_HASH_CHECKPOINT);
    return result < 0 ? result : 0;
}

static int hash_checkpoint_load(const hdl_transaction_t *transaction,
                                sha256_context_t *context)
{
    unsigned char record[HDL_HASH_CHECKPOINT_RECORD_SIZE];
    int result;

    result = read_exact_file(HDL_INSTALL_HASH_CHECKPOINT,
                             record, sizeof(record));
    if (result == 0)
        return hdl_hash_checkpoint_restore(record, transaction, context);
    result = read_exact_file(HDL_INSTALL_HASH_CHECKPOINT_NEW,
                             record, sizeof(record));
    if (result < 0)
        return result;
    return hdl_hash_checkpoint_restore(record, transaction, context);
}
#endif

static int journal_save(const hdl_transaction_t *transaction)
{
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];
    unsigned char verify[HDL_TRANSACTION_RECORD_SIZE];
    hdl_transaction_t decoded;
    int result;

#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
    if (transaction != NULL && transaction->completed_sectors == 0)
        hash_checkpoint_remove();
#endif
    result = hdl_transaction_encode(transaction, record);
    if (result < 0)
        return result;
    result = write_whole_file(HDL_INSTALL_JOURNAL_NEW, record, sizeof(record));
    if (result < 0)
        return result;
    result = read_exact_file(HDL_INSTALL_JOURNAL_NEW, verify, sizeof(verify));
    if (result < 0 || memcmp(record, verify, sizeof(record)) != 0 ||
        hdl_transaction_decode(verify, &decoded) < 0)
        return HDL_INSTALL_JOURNAL_INVALID;
    (void)fileXioRemove(HDL_INSTALL_JOURNAL);
    result = fileXioRename(HDL_INSTALL_JOURNAL_NEW, HDL_INSTALL_JOURNAL);
    return result < 0 ? result : 0;
}

static int journal_load(hdl_transaction_t *transaction)
{
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];
    int result = read_exact_file(HDL_INSTALL_JOURNAL, record, sizeof(record));

    if (result == 0 && hdl_transaction_decode(record, transaction) == 0)
        return 0;
    result = read_exact_file(HDL_INSTALL_JOURNAL_NEW,
                             record, sizeof(record));
    if (result < 0)
        return result;
    return hdl_transaction_decode(record, transaction);
}

static void journal_remove(void)
{
    (void)fileXioRemove(HDL_INSTALL_JOURNAL);
    (void)fileXioRemove(HDL_INSTALL_JOURNAL_NEW);
#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
    hash_checkpoint_remove();
#endif
}

static int target_path(const char *prefix, const char *target,
                       char *destination, unsigned int capacity)
{
    int written = snprintf(destination, capacity, "%s%s", prefix, target);

    return written < 0 || (unsigned int)written >= capacity ? -1 : 0;
}

static int read_target_metadata(const char *target,
                                unsigned char metadata[HDL_METADATA_SIZE])
{
    char path[64];
    unsigned int complete = 0;
    int fd;
    int result;

    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    result = fileXioLseek(fd, 0x100000, FIO_SEEK_SET);
    if (result != 0x100000) {
        fileXioClose(fd);
        return result < 0 ? result : HDL_INSTALL_METADATA_FAILED;
    }
    while (complete < HDL_METADATA_SIZE) {
        result = fileXioRead(fd, metadata + complete,
                             HDL_METADATA_SIZE - complete);
        if (result <= 0) {
            fileXioClose(fd);
            return result < 0 ? result : HDL_INSTALL_METADATA_FAILED;
        }
        complete += (unsigned int)result;
    }
    fileXioClose(fd);
    return 0;
}

static int target_has_metadata(const char *target)
{
    unsigned char metadata[HDL_METADATA_SIZE];
    int result = read_target_metadata(target, metadata);

    if (result < 0)
        return result;
    return metadata[0] == 0xed && metadata[1] == 0xfe &&
           metadata[2] == 0xad && metadata[3] == 0xde;
}

static int target_metadata_matches(const char *target,
                                   const unsigned char expected[HDL_METADATA_SIZE])
{
    unsigned char actual[HDL_METADATA_SIZE];
    int result = read_target_metadata(target, actual);

    return result == 0 && memcmp(actual, expected, sizeof(actual)) == 0;
}

static int target_exists(const char *target)
{
    iox_stat_t stat;
    char path[64];

    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return 1;
    return fileXioGetStat(path, &stat) >= 0;
}

static int remove_incomplete_target(const char *target)
{
    char path[64];
    int metadata_state = target_has_metadata(target);

    if (metadata_state > 0)
        return HDL_INSTALL_TARGET_EXISTS;
    if (metadata_state < 0)
        return metadata_state;
    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    return fileXioRemove(path);
}
