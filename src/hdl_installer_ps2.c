/* Guarded on-console HDLoader installer for ISO images on mass:. */

#include <debug.h>
#include <libpad.h>
#include <malloc.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>
#include <iox_stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apa.h"
#include "app_ui_ps2.h"
#include "disk_status_ps2.h"
#include "hdd_read.h"
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
#define HDL_INSTALL_MAX_IMAGES 64u
#define HDL_INSTALL_PAGE_SIZE 10u
#define HDL_INSTALL_IO_BYTES (64u * 1024u)
#define HDL_INSTALL_JOURNAL_INTERVAL_SECTORS 16384u

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
    HDL_INSTALL_MEMORY_FAILED = -573
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

static unsigned char admission_header[APA_HEADER_SIZE]
    __attribute__((aligned(64)));
static hdl_image_entry_t image_entries[HDL_INSTALL_MAX_IMAGES];

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

static void sort_images(hdl_image_entry_t *images, unsigned int count)
{
    unsigned int i;

    for (i = 1; i < count; i++) {
        hdl_image_entry_t value = images[i];
        unsigned int position = i;

        while (position != 0 && strcmp(images[position - 1].name,
                                       value.name) > 0) {
            images[position] = images[position - 1];
            position--;
        }
        images[position] = value;
    }
}

static int scan_mass_images(hdl_image_entry_t images[HDL_INSTALL_MAX_IMAGES],
                            unsigned int *count)
{
    iox_dirent_t entry;
    int directory;
    int result;

    *count = 0;
    directory = fileXioDopen("mass:/");
    if (directory < 0)
        return directory;
    while ((result = fileXioDread(directory, &entry)) > 0) {
        hdl_image_entry_t *image;
        int written;

        if (*count >= HDL_INSTALL_MAX_IMAGES)
            continue;
        if (!FIO_S_ISREG(entry.stat.mode) || !iso_name(entry.name))
            continue;
        image = &images[(*count)++];
        snprintf(image->name, sizeof(image->name), "%s", entry.name);
        written = snprintf(image->path, sizeof(image->path),
                           "mass:/%s", entry.name);
        if (written < 0 || (unsigned int)written >= sizeof(image->path)) {
            (*count)--;
            continue;
        }
        image->bytes = stat_bytes(&entry.stat);
    }
    fileXioDclose(directory);
    if (result < 0)
        return result;
    sort_images(images, *count);
    return *count == 0 ? HDL_INSTALL_NO_IMAGES : 0;
}

static int select_image(hdl_image_entry_t images[HDL_INSTALL_MAX_IMAGES],
                        unsigned int count)
{
    unsigned int page = 0;
    unsigned int selected = 0;

    for (;;) {
        app_ui_menu_item_t items[12];
        char hints[HDL_INSTALL_PAGE_SIZE][80];
        unsigned int indexes[12];
        unsigned int start = page * HDL_INSTALL_PAGE_SIZE;
        unsigned int shown = count - start;
        unsigned int item_count = 0;
        unsigned int i;
        char status[96];
        int choice;

        if (shown > HDL_INSTALL_PAGE_SIZE)
            shown = HDL_INSTALL_PAGE_SIZE;
        for (i = 0; i < shown; i++) {
            unsigned int index = start + i;

            snprintf(hints[i], sizeof(hints[i]), "%llu MiB | mass: root",
                     (unsigned long long)(images[index].bytes / 1024u / 1024u));
            items[item_count].label = images[index].name;
            items[item_count].hint = hints[i];
            items[item_count].enabled = 1;
            indexes[item_count++] = index;
        }
        if (start + shown < count) {
            items[item_count].label = "Next page";
            items[item_count].hint = "Show more ISO images";
            items[item_count].enabled = 1;
            indexes[item_count++] = UINT32_MAX;
        }
        if (page != 0) {
            items[item_count].label = "Previous page";
            items[item_count].hint = "Return to earlier ISO images";
            items[item_count].enabled = 1;
            indexes[item_count++] = UINT32_MAX - 1u;
        }
        snprintf(status, sizeof(status), "%u ISO image%s | page %u",
                 count, count == 1 ? "" : "s", page + 1u);
        if (selected >= item_count)
            selected = 0;
        choice = app_ui_menu_select("HDL installer: choose ISO", status,
                                    items, item_count, &selected);
        if (choice < 0)
            return -1;
        if (indexes[choice] == UINT32_MAX) {
            page++;
            selected = 0;
            continue;
        }
        if (indexes[choice] == UINT32_MAX - 1u) {
            page--;
            selected = 0;
            continue;
        }
        return (int)indexes[choice];
    }
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

static int journal_save(const hdl_transaction_t *transaction)
{
    unsigned char record[HDL_TRANSACTION_RECORD_SIZE];
    unsigned char verify[HDL_TRANSACTION_RECORD_SIZE];
    hdl_transaction_t decoded;
    int result;

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
}

static const char *allocation_name(uint64_t bytes)
{
    static const char *const names[] = {"128M", "256M", "512M",
                                        "1G", "2G", "4G"};
    static const uint64_t sizes[] = {
        128ull << 20, 256ull << 20, 512ull << 20,
        1024ull << 20, 2048ull << 20, 4096ull << 20
    };
    unsigned int i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (bytes == sizes[i])
            return names[i];
    }
    return NULL;
}

