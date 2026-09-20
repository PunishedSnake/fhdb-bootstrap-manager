#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hdl_iso.h"

#define TEST_SECTORS 48u

typedef struct {
    unsigned char data[TEST_SECTORS * HDL_ISO_SECTOR_SIZE];
    size_t size;
    int fail_reads;
} fake_iso_t;

static void write_le16(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_be16(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)(value >> 8);
    destination[1] = (unsigned char)value;
}

static void write_le32(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void write_be32(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static void directory_record(unsigned char *record, unsigned int extent,
                             unsigned int bytes, const unsigned char *name,
                             unsigned int name_length)
{
    unsigned int length = 33 + name_length + (name_length % 2 == 0 ? 1 : 0);

    memset(record, 0, length);
    record[0] = (unsigned char)length;
    write_le32(record + 2, extent);
    write_be32(record + 6, extent);
    write_le32(record + 10, bytes);
    write_be32(record + 14, bytes);
    record[25] = 0;
    write_le16(record + 28, 1);
    write_be16(record + 30, 1);
    record[32] = (unsigned char)name_length;
    memcpy(record + 33, name, name_length);
}

static fake_iso_t valid_iso(const char *boot_line)
{
    fake_iso_t iso;
    unsigned char *pvd;
    unsigned char *root;
    unsigned char root_name = 0;
    static const unsigned char cnf_name[] = "SYSTEM.CNF;1";
    size_t boot_length = strlen(boot_line);

    memset(&iso, 0, sizeof(iso));
    iso.size = sizeof(iso.data);
    pvd = iso.data + 16 * HDL_ISO_SECTOR_SIZE;
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    memset(pvd + 40, ' ', 32);
    memcpy(pvd + 40, "TEST GAME", 9);
    write_le32(pvd + 80, TEST_SECTORS);
    write_be32(pvd + 84, TEST_SECTORS);
    write_le16(pvd + 128, HDL_ISO_SECTOR_SIZE);
    write_be16(pvd + 130, HDL_ISO_SECTOR_SIZE);
    directory_record(pvd + 156, 20, HDL_ISO_SECTOR_SIZE, &root_name, 1);

    root = iso.data + 20 * HDL_ISO_SECTOR_SIZE;
    directory_record(root, 20, HDL_ISO_SECTOR_SIZE, &root_name, 1);
    directory_record(root + root[0], 21, (unsigned int)boot_length,
                     cnf_name, sizeof(cnf_name) - 1);
    memcpy(iso.data + 21 * HDL_ISO_SECTOR_SIZE, boot_line, boot_length);
    return iso;
}

static int fake_read(void *context, uint64_t offset, void *destination,
                     size_t size)
{
    fake_iso_t *iso = context;

    if (iso->fail_reads || offset > iso->size || size > iso->size - offset)
        return -1;
    memcpy(destination, iso->data + (size_t)offset, size);
    return 0;
}

static hdl_iso_source_t source_for(fake_iso_t *iso)
{
    hdl_iso_source_t source = {fake_read, iso, iso->size};
    return source;
}

static void test_valid_ps2_iso(void)
{
    fake_iso_t iso = valid_iso("BOOT2 = cdrom0:\\slus_123.45;1\r\nVER = 1.00\r\n");
    hdl_iso_source_t source = source_for(&iso);
    hdl_iso_info_t info;

    assert(hdl_iso_probe(&source, &info) == 0);
    assert(strcmp(info.startup, "SLUS_123.45") == 0);
    assert(strcmp(info.disc_id, "SLUS-12345") == 0);
    assert(strcmp(info.volume_title, "TEST GAME") == 0);
    assert(info.image_sectors == TEST_SECTORS);
    assert(info.image_bytes == sizeof(iso.data));
    assert(info.disc_type == 0x12);
    assert(info.media_type_confident == 0);
    assert(info.requires_layer_break == 0);
}

static void test_reader_and_pvd_fail_closed(void)
{
    fake_iso_t iso = valid_iso("BOOT2=cdrom0:\\SLUS_123.45;1\n");
    hdl_iso_source_t source = source_for(&iso);
    hdl_iso_info_t info;

    iso.fail_reads = 1;
    assert(hdl_iso_probe(&source, &info) == HDL_ISO_READ_FAILED);
    iso.fail_reads = 0;
    iso.data[16 * HDL_ISO_SECTOR_SIZE + 1] = 'X';
    assert(hdl_iso_probe(&source, &info) == HDL_ISO_INVALID_PVD);
}

static void test_missing_and_invalid_system_cnf(void)
{
    fake_iso_t iso = valid_iso("BOOT2=cdrom0:\\SLUS_123.45;1\n");
    hdl_iso_source_t source = source_for(&iso);
    hdl_iso_info_t info;
    unsigned char *root = iso.data + 20 * HDL_ISO_SECTOR_SIZE;

    root[root[0] + 33] = 'X';
    assert(hdl_iso_probe(&source, &info) == HDL_ISO_SYSTEM_CNF_MISSING);

    iso = valid_iso("BOOT2=cdrom0:\\README.TXT;1\n");
    source = source_for(&iso);
    assert(hdl_iso_probe(&source, &info) == HDL_ISO_STARTUP_INVALID);

    iso = valid_iso("VER=1.00\nVMODE=NTSC\n");
    source = source_for(&iso);
    assert(hdl_iso_probe(&source, &info) == HDL_ISO_SYSTEM_CNF_INVALID);
}

static void test_rejects_partial_sector_image(void)
{
    fake_iso_t iso = valid_iso("BOOT2=cdrom0:\\SLUS_123.45;1\n");
    hdl_iso_source_t source = source_for(&iso);
    hdl_iso_info_t info;

    source.image_bytes--;
    assert(hdl_iso_probe(&source, &info) == HDL_ISO_IMAGE_SIZE_INVALID);
}

int main(void)
{
    test_valid_ps2_iso();
    test_reader_and_pvd_fail_closed();
    test_missing_and_invalid_system_cnf();
    test_rejects_partial_sector_image();
    puts("All HDL ISO inspection tests passed.");
    return 0;
}
