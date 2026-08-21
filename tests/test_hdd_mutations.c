/*
 * Host-side mutation model for the two disk effects used by the manager:
 * writing the bootstrap payload inside __mbr and changing osdStart/osdSize.
 *
 * This deliberately does not pretend to emulate ps2hdd, DEV9, DMA, cache
 * flushes, or journaling. It verifies the byte-level postconditions and write
 * ordering that are portable: payload writes stay away from sector zero,
 * touched sectors are zero-padded like hdd_write.c, pointer changes preserve
 * the rest of the APA header, and the APA checksum is recomputed.
 */

#include "apa.h"
#include "hdd_limits.h"
#include "kelf.h"

#include <stdio.h>
#include <string.h>

#define TEST_BOOT_INDICATOR_OFFSET 446u
#define TEST_SENTINEL_SECTOR (HDD_MBR_PAYLOAD_START + 2u)
#define TEST_KELF_BYTES 700u

static void write_le16_test(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32_test(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static int read_bytes(FILE *disk, long offset, unsigned char *data, size_t size)
{
    if (fseek(disk, offset, SEEK_SET) != 0)
        return -1;
    return fread(data, 1, size, disk) == size ? 0 : -2;
}

static int write_bytes(FILE *disk, long offset,
                       const unsigned char *data, size_t size)
{
    if (fseek(disk, offset, SEEK_SET) != 0)
        return -1;
    if (fwrite(data, 1, size, disk) != size)
        return -2;
    return fflush(disk) == 0 ? 0 : -3;
}

static int seed_header_from_fixture(FILE *disk, const char *directory,
                                    const char *name)
{
    char path[512];
    unsigned char header[APA_HEADER_SIZE];
    FILE *source;

    snprintf(path, sizeof(path), "%s/%s.raw", directory, name);
    source = fopen(path, "rb");
    if (source == NULL)
        return -1;
    if (fread(header, 1, sizeof(header), source) != sizeof(header)) {
        fclose(source);
        return -2;
    }
    fclose(source);
    return write_bytes(disk, 0, header, sizeof(header));
}

static int seed_payload_from_fixture(FILE *disk, const char *directory,
                                     const char *name)
{
    char path[512];
    unsigned char sector[HDD_SECTOR_SIZE];
    FILE *source;

    snprintf(path, sizeof(path), "%s/%s.raw", directory, name);
    source = fopen(path, "rb");
    if (source == NULL)
        return -1;
    if (fseek(source, (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
              SEEK_SET) != 0 ||
        fread(sector, 1, sizeof(sector), source) != sizeof(sector)) {
        fclose(source);
        return -2;
    }
    fclose(source);
    return write_bytes(disk,
                       (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
                       sector, sizeof(sector));
}

/* Byte-level model of the successful HDIOC_SETOSDMBR postcondition. */
static int model_set_osd_mbr(FILE *disk, unsigned int start, unsigned int size)
{
    unsigned char header[APA_HEADER_SIZE];

    if (read_bytes(disk, 0, header, sizeof(header)) < 0)
        return -1;
    write_le32_test(header + APA_OSD_START_OFFSET, start);
    write_le32_test(header + APA_OSD_SIZE_OFFSET, size);
    write_le32_test(header, apa_checksum(header));
    return write_bytes(disk, 0, header, sizeof(header));
}

/* Byte-level model of hdd_write_payload_verified()'s write phase. */
static int model_write_payload(FILE *disk, const unsigned char *payload,
                               unsigned int payload_size,
                               unsigned int start_sector)
{
    unsigned char packet[HDD_TRANSFER_BYTES];
    unsigned int offset = 0;
    unsigned int sector_offset = 0;

    while (offset < payload_size) {
        unsigned int remaining = payload_size - offset;
        unsigned int bytes = remaining > HDD_TRANSFER_BYTES
                                 ? HDD_TRANSFER_BYTES : remaining;
        unsigned int sectors =
            (bytes + HDD_SECTOR_SIZE - 1u) / HDD_SECTOR_SIZE;

        memset(packet, 0, sizeof(packet));
        memcpy(packet, payload + offset, bytes);
        if (write_bytes(disk,
                        (long)(start_sector + sector_offset) * HDD_SECTOR_SIZE,
                        packet, sectors * HDD_SECTOR_SIZE) < 0)
            return -1;
        offset += bytes;
        sector_offset += sectors;
    }
    return 0;
}

static void make_two_sector_kelf(unsigned char payload[TEST_KELF_BYTES])
{
    const unsigned int header_size = 72u;
    const unsigned int elf_size = TEST_KELF_BYTES - header_size;
    unsigned int i;

    memset(payload, 0, TEST_KELF_BYTES);
    memcpy(payload, "KELFTEST", 8);
    write_le32_test(payload + 0x10, elf_size);
    write_le16_test(payload + 0x14, header_size);
    write_le16_test(payload + 0x18, 0);
    write_le16_test(payload + 0x1a, 0);
    for (i = header_size; i < TEST_KELF_BYTES; i++)
        payload[i] = (unsigned char)(i * 37u + 11u);
}

static int header_diff_is_only_pointer_and_checksum(
    const unsigned char before[APA_HEADER_SIZE],
    const unsigned char after[APA_HEADER_SIZE])
{
    unsigned int i;

    for (i = 0; i < APA_HEADER_SIZE; i++) {
        if (i < 4u ||
            (i >= APA_OSD_START_OFFSET && i < APA_OSD_SIZE_OFFSET + 4u))
            continue;
        if (before[i] != after[i])
            return 0;
    }
    return 1;
}

static int test_disable_pointer_only(const char *directory)
{
    unsigned char before[APA_HEADER_SIZE];
    unsigned char after[APA_HEADER_SIZE];
    unsigned char payload_before[HDD_SECTOR_SIZE];
    unsigned char payload_after[HDD_SECTOR_SIZE];
    FILE *disk = tmpfile();
    int ok = 0;

    if (disk == NULL)
        return 0;
    if (seed_header_from_fixture(disk, directory, "valid_enabled") < 0 ||
        seed_payload_from_fixture(disk, directory, "valid_enabled") < 0)
        goto done;

    /* A PC-style boot-indicator byte is unrelated to PS2 HDD bootability.
       Preserve it to prove disable only clears the APA OSD pointer fields. */
    if (read_bytes(disk, 0, before, sizeof(before)) < 0)
        goto done;
    before[TEST_BOOT_INDICATOR_OFFSET] = 0x80;
    write_le32_test(before, apa_checksum(before));
    if (write_bytes(disk, 0, before, sizeof(before)) < 0 ||
        read_bytes(disk,
                   (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
                   payload_before, sizeof(payload_before)) < 0)
        goto done;

    if (!is_standard_apa_header(before) || is_hybrid_gpt(before))
        goto done;
    if (model_set_osd_mbr(disk, 0, 0) < 0 ||
        read_bytes(disk, 0, after, sizeof(after)) < 0 ||
        read_bytes(disk,
                   (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
                   payload_after, sizeof(payload_after)) < 0)
        goto done;

    if (!is_standard_apa_header(after) ||
        read_le32(after + APA_OSD_START_OFFSET) != 0 ||
        read_le32(after + APA_OSD_SIZE_OFFSET) != 0 ||
        read_le32(after) != apa_checksum(after) ||
        !headers_match_same_disk(before, after) ||
        !header_diff_is_only_pointer_and_checksum(before, after) ||
        after[TEST_BOOT_INDICATOR_OFFSET] != 0x80 ||
        memcmp(payload_before, payload_after, sizeof(payload_before)) != 0)
        goto done;

    ok = 1;
done:
    fclose(disk);
    return ok;
}

static int test_payload_overwrite_pointer_last(const char *directory)
{
    unsigned char header_before[APA_HEADER_SIZE];
    unsigned char header_after_write[APA_HEADER_SIZE];
    unsigned char header_after_enable[APA_HEADER_SIZE];
    unsigned char payload[TEST_KELF_BYTES];
    unsigned char written[HDD_TRANSFER_BYTES];
    unsigned char sentinel[HDD_SECTOR_SIZE];
    unsigned char sentinel_after[HDD_SECTOR_SIZE];
    unsigned int kelf_bytes = 0;
    FILE *disk = tmpfile();
    int ok = 0;

    if (disk == NULL)
        return 0;
    if (seed_header_from_fixture(disk, directory, "valid_disabled") < 0)
        goto done;

    memset(written, 0xa5, sizeof(written));
    if (write_bytes(disk,
                    (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
                    written, sizeof(written)) < 0)
        goto done;
    memset(sentinel, 0x5a, sizeof(sentinel));
    if (write_bytes(disk, (long)TEST_SENTINEL_SECTOR * HDD_SECTOR_SIZE,
                    sentinel, sizeof(sentinel)) < 0 ||
        read_bytes(disk, 0, header_before, sizeof(header_before)) < 0)
        goto done;

    make_two_sector_kelf(payload);
    if (model_write_payload(disk, payload, sizeof(payload),
                            HDD_MBR_PAYLOAD_START) < 0 ||
        read_bytes(disk, 0, header_after_write, sizeof(header_after_write)) < 0 ||
        read_bytes(disk,
                   (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
                   written, sizeof(written)) < 0 ||
        read_bytes(disk, (long)TEST_SENTINEL_SECTOR * HDD_SECTOR_SIZE,
                   sentinel_after, sizeof(sentinel_after)) < 0)
        goto done;

    /* Payload-first: sector zero and the OSD pointer are still untouched. */
    if (memcmp(header_before, header_after_write, APA_HEADER_SIZE) != 0 ||
        read_le32(header_after_write + APA_OSD_START_OFFSET) != 0 ||
        read_le32(header_after_write + APA_OSD_SIZE_OFFSET) != 0 ||
        memcmp(written, payload, sizeof(payload)) != 0)
        goto done;
    {
        unsigned int i;
        for (i = sizeof(payload); i < sizeof(written); i++) {
            if (written[i] != 0)
                goto done;
        }
    }
    if (memcmp(sentinel, sentinel_after, sizeof(sentinel)) != 0 ||
        kelf_size_from_disk_image(written, sizeof(written), &kelf_bytes) !=
            KELF_IMAGE_VALID ||
        kelf_bytes != sizeof(payload))
        goto done;

    /* Pointer-last activation models the successful ps2hdd postcondition. */
    if (model_set_osd_mbr(disk, HDD_MBR_PAYLOAD_START, 2) < 0 ||
        read_bytes(disk, 0, header_after_enable, sizeof(header_after_enable)) < 0)
        goto done;
    if (!is_standard_apa_header(header_after_enable) ||
        read_le32(header_after_enable + APA_OSD_START_OFFSET) !=
            HDD_MBR_PAYLOAD_START ||
        read_le32(header_after_enable + APA_OSD_SIZE_OFFSET) != 2 ||
        !headers_match_same_disk(header_before, header_after_enable) ||
        !header_diff_is_only_pointer_and_checksum(header_before,
                                                  header_after_enable))
        goto done;

    /* Disabling again must clear only the pointer and leave the MBR program. */
    if (model_set_osd_mbr(disk, 0, 0) < 0 ||
        read_bytes(disk, 0, header_after_enable, sizeof(header_after_enable)) < 0 ||
        read_bytes(disk,
                   (long)HDD_MBR_PAYLOAD_START * HDD_SECTOR_SIZE,
                   written, sizeof(written)) < 0)
        goto done;
    if (!is_standard_apa_header(header_after_enable) ||
        read_le32(header_after_enable + APA_OSD_START_OFFSET) != 0 ||
        read_le32(header_after_enable + APA_OSD_SIZE_OFFSET) != 0 ||
        memcmp(written, payload, sizeof(payload)) != 0)
        goto done;

    ok = 1;
done:
    fclose(disk);
    return ok;
}

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : "tests/generated_hdds";

    if (!test_disable_pointer_only(directory)) {
        fprintf(stderr, "Disable-pointer mutation regression failed.\n");
        return 1;
    }
    if (!test_payload_overwrite_pointer_last(directory)) {
        fprintf(stderr, "MBR payload overwrite/pointer-last regression failed.\n");
        return 2;
    }

    puts("All synthetic HDD mutation tests passed.");
    return 0;
}