static int target_path(const char *prefix, const char *target,
                       char *destination, unsigned int capacity)
{
    int written = snprintf(destination, capacity, "%s%s", prefix, target);

    return written < 0 || (unsigned int)written >= capacity ? -1 : 0;
}

static int target_has_metadata(const char *target)
{
    unsigned char metadata[HDL_METADATA_SIZE];
    char path[64];
    int fd;
    int result;

    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return 1;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    result = fileXioLseek(fd, 0x100000, FIO_SEEK_SET);
    if (result == 0x100000)
        result = fileXioRead(fd, metadata, sizeof(metadata));
    fileXioClose(fd);
    return result == (int)sizeof(metadata) &&
           metadata[0] == 0xed && metadata[1] == 0xfe &&
           metadata[2] == 0xad && metadata[3] == 0xde;
}

static int target_exists(const char *target)
{
    iox_stat_t stat;
    char path[64];

    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return 1;
    return fileXioGetStat(path, &stat) >= 0;
}

static int target_metadata_matches(const char *target,
                                   const unsigned char expected[HDL_METADATA_SIZE])
{
    unsigned char actual[HDL_METADATA_SIZE];
    char path[64];
    int fd;
    int result;

    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return 0;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    result = fileXioLseek(fd, 0x100000, FIO_SEEK_SET);
    if (result == 0x100000)
        result = fileXioRead(fd, actual, sizeof(actual));
    fileXioClose(fd);
    return result == (int)sizeof(actual) &&
           memcmp(actual, expected, sizeof(actual)) == 0;
}

static int remove_incomplete_target(const char *target)
{
    char path[64];

    if (target_has_metadata(target))
        return HDL_INSTALL_TARGET_EXISTS;
    if (target_path("hdd0:", target, path, sizeof(path)) < 0)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    return fileXioRemove(path);
}

static int recheck_disk(uint32_t *max_partition_sectors,
                        uint32_t *free_sectors)
{
    int status = fileXioDevctl("hdd0:", HDIOC_STATUS,
                               NULL, 0, NULL, 0);
    int total;
    int maximum;
    int free_space;

    if (status != 0 || hdd_read_raw_sectors(0, 2, admission_header) < 0 ||
        !is_standard_apa_header(admission_header) ||
        is_hybrid_gpt(admission_header))
        return HDL_INSTALL_DISK_UNSAFE;
    total = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR,
                          NULL, 0, NULL, 0);
    if (total < 0)
        return HDL_INSTALL_DISK_TOO_LARGE;
    maximum = fileXioDevctl("hdd0:", HDIOC_MAXSECTOR,
                            NULL, 0, NULL, 0);
    free_space = fileXioDevctl("hdd0:", HDIOC_FREESECTOR,
                               NULL, 0, NULL, 0);
    if (maximum <= 0 || free_space < 0)
        return HDL_INSTALL_DISK_UNSAFE;
    *max_partition_sectors = (uint32_t)maximum;
    *free_sectors = (uint32_t)free_space;
    return 0;
}

