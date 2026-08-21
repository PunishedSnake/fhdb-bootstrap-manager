/* Host-side integration fixtures for payload hashing plus KELF sizing. */

#include "boot_payload.h"
#include "kelf.h"
#include "sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_BYTES 512u
#define TEST_ELF_SIZE_OFFSET 0x10u
#define TEST_HEADER_SIZE_OFFSET 0x14u
#define TEST_FLAGS_OFFSET 0x18u
#define TEST_BIT_COUNT_OFFSET 0x1au

static void write_le16_test(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_le32_test(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static unsigned int make_sector_padded_kelf(unsigned char *data)
{
    const unsigned int header_bytes = 72u;
    const unsigned int elf_bytes = 16u;
    unsigned int file_bytes = header_bytes + elf_bytes;
    unsigned int i;

    memset(data, 0, FIXTURE_BYTES);
    write_le32_test(data + TEST_ELF_SIZE_OFFSET, elf_bytes);
    write_le16_test(data + TEST_HEADER_SIZE_OFFSET, header_bytes);
    write_le16_test(data + TEST_FLAGS_OFFSET, 0u);
    write_le16_test(data + TEST_BIT_COUNT_OFFSET, 0u);
    for (i = KELF_FIXED_HEADER_SIZE; i < file_bytes; i++)
        data[i] = (unsigned char)(i ^ 0x5au);
    memset(data + file_bytes, 0xa5, FIXTURE_BYTES - file_bytes);
    return file_bytes;
}

static int test_valid_payload_fingerprint(void)
{
    boot_chain_info_t info;
    unsigned char data[FIXTURE_BYTES];
    unsigned char expected_payload_hash[32];
    unsigned char expected_kelf_hash[32];
    unsigned int file_bytes = make_sector_padded_kelf(data);

    memset(&info, 0, sizeof(info));
    boot_payload_fingerprint(&info, data, sizeof(data));
    sha256_buffer(data, sizeof(data), expected_payload_hash);
    sha256_buffer(data, file_bytes, expected_kelf_hash);

    return info.payload_bytes == sizeof(data) &&
           info.payload_kelf_result == KELF_IMAGE_VALID &&
           info.kelf_file_bytes == file_bytes &&
           memcmp(info.payload_sha256, expected_payload_hash, 32) == 0 &&
           memcmp(info.kelf_sha256, expected_kelf_hash, 32) == 0;
}

static int test_invalid_kelf_keeps_sector_hash(void)
{
    boot_chain_info_t info;
    unsigned char data[FIXTURE_BYTES];
    unsigned char expected_payload_hash[32];
    unsigned char zero_hash[32];

    memset(data, 0, sizeof(data));
    memset(&info, 0x5a, sizeof(info));
    memset(zero_hash, 0, sizeof(zero_hash));
    boot_payload_fingerprint(&info, data, sizeof(data));
    sha256_buffer(data, sizeof(data), expected_payload_hash);

    return info.payload_bytes == sizeof(data) &&
           info.payload_kelf_result < 0 &&
           info.kelf_file_bytes == 0 &&
           memcmp(info.payload_sha256, expected_payload_hash, 32) == 0 &&
           memcmp(info.kelf_sha256, zero_hash, 32) == 0;
}

static int test_empty_input_resets_payload_fields(void)
{
    boot_chain_info_t info;
    unsigned char zero_hash[32];

    memset(&info, 0x5a, sizeof(info));
    memset(zero_hash, 0, sizeof(zero_hash));
    boot_payload_fingerprint(&info, NULL, 0);

    return info.payload_bytes == 0 &&
           info.kelf_file_bytes == 0 &&
           info.payload_kelf_result == -1 &&
           memcmp(info.payload_sha256, zero_hash, 32) == 0 &&
           memcmp(info.kelf_sha256, zero_hash, 32) == 0;
}

int main(void)
{
    if (!test_valid_payload_fingerprint()) {
        fprintf(stderr, "Valid payload fingerprint fixture failed.\n");
        return 1;
    }
    if (!test_invalid_kelf_keeps_sector_hash()) {
        fprintf(stderr, "Invalid-KELF payload fixture failed.\n");
        return 2;
    }
    if (!test_empty_input_resets_payload_fields()) {
        fprintf(stderr, "Empty payload reset fixture failed.\n");
        return 3;
    }

    puts("All portable boot payload tests passed.");
    return 0;
}
