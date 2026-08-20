/*
 * Compact SHA-256 implementation for rescue-capsule integrity checks.
 *
 * This file intentionally has no PS2-specific dependencies so the exact
 * implementation used by the console can be checked with ordinary host-side
 * test vectors before a release is built.
 */

#include "sha256.h"

#include <string.h>

static const uint32_t round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotate_right(uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static uint32_t read_be32(const unsigned char *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static void write_be32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static void sha256_transform(sha256_context_t *context,
                             const unsigned char block[64])
{
    /*
     * SHA-256 needs only the previous 16 message words to derive the next one.
     * A rolling schedule cuts transform-local storage from 256 to 64 bytes,
     * which is friendlier to the R5900 stack/cache without changing the hash.
     */
    uint32_t schedule[16];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned int i;

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t word;
        uint32_t sum1;
        uint32_t choose;
        uint32_t temporary1;
        uint32_t sum0;
        uint32_t majority;
        uint32_t temporary2;

        if (i < 16) {
            word = read_be32(block + (i * 4));
            schedule[i] = word;
        } else {
            uint32_t w15 = schedule[(i - 15) & 15];
            uint32_t w2 = schedule[(i - 2) & 15];
            uint32_t s0 = rotate_right(w15, 7) ^ rotate_right(w15, 18) ^
                          (w15 >> 3);
            uint32_t s1 = rotate_right(w2, 17) ^ rotate_right(w2, 19) ^
                          (w2 >> 10);

            word = schedule[i & 15] + s0 + schedule[(i - 7) & 15] + s1;
            schedule[i & 15] = word;
        }

        sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        choose = (e & f) ^ ((~e) & g);
        temporary1 = h + sum1 + choose + round_constants[i] + word;
        sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        majority = (a & b) ^ (a & c) ^ (b & c);
        temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void sha256_init(sha256_context_t *context)
{
    static const uint32_t initial_state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    memcpy(context->state, initial_state, sizeof(initial_state));
    context->total_bytes = 0;
    context->block_used = 0;
    memset(context->block, 0, sizeof(context->block));
}

void sha256_update(sha256_context_t *context, const void *data, size_t size)
{
    const unsigned char *source = (const unsigned char *)data;

    context->total_bytes += size;

    /* Finish a partial buffered block first. */
    if (context->block_used != 0 && size != 0) {
        size_t available = sizeof(context->block) - context->block_used;
        size_t chunk = size < available ? size : available;

        memcpy(context->block + context->block_used, source, chunk);
        context->block_used += chunk;
        source += chunk;
        size -= chunk;
        if (context->block_used == sizeof(context->block)) {
            sha256_transform(context, context->block);
            context->block_used = 0;
        }
    }

    /*
     * Hash complete caller-owned blocks in place. read_be32() consumes bytes,
     * so this remains safe for unaligned EE addresses and avoids a 64-byte
     * memcpy for every full block of a multi-megabyte rescue payload.
     */
    while (context->block_used == 0 && size >= sizeof(context->block)) {
        sha256_transform(context, source);
        source += sizeof(context->block);
        size -= sizeof(context->block);
    }

    if (size != 0) {
        memcpy(context->block, source, size);
        context->block_used = size;
    }
}

void sha256_final(sha256_context_t *context, unsigned char digest[32])
{
    uint64_t total_bits = context->total_bytes * 8u;
    unsigned int i;

    context->block[context->block_used++] = 0x80;
    if (context->block_used > 56) {
        memset(context->block + context->block_used, 0,
               sizeof(context->block) - context->block_used);
        sha256_transform(context, context->block);
        context->block_used = 0;
    }
    memset(context->block + context->block_used, 0, 56 - context->block_used);
    for (i = 0; i < 8; i++)
        context->block[63 - i] = (unsigned char)(total_bits >> (i * 8));
    sha256_transform(context, context->block);

    for (i = 0; i < 8; i++)
        write_be32(digest + (i * 4), context->state[i]);
    memset(context, 0, sizeof(*context));
}

void sha256_buffer(const void *data, size_t size, unsigned char digest[32])
{
    sha256_context_t context;

    sha256_init(&context);
    sha256_update(&context, data, size);
    sha256_final(&context, digest);
}

void sha256_hex(const unsigned char digest[32], char output[65])
{
    static const char digits[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < 32; i++) {
        output[i * 2] = digits[digest[i] >> 4];
        output[(i * 2) + 1] = digits[digest[i] & 0x0f];
    }
    output[64] = '\0';
}