static int create_partitions(const hdl_transaction_t *transaction,
                             const hdl_partition_plan_t *plan)
{
    char existing[64];
    char create[96];
    int fd;
    unsigned int i;

    target_path("hdd0:", transaction->target, existing, sizeof(existing));
    fd = fileXioOpen(existing, FIO_O_RDONLY, 0);
    if (fd >= 0) {
        fileXioClose(fd);
        if (target_has_metadata(transaction->target))
            return HDL_INSTALL_TARGET_EXISTS;
        if (remove_incomplete_target(transaction->target) < 0)
            return HDL_INSTALL_TARGET_EXISTS;
    }

    snprintf(create, sizeof(create), "hdd0:%s,,,%s,HDL",
             transaction->target,
             allocation_name(plan->slices[0].allocation_bytes));
    fd = fileXioOpen(create, FIO_O_RDWR | FIO_O_CREAT, 0666);
    if (fd < 0)
        return fd;
    for (i = 1; i < plan->count; i++) {
        const char *size = allocation_name(plan->slices[i].allocation_bytes);
        int result = fileXioIoctl2(fd, HIOCADDSUB, (void *)size,
                                   strlen(size) + 1u, NULL, 0);

        if (result < 0) {
            fileXioClose(fd);
            (void)remove_incomplete_target(transaction->target);
            return HDL_INSTALL_CREATE_FAILED;
        }
    }
    if (fileXioIoctl2(fd, HIOCFLUSH, NULL, 0, NULL, 0) < 0) {
        fileXioClose(fd);
        (void)remove_incomplete_target(transaction->target);
        return HDL_INSTALL_CREATE_FAILED;
    }
    fileXioClose(fd);
    return 0;
}

static int open_target(const hdl_transaction_t *transaction,
                       const hdl_partition_plan_t *plan,
                       hdl_stream_layout_t *layout)
{
    char path[64];
    int fd;
    unsigned int i;

    if (target_path("hdl0:", transaction->target, path, sizeof(path)) < 0)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    fd = fileXioOpen(path, FIO_O_RDWR, 0);
    if (fd < 0)
        return fd;
    if (fileXioIoctl2(fd, HDL_STREAM_IOCTL2_GET_LAYOUT,
                      NULL, 0, layout, sizeof(*layout)) < 0 ||
        layout->count != plan->count) {
        fileXioClose(fd);
        return HDL_INSTALL_LAYOUT_MISMATCH;
    }
    for (i = 0; i < plan->count; i++) {
        if ((uint64_t)layout->lengths[i] * 512u !=
                plan->slices[i].allocation_bytes || layout->starts[i] == 0) {
            fileXioClose(fd);
            return HDL_INSTALL_LAYOUT_MISMATCH;
        }
    }
    return fd;
}

static int read_exact_fd(int fd, unsigned char *buffer, unsigned int bytes)
{
    unsigned int complete = 0;

    while (complete < bytes) {
        int result = fileXioRead(fd, buffer + complete, bytes - complete);

        if (result <= 0)
            return result < 0 ? result : HDL_INSTALL_COPY_FAILED;
        complete += (unsigned int)result;
    }
    return 0;
}

static int write_exact_fd(int fd, const unsigned char *buffer,
                          unsigned int bytes)
{
    unsigned int complete = 0;

    while (complete < bytes) {
        int result = fileXioWrite(fd, buffer + complete, bytes - complete);

        if (result <= 0)
            return result < 0 ? result : HDL_INSTALL_COPY_FAILED;
        complete += (unsigned int)result;
    }
    return 0;
}

static uint32_t physical_lba(const hdl_partition_plan_t *plan,
                             const hdl_stream_layout_t *layout,
                             uint64_t payload_offset)
{
    unsigned int i;

    for (i = 0; i < plan->count; i++) {
        uint64_t begin = plan->slices[i].payload_offset;
        uint64_t end = begin + plan->slices[i].payload_bytes;

        if (payload_offset >= begin && payload_offset < end)
            return layout->starts[i] + (i == 0 ? 0x2000u : 4u) +
                   (uint32_t)((payload_offset - begin) / 512u);
    }
    return 0;
}

