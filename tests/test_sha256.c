/*
 * SHA-256 conformance for the unrolled R5900 kernel.
 *
 * Expected values are FIPS 180-4 / NIST vectors, and for the length sweep a
 * digest-of-digests computed independently with Python's hashlib:
 *   data[i] = (i * 131 + 7) & 0xff
 *   sha256(sha256(data[:0]) || sha256(data[:1]) || ... || sha256(data[:300]))
 */

#include <stdio.h>
#include <string.h>

#include "sha256.h"

static int failures;

static void to_hex(const unsigned char digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
}

static void expect(const unsigned char digest[32], const char *hex,
                   const char *name)
{
    char actual[65];

    to_hex(digest, actual);
    if (strcmp(actual, hex) != 0) {
        fprintf(stderr, "FAIL %s\n  got  %s\n  want %s\n", name, actual, hex);
        failures++;
    }
}

int main(void)
{
    static unsigned char data[300];
    static unsigned char million[1000];
    unsigned char digest[32];
    unsigned char reference[32];
    sha256_context_t context;
    sha256_context_t sweep;
    unsigned int n;
    unsigned int step;

    sha256_buffer("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                  56, digest);
    expect(digest,
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "NIST 448-bit two-block");
    sha256_buffer("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                  "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
                  112, digest);
    expect(digest,
           "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
           "NIST 896-bit");

    memset(million, 'a', sizeof(million));
    sha256_init(&context);
    for (n = 0; n < 1000; n++)
        sha256_update(&context, million, sizeof(million));
    sha256_final(&context, digest);
    expect(digest,
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
           "one million 'a'");

    for (n = 0; n < sizeof(data); n++)
        data[n] = (unsigned char)((n * 131u + 7u) & 0xffu);
    sha256_init(&sweep);
    for (n = 0; n <= sizeof(data); n++) {
        sha256_buffer(data, n, reference);
        sha256_update(&sweep, reference, sizeof(reference));
    }
    sha256_final(&sweep, digest);
    expect(digest, "7722024365d27836079cbf29de35d060b9383d7c713083199661392f983216d4", "length sweep 0..300");

    /* Every split pattern must equal the one-shot digest. */
    sha256_buffer(data, sizeof(data), reference);
    for (step = 1; step <= 130; step++) {
        unsigned int offset = 0;

        sha256_init(&context);
        while (offset < sizeof(data)) {
            unsigned int chunk = (offset / step) % 2 ? step + 3 : step;

            if (chunk > sizeof(data) - offset)
                chunk = (unsigned int)sizeof(data) - offset;
            sha256_update(&context, data + offset, chunk);
            offset += chunk;
        }
        sha256_final(&context, digest);
        if (memcmp(digest, reference, sizeof(digest)) != 0) {
            fprintf(stderr, "FAIL split update step %u\n", step);
            failures++;
        }
    }

    if (failures != 0)
        return 1;
    puts("All SHA-256 conformance tests passed.");
    return 0;
}
