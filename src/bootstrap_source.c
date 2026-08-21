/* PS2-side preparation of an installable HDD bootstrap source. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <delaythread.h>

#include <stdlib.h>
#include <string.h>

#include "bootstrap_source.h"
#include "hdd_limits.h"
#include "kelf.h"
#include "storage.h"

static void reset_result(bootstrap_source_result_t *result)
{
    if (result == NULL)
        return;
    memset(result, 0, sizeof(*result));
}

static int fail_result(bootstrap_source_result_t *result,
                       bootstrap_source_stage_t stage, int code)
{
    if (result != NULL) {
        result->stage = stage;
        result->code = code;
    }
    return code;
}

void bootstrap_source_init(bootstrap_source_t *source, unsigned int storage)
{
    if (source == NULL)
        return;

    memset(source, 0, sizeof(*source));
    storage_path(source->path, sizeof(source->path), storage, "MBR.XLF");
}

static int load_payload_file(unsigned int storage, bootstrap_source_t *source,
                             bootstrap_source_result_t *result)
{
    int attempts = storage == 2 ? 20 : 1;
    int fd = -1;
    int size;
    int total;
    unsigned char *data;

    while (attempts-- > 0) {
        fd = fileXioOpen(source->path, FIO_O_RDONLY, 0);
        if (fd >= 0)
            break;
        DelayThread(250000);
    }
    if (fd < 0)
        return fail_result(result, BOOTSTRAP_SOURCE_STAGE_LOAD, fd);

    size = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (size <= 0 || (unsigned int)size > HDD_MAX_MBR_PAYLOAD_SIZE) {
        fileXioClose(fd);
        return fail_result(result, BOOTSTRAP_SOURCE_STAGE_LOAD,
                           BOOTSTRAP_SOURCE_SIZE_INVALID);
    }
    if (fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return fail_result(result, BOOTSTRAP_SOURCE_STAGE_LOAD,
                           BOOTSTRAP_SOURCE_SEEK_FAILED);
    }

    data = malloc((unsigned int)size);
    if (data == NULL) {
        fileXioClose(fd);
        return fail_result(result, BOOTSTRAP_SOURCE_STAGE_LOAD,
                           BOOTSTRAP_SOURCE_ALLOC_FAILED);
    }

    total = 0;
    while (total < size) {
        int received = fileXioRead(fd, data + total, size - total);

        if (received <= 0) {
            int code = received < 0 ? received : BOOTSTRAP_SOURCE_SHORT_READ;

            free(data);
            fileXioClose(fd);
            return fail_result(result, BOOTSTRAP_SOURCE_STAGE_LOAD, code);
        }
        total += received;
    }
    fileXioClose(fd);

    source->payload = data;
    source->payload_size = (unsigned int)size;
    source->sectors = (source->payload_size + HDD_SECTOR_SIZE - 1) /
                      HDD_SECTOR_SIZE;
    result->payload_sectors = source->sectors;
    return 0;
}

int bootstrap_source_prepare(unsigned int storage, bootstrap_source_t *source,
                             bootstrap_source_result_t *result)
{
    iox_stat_t mbr_stat;
    int code;

    reset_result(result);
    if (source == NULL || result == NULL || storage >= STORAGE_TARGET_COUNT)
        return fail_result(result, BOOTSTRAP_SOURCE_STAGE_LOAD,
                           BOOTSTRAP_SOURCE_SIZE_INVALID);
    if (source->path[0] == '\0')
        bootstrap_source_init(source, storage);

    code = load_payload_file(storage, source, result);
    if (code < 0)
        return code;

    code = kelf_validate_layout(source->payload, source->payload_size);
    if (code < 0) {
        bootstrap_source_release(source);
        return fail_result(result, BOOTSTRAP_SOURCE_STAGE_KELF, code);
    }

    memset(&mbr_stat, 0, sizeof(mbr_stat));
    code = fileXioGetStat("hdd0:__mbr", &mbr_stat);
    result->getstat_result = code;
    result->mbr_start = (unsigned int)mbr_stat.private_5;
    result->mbr_size = (unsigned int)mbr_stat.size;
    if (code < 0 || mbr_stat.private_5 != 0 ||
        mbr_stat.size <= HDD_MBR_PAYLOAD_START ||
        source->sectors > mbr_stat.size - HDD_MBR_PAYLOAD_START) {
        bootstrap_source_release(source);
        result->stage = BOOTSTRAP_SOURCE_STAGE_CAPACITY;
        result->code = code < 0 ? code : BOOTSTRAP_SOURCE_CAPACITY_INVALID;
        return result->code;
    }

    result->stage = BOOTSTRAP_SOURCE_STAGE_NONE;
    result->code = 0;
    return 0;
}

void bootstrap_source_release(bootstrap_source_t *source)
{
    if (source == NULL)
        return;
    free(source->payload);
    source->payload = NULL;
    source->payload_size = 0;
    source->sectors = 0;
}