static int copy_payload(hdl_transaction_t *transaction,
                        const hdl_partition_plan_t *plan,
                        const hdl_stream_layout_t *layout,
                        int source_fd, int target_fd)
{
    unsigned char *buffer;
    uint64_t offset = transaction->completed_sectors * 2048u;
    uint64_t next_journal = transaction->completed_sectors +
                            HDL_INSTALL_JOURNAL_INTERVAL_SECTORS;
    int result = 0;

    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    if (fileXioLseek64(source_fd, (s64)offset, FIO_SEEK_SET) != (s64)offset ||
        fileXioLseek64(target_fd, (s64)offset, FIO_SEEK_SET) != (s64)offset) {
        result = HDL_INSTALL_COPY_FAILED;
        goto done;
    }
    while (offset < transaction->source_bytes) {
        uint64_t remaining = transaction->source_bytes - offset;
        unsigned int bytes = remaining > HDL_INSTALL_IO_BYTES ?
                             HDL_INSTALL_IO_BYTES : (unsigned int)remaining;
        u32 pressed = 0;

        result = read_exact_fd(source_fd, buffer, bytes);
        if (result < 0)
            goto done;
        result = write_exact_fd(target_fd, buffer, bytes);
        if (result < 0)
            goto done;
        disk_status_io(DISK_STATUS_WRITE,
                       physical_lba(plan, layout, offset), bytes / 512u,
                       (unsigned int)(offset / 2048u),
                       (unsigned int)transaction->total_sectors);
        offset += bytes;
        transaction->completed_sectors = offset / 2048u;
        if (transaction->completed_sectors >= next_journal ||
            offset == transaction->source_bytes) {
            result = journal_save(transaction);
            if (result < 0)
                goto done;
            next_journal = transaction->completed_sectors +
                           HDL_INSTALL_JOURNAL_INTERVAL_SECTORS;
        }
        if (poll_for_press(&pressed) && (pressed & PAD_TRIANGLE)) {
            result = journal_save(transaction);
            if (result == 0)
                result = HDL_INSTALL_CANCELLED;
            goto done;
        }
    }
done:
    free(buffer);
    return result;
}

static int verify_payload(const hdl_transaction_t *transaction,
                          const hdl_partition_plan_t *plan,
                          const hdl_stream_layout_t *layout,
                          int source_fd, int target_fd)
{
    unsigned char *source_buffer;
    unsigned char *target_buffer;
    unsigned char source_digest[32];
    unsigned char target_digest[32];
    sha256_context_t source_hash;
    sha256_context_t target_hash;
    uint64_t offset = 0;
    int result = 0;

    source_buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    target_buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (source_buffer == NULL || target_buffer == NULL) {
        result = HDL_INSTALL_MEMORY_FAILED;
        goto done;
    }
    if (fileXioLseek64(source_fd, 0, FIO_SEEK_SET) != 0 ||
        fileXioLseek64(target_fd, 0, FIO_SEEK_SET) != 0) {
        result = HDL_INSTALL_VERIFY_FAILED;
        goto done;
    }
    sha256_init(&source_hash);
    sha256_init(&target_hash);
    while (offset < transaction->source_bytes) {
        uint64_t remaining = transaction->source_bytes - offset;
        unsigned int bytes = remaining > HDL_INSTALL_IO_BYTES ?
                             HDL_INSTALL_IO_BYTES : (unsigned int)remaining;

        if (read_exact_fd(source_fd, source_buffer, bytes) < 0 ||
            read_exact_fd(target_fd, target_buffer, bytes) < 0 ||
            memcmp(source_buffer, target_buffer, bytes) != 0) {
            result = HDL_INSTALL_VERIFY_FAILED;
            goto done;
        }
        sha256_update(&source_hash, source_buffer, bytes);
        sha256_update(&target_hash, target_buffer, bytes);
        disk_status_io(DISK_STATUS_VERIFY,
                       physical_lba(plan, layout, offset), bytes / 512u,
                       (unsigned int)(offset / 2048u),
                       (unsigned int)transaction->total_sectors);
        offset += bytes;
    }
    sha256_final(&source_hash, source_digest);
    sha256_final(&target_hash, target_digest);
    if (memcmp(source_digest, target_digest, sizeof(source_digest)) != 0)
        result = HDL_INSTALL_VERIFY_FAILED;
done:
    free(source_buffer);
    free(target_buffer);
    return result;
}

