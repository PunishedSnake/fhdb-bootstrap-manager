/* Explicit PS2-only raw writer for narrowly planned APA master repairs. */

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include <string.h>

#include "apa.h"
#include "hdd_read.h"
#include "hdd_repair_ps2.h"

#define HDIOC_WRITESECTOR_LOCAL 0x6837
#define HDIOC_FLUSH_LOCAL 0x4804

typedef struct {
    unsigned int lba;
    unsigned int size;
    unsigned char data[APA_HEADER_SIZE];
} repair_write_packet_t;

static repair_write_packet_t repair_packet __attribute__((aligned(64)));
static unsigned char repair_verify[APA_HEADER_SIZE]
    __attribute__((aligned(64)));

int hdd_repair_write_master_header_verified(
    const unsigned char repaired[APA_HEADER_SIZE],
    unsigned char readback[APA_HEADER_SIZE])
{
    int result;

    if (repaired == NULL || readback == NULL)
        return HDD_REPAIR_INVALID_ARGUMENT;

    /* Never use this raw path to erase a GPT/protective-MBR signal or to write
       a header that does not already satisfy our complete repaired contract. */
    if (!is_standard_apa_header(repaired) || is_hybrid_gpt(repaired) ||
        read_le32(repaired + APA_START_OFFSET) != 0 ||
        read_le16(repaired + APA_TYPE_OFFSET) != APA_MASTER_TYPE_VALUE ||
        read_le32(repaired + APA_MBR_VERSION_OFFSET) !=
            APA_MASTER_VERSION_VALUE)
        return HDD_REPAIR_UNSAFE_HEADER;

    repair_packet.lba = 0;
    repair_packet.size = 2;
    memcpy(repair_packet.data, repaired, APA_HEADER_SIZE);
    result = fileXioDevctl("hdd0:", HDIOC_WRITESECTOR_LOCAL,
                           &repair_packet, sizeof(repair_packet), NULL, 0);
    if (result < 0)
        return HDD_REPAIR_WRITE_FAILED;

    result = fileXioDevctl("hdd0:", HDIOC_FLUSH_LOCAL,
                           NULL, 0, NULL, 0);
    if (result < 0)
        return HDD_REPAIR_FLUSH_FAILED;

    result = hdd_read_raw_sectors(0, 2, repair_verify);
    if (result < 0)
        return HDD_REPAIR_READBACK_FAILED;
    if (memcmp(repair_verify, repaired, APA_HEADER_SIZE) != 0)
        return HDD_REPAIR_COMPARE_FAILED;

    memcpy(readback, repair_verify, APA_HEADER_SIZE);
    return 0;
}
