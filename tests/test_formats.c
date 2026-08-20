/* Host-side test vectors for code that is also linked into the PS2 ELF. */

#include "capsule_format.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

static int test_sha256(void)
{
    static const char empty_expected[] =
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855";
    sha256_context_t context;
    unsigned char digest[32];
    char hex[65];

    sha256_buffer("", 0, digest);
    sha256_hex(digest, hex);
    if (strcmp(hex, empty_expected) != 0)
        return 0;

    /* Exercise the streaming path across multiple update boundaries. */
    sha256_init(&context);
    sha256_update(&context, "a", 1);
    sha256_update(&context, "b", 1);
    sha256_update(&context, "c", 1);
    sha256_final(&context, digest);
    sha256_hex(digest, hex);
    if (strcmp(hex,
               "ba7816bf8f01cfea414140de5dae2223"
               "b00361a396177a9cb410ff61f20015ad") != 0)
        return 0;

    sha256_buffer("abc", 3, digest);
    sha256_hex(digest, hex);
    if (strcmp(hex,
               "ba7816bf8f01cfea414140de5dae2223"
               "b00361a396177a9cb410ff61f20015ad") != 0)
        return 0;

    /* Exercise the direct full-block path used for HDD rescue payloads. */
    {
        unsigned char block[128];

        memset(block, 'a', sizeof(block));
        sha256_buffer(block, 64, digest);
        sha256_hex(digest, hex);
        if (strcmp(hex,
                   "ffe054fe7ae0cb6dc65c3af9b61d5209"
                   "f439851db43d0ba5997337df154668eb") != 0)
            return 0;

        /* Cross a block boundary after beginning with a partial update. */
        sha256_init(&context);
        sha256_update(&context, block, 13);
        sha256_update(&context, block + 13, sizeof(block) - 13);
        sha256_final(&context, digest);
        sha256_hex(digest, hex);
        if (strcmp(hex,
                   "6836cf13bac400e9105071cd6af47084d"
                   "facad4e5e302c94bfed24e013afb73e") != 0)
            return 0;
    }

    sha256_buffer("abcdbcdecdefdefgefghfghighijhijk"
                  "ijkljklmklmnlmnomnopnopq", 56, digest);
    sha256_hex(digest, hex);
    return strcmp(hex,
                  "248d6a61d20638b8e5c026930c3e6039"
                  "a33ce45964ff2167f6ecedd419db06c1") == 0;
}

static int test_capsule_round_trip(void)
{
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE];
    size_t complete_size;

    memset(&source, 0, sizeof(source));
    source.flags = RESCUE_CAPSULE_FLAG_VALID_APA |
                   RESCUE_CAPSULE_FLAG_HAS_PAYLOAD |
                   RESCUE_CAPSULE_FLAG_VALID_KELF;
    source.payload_start = 0x2000;
    source.payload_sectors = 4;
    source.payload_bytes = 2048;
    source.kelf_file_bytes = 1800;
    memcpy(source.romver, "0220JC20060905", 14);
    memcpy(source.family, "PSBBN / OSDMenu MBR", 20);
    memcpy(source.confidence, "high", 4);
    memset(source.apa_sha256, 0x11, sizeof(source.apa_sha256));
    memset(source.payload_sha256, 0x22, sizeof(source.payload_sha256));

    rescue_capsule_encode(metadata, &source);
    complete_size = RESCUE_CAPSULE_METADATA_SIZE +
                    RESCUE_CAPSULE_APA_HEADER_SIZE + source.payload_bytes;
    if (rescue_capsule_decode(metadata, complete_size, &decoded) != 0)
        return 0;
    return decoded.flags == source.flags &&
           decoded.payload_start == source.payload_start &&
           decoded.payload_sectors == source.payload_sectors &&
           decoded.payload_bytes == source.payload_bytes &&
           decoded.kelf_file_bytes == source.kelf_file_bytes &&
           memcmp(decoded.apa_sha256, source.apa_sha256, 32) == 0 &&
           memcmp(decoded.payload_sha256, source.payload_sha256, 32) == 0 &&
           strcmp(decoded.family, source.family) == 0;
}

static int test_capsule_rejects_wrong_size(void)
{
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE];
    size_t complete_size;

    memset(&source, 0, sizeof(source));
    source.flags = RESCUE_CAPSULE_FLAG_VALID_APA;
    rescue_capsule_encode(metadata, &source);
    complete_size = RESCUE_CAPSULE_METADATA_SIZE +
                    RESCUE_CAPSULE_APA_HEADER_SIZE;
    return rescue_capsule_decode(metadata, complete_size - 1, &decoded) < 0;
}

static int test_capsule_rejects_impossible_flags(void)
{
    rescue_capsule_info_t source;
    rescue_capsule_info_t decoded;
    unsigned char metadata[RESCUE_CAPSULE_METADATA_SIZE];
    size_t complete_size = RESCUE_CAPSULE_METADATA_SIZE +
                           RESCUE_CAPSULE_APA_HEADER_SIZE;

    memset(&source, 0, sizeof(source));
    source.flags = RESCUE_CAPSULE_FLAG_VALID_APA |
                   RESCUE_CAPSULE_FLAG_VALID_KELF;
    rescue_capsule_encode(metadata, &source);
    if (rescue_capsule_decode(metadata, complete_size, &decoded) >= 0)
        return 0;

    source.flags = RESCUE_CAPSULE_FLAG_VALID_APA;
    rescue_capsule_encode(metadata, &source);
    metadata[23] = 0x80; /* Set an unknown high flag bit in little endian. */
    return rescue_capsule_decode(metadata, complete_size, &decoded) < 0;
}

int main(void)
{
    if (!test_sha256()) {
        fprintf(stderr, "SHA-256 test vector failed.\n");
        return 1;
    }
    if (!test_capsule_round_trip()) {
        fprintf(stderr, "Capsule round-trip failed.\n");
        return 2;
    }
    if (!test_capsule_rejects_wrong_size()) {
        fprintf(stderr, "Capsule size validation failed.\n");
        return 3;
    }
    if (!test_capsule_rejects_impossible_flags()) {
        fprintf(stderr, "Capsule flag validation failed.\n");
        return 4;
    }
    puts("All host-side format tests passed.");
    return 0;
}