static int execute_transaction(hdl_transaction_t *transaction)
{
    hdl_partition_plan_t plan;
    hdl_stream_layout_t layout;
    hdl_metadata_options_t metadata_options;
    unsigned char metadata[HDL_METADATA_SIZE];
    hdl_file_source_t source;
    unsigned char fingerprint[32];
    uint32_t maximum;
    uint32_t free_sectors;
    int target_fd = -1;
    int verified_this_run = 0;
    int result;

    result = recheck_disk(&maximum, &free_sectors);
    if (result < 0)
        return result;
    result = hdl_partition_plan(transaction->source_bytes, maximum, &plan);
    if (result < 0)
        return result;
    if (transaction->partition_count != plan.count)
        return HDL_INSTALL_LAYOUT_MISMATCH;
    if (plan.allocation_bytes / 512u > free_sectors &&
        transaction->stage == HDL_TRANSACTION_STAGE_PLANNED)
        return HDL_INSTALL_NO_SPACE;
    result = open_source(transaction->source_path,
                         transaction->source_bytes, &source);
    if (result < 0)
        return result;
    result = source_fingerprint(&source, fingerprint);
    if (result < 0 || memcmp(fingerprint, transaction->source_fingerprint,
                             sizeof(fingerprint)) != 0) {
        fileXioClose(source.fd);
        return HDL_INSTALL_SOURCE_CHANGED;
    }

    pad_activity_begin();
    disk_status_begin_at("HDL game installation",
                         "Preparing guarded APA allocation",
                         "Standard HDL main/sub partitions");
    if (transaction->stage == HDL_TRANSACTION_STAGE_PLANNED) {
        result = create_partitions(transaction, &plan);
        if (result < 0)
            goto done;
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_PARTITIONS_CREATED, 0);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    target_fd = open_target(transaction, &plan, &layout);
    if (target_fd < 0) {
        result = target_fd;
        goto done;
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_PARTITIONS_CREATED) {
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_COPYING, 0);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING) {
        disk_status_phase_at("Copying ISO payload from mass:",
                             "HDL payload data area");
        result = copy_payload(transaction, &plan, &layout,
                              source.fd, target_fd);
        if (result < 0)
            goto done;
        if (fileXioIoctl2(target_fd, HDL_STREAM_IOCTL2_FLUSH,
                          NULL, 0, NULL, 0) < 0) {
            result = HDL_INSTALL_COPY_FAILED;
            goto done;
        }
        disk_status_phase_at("Verifying every installed byte",
                             "HDL payload read-back against source ISO");
        result = verify_payload(transaction, &plan, &layout,
                                source.fd, target_fd);
        if (result < 0)
            goto done;
        verified_this_run = 1;
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED,
            transaction->total_sectors);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
        if (!verified_this_run) {
            disk_status_phase_at("Re-verifying resumed payload",
                                 "HDL payload read-back against source ISO");
            result = verify_payload(transaction, &plan, &layout,
                                    source.fd, target_fd);
            if (result < 0)
                goto done;
        }
        metadata_options.game_title = transaction->game_title;
        metadata_options.startup = transaction->startup;
        metadata_options.disc_type = transaction->disc_type;
        metadata_options.layer1_start = transaction->layer1_start;
        metadata_options.hdl_compat_flags = transaction->hdl_compat_flags;
        metadata_options.opl_compat_flags = transaction->opl_compat_flags;
        metadata_options.dma_type = transaction->dma_type;
        metadata_options.dma_mode = transaction->dma_mode;
        result = hdl_metadata_build(&plan, layout.starts, layout.count,
                                    &metadata_options, metadata);
        if (result < 0)
            goto done;
        disk_status_phase_at("Committing verified game metadata last",
                             "Main partition attribute area / HDL metadata");
        result = fileXioIoctl2(target_fd,
                               HDL_STREAM_IOCTL2_COMMIT_METADATA,
                               metadata, sizeof(metadata), NULL, 0);
        if (result < 0) {
            result = HDL_INSTALL_METADATA_FAILED;
            goto done;
        }
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_METADATA_COMMITTED,
            transaction->total_sectors);
        if (result < 0 || journal_save(transaction) < 0) {
            result = HDL_INSTALL_JOURNAL_INVALID;
            goto done;
        }
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_METADATA_COMMITTED) {
        metadata_options.game_title = transaction->game_title;
        metadata_options.startup = transaction->startup;
        metadata_options.disc_type = transaction->disc_type;
        metadata_options.layer1_start = transaction->layer1_start;
        metadata_options.hdl_compat_flags = transaction->hdl_compat_flags;
        metadata_options.opl_compat_flags = transaction->opl_compat_flags;
        metadata_options.dma_type = transaction->dma_type;
        metadata_options.dma_mode = transaction->dma_mode;
        result = hdl_metadata_build(&plan, layout.starts, layout.count,
                                    &metadata_options, metadata);
        if (result < 0 ||
            !target_metadata_matches(transaction->target, metadata)) {
            result = HDL_INSTALL_METADATA_FAILED;
            goto done;
        }
        result = hdl_transaction_set_stage(
            transaction, HDL_TRANSACTION_STAGE_COMPLETE,
            transaction->total_sectors);
        if (result < 0)
            goto done;
        journal_remove();
    }
    result = 0;
done:
    if (target_fd >= 0)
        fileXioClose(target_fd);
    fileXioClose(source.fd);
    disk_status_end();
    pad_activity_end();
    session_log_line("HDL transaction target=%s stage=%u progress=%llu/%llu result=%d",
                     transaction->target, (unsigned int)transaction->stage,
                     (unsigned long long)transaction->completed_sectors,
                     (unsigned long long)transaction->total_sectors, result);
    return result;
}

static int choose_media_type(uint32_t suggested)
{
    static const app_ui_menu_item_t items[] = {
        {"PS2 CD", "Store disc type 0x12", 1},
        {"PS2 DVD", "Store disc type 0x14", 1}
    };
    unsigned int selected = suggested == 0x14 ? 1u : 0u;
    int choice = app_ui_menu_select(
        "Ambiguous media type",
        "ISO9660 cannot distinguish a small DVD from a CD",
        items, 2, &selected);

    return choice < 0 ? -1 : (choice == 0 ? 0x12 : 0x14);
}

static void show_result(const hdl_transaction_t *transaction, int result)
{
    scr_clear();
    if (result == 0) {
        scr_printf("HDL installation complete.\n\n");
        scr_printf("Game  : %s\n", transaction->game_title);
        scr_printf("Disc  : %s\n", transaction->startup);
        scr_printf("Target: %s\n\n", transaction->target);
        scr_printf("Payload and metadata were read back successfully.\n");
    } else if (result == HDL_INSTALL_CANCELLED) {
        scr_printf("HDL installation paused.\n\n");
        scr_printf("Progress: %llu / %llu ISO sectors\n",
                   (unsigned long long)transaction->completed_sectors,
                   (unsigned long long)transaction->total_sectors);
        scr_printf("Use Resume incomplete install to continue.\n");
    } else {
        scr_printf("HDL installation stopped (code %d).\n\n", result);
        scr_printf("No game metadata was exposed unless the journal reached\n");
        scr_printf("METADATA_COMMITTED. The journal was preserved for review\n");
        scr_printf("or a guarded resume.\n");
    }
    app_ui_wait_to_return();
}

static void begin_new_install(void)
{
    hdl_file_source_t source;
    hdl_iso_source_t iso_source;
    hdl_iso_info_t info;
    hdl_partition_plan_t plan;
    hdl_transaction_t transaction;
    uint32_t maximum;
    uint32_t free_sectors;
    unsigned int count;
    int selection;
    int result;

    result = scan_mass_images(image_entries, &count);
    if (result < 0) {
        scr_clear();
        scr_printf("No usable .ISO files were found in mass:/.\n");
        scr_printf("Scan code: %d\n", result);
        app_ui_wait_to_return();
        return;
    }
    selection = select_image(image_entries, count);
    if (selection < 0)
        return;
    result = open_source(image_entries[selection].path,
                         image_entries[selection].bytes,
                         &source);
    if (result < 0) {
        show_result(&(hdl_transaction_t){0}, result);
        return;
    }
    iso_source.read = source_read;
    iso_source.context = &source;
    iso_source.image_bytes = source.bytes;
    result = hdl_iso_probe(&iso_source, &info);
    if (result < 0) {
        fileXioClose(source.fd);
        show_result(&(hdl_transaction_t){0}, result);
        return;
    }
    if (info.requires_layer_break) {
        fileXioClose(source.fd);
        scr_clear();
        scr_printf("DVD9 installation is not enabled yet.\n\n");
        scr_printf("The image is valid, but its layer break has not been\n");
        scr_printf("proven. Guessing would create plausible-looking garbage.\n");
        app_ui_wait_to_return();
        return;
    }

    memset(&transaction, 0, sizeof(transaction));
    transaction.stage = HDL_TRANSACTION_STAGE_PLANNED;
    transaction.source_bytes = info.image_bytes;
    transaction.total_sectors = info.image_sectors;
    snprintf(transaction.source_path, sizeof(transaction.source_path), "%s",
             image_entries[selection].path);
    snprintf(transaction.startup, sizeof(transaction.startup), "%s",
             info.startup);
    snprintf(transaction.game_title, sizeof(transaction.game_title), "%s",
             info.volume_title[0] != '\0' ? info.volume_title : info.disc_id);
    transaction.disc_type = info.disc_type;
    if (!info.media_type_confident) {
        result = choose_media_type(info.disc_type);
        if (result < 0) {
            fileXioClose(source.fd);
            return;
        }
        transaction.disc_type = (uint32_t)result;
    }
    result = hdl_partition_id(info.disc_id, transaction.game_title,
                              transaction.target);
    if (result == 0)
        result = source_fingerprint(&source,
                                    transaction.source_fingerprint);
    fileXioClose(source.fd);
    if (result < 0) {
        show_result(&transaction, result);
        return;
    }
    result = recheck_disk(&maximum, &free_sectors);
    if (result == 0)
        result = hdl_partition_plan(transaction.source_bytes, maximum, &plan);
    if (result == 0 && plan.allocation_bytes / 512u > free_sectors)
        result = HDL_INSTALL_NO_SPACE;
    if (result == 0) {
        transaction.partition_count = plan.count;
        if (target_exists(transaction.target))
            result = HDL_INSTALL_TARGET_EXISTS;
    }
    if (result < 0) {
        show_result(&transaction, result);
        return;
    }

    scr_clear();
    scr_printf("Install PS2 ISO as an HDL game\n\n");
    scr_printf("Game       : %s\n", transaction.game_title);
    scr_printf("Startup    : %s\n", transaction.startup);
    scr_printf("Source     : %llu MiB\n",
               (unsigned long long)(transaction.source_bytes / 1024u / 1024u));
    scr_printf("Allocation : %llu MiB in %u partition%s\n",
               (unsigned long long)(plan.allocation_bytes / 1024u / 1024u),
               plan.count, plan.count == 1 ? "" : "s");
    scr_printf("Target     : %s\n\n", transaction.target);
    scr_printf("Hold L1+R1 and press X to allocate and install.\n");
    scr_printf("TRIANGLE cancels without changing the HDD.\n");
    if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_CROSS))
        return;
    result = journal_save(&transaction);
    if (result == 0)
        result = execute_transaction(&transaction);
    show_result(&transaction, result);
}

static void incomplete_menu(hdl_transaction_t *transaction)
{
    static const app_ui_menu_item_t items[] = {
        {"Resume incomplete install", "Recheck source, disk and exact target layout", 1},
        {"Remove incomplete allocation", "Only the journal target; valid games are refused", 1},
        {"Keep it for later", "Return without changing journal or HDD", 1}
    };
    unsigned int selected = 0;
    char status[160];
    int choice;
    int result;

    snprintf(status, sizeof(status), "%s | stage %u | %llu/%llu sectors",
             transaction->target, (unsigned int)transaction->stage,
             (unsigned long long)transaction->completed_sectors,
             (unsigned long long)transaction->total_sectors);
    choice = app_ui_menu_select("Incomplete HDL transaction", status,
                                items, 3, &selected);
    if (choice == 0) {
        result = execute_transaction(transaction);
        show_result(transaction, result);
    }
    if (choice == 1) {
        scr_clear();
        scr_printf("Remove incomplete HDL allocation\n\n");
        scr_printf("Target: %s\n\n", transaction->target);
        scr_printf("A valid installed game will be refused.\n");
        scr_printf("Hold L1+R1 and press SQUARE to remove.\n");
        if (!wait_for_chord(PAD_L1 | PAD_R1 | PAD_SQUARE))
            return;
        result = remove_incomplete_target(transaction->target);
        if (result >= 0) {
            journal_remove();
            result = 0;
        }
        show_result(transaction, result);
    }
}

void hdl_installer_menu(void)
{
    hdl_transaction_t transaction;
    int journal_result = journal_load(&transaction);

    if (journal_result == 0 &&
        transaction.stage != HDL_TRANSACTION_STAGE_COMPLETE) {
        incomplete_menu(&transaction);
        return;
    }
    if (path_exists(HDL_INSTALL_JOURNAL) ||
        path_exists(HDL_INSTALL_JOURNAL_NEW)) {
        scr_clear();
        scr_printf("The HDL transaction journal is corrupt.\n\n");
        scr_printf("No HDD target can be trusted from this record, so the\n");
        scr_printf("manager will not delete or modify any partition.\n\n");
        scr_printf("Remove HDLINSTALL.TXN/NEW manually after inspection.\n");
        app_ui_wait_to_return();
        return;
    }
    begin_new_install();
}
