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

/*
 * R5900-oriented scalar transform.
 *
 * The R5900 is in-order, dual-issue and has no rotate instruction, so every
 * removed ALU op and spill counts. Measured from GCC 15 -O2 R5900 output, per
 * 64-byte block: reference loop ~5,820 instructions; this version ~3,990
 * (-31%), for 8.8 KiB of code instead of 2 KiB (still well inside the 16 KiB
 * I-cache; refills at chunk boundaries amortise over thousands of blocks).
 *
 * - Rounds 0..15 are straight-line and load their word from the block.
 * - Rounds 16..63 run as three 16-round groups. With 16-way unrolling every
 *   schedule index is a compile-time constant, so no per-round index math or
 *   loop-carried pointer survives (the reference loop recomputed four
 *   schedule addresses and reloaded its counter from the stack every round).
 * - Rotating variable names remove the seven h=g, g=f, ... moves per round.
 * - Ch = g ^ (e & (f ^ g)) and Maj = (a & b) | (c & (a | b)): 3 and 4 ops
 *   instead of 4 and 5.
 *
 * Output is bit-identical to FIPS 180-4; tests/test_sha256.c covers standard
 * vectors, the million-'a' vector and a split-update sweep.
 */
#define SHA_CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define SHA_MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define SHA_BSIG0(x) (rotate_right((x), 2) ^ rotate_right((x), 13) ^ \
                      rotate_right((x), 22))
#define SHA_BSIG1(x) (rotate_right((x), 6) ^ rotate_right((x), 11) ^ \
                      rotate_right((x), 25))
#define SHA_SSIG0(x) (rotate_right((x), 7) ^ rotate_right((x), 18) ^ ((x) >> 3))
#define SHA_SSIG1(x) (rotate_right((x), 17) ^ rotate_right((x), 19) ^ \
                      ((x) >> 10))

#define SHA_ROUND(a, b, c, d, e, f, g, h, k, w) \
    do { \
        uint32_t t1_ = (h) + SHA_BSIG1(e) + SHA_CH(e, f, g) + (k) + (w); \
        (d) += t1_; \
        (h) = t1_ + SHA_BSIG0(a) + SHA_MAJ(a, b, c); \
    } while (0)

#define SHA_EXPAND16(n) \
    (schedule[(n) & 15] += SHA_SSIG1(schedule[((n) + 14) & 15]) + \
                           schedule[((n) + 9) & 15] + \
                           SHA_SSIG0(schedule[((n) + 1) & 15]))

static void sha256_transform(sha256_context_t *context,
                             const unsigned char block[64])
{
    uint32_t schedule[16];
    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    const uint32_t *k = round_constants;
    unsigned int i;

    schedule[0] = read_be32(block + 0);
    SHA_ROUND(a, b, c, d, e, f, g, h, k[0], schedule[0]);
    schedule[1] = read_be32(block + 4);
    SHA_ROUND(h, a, b, c, d, e, f, g, k[1], schedule[1]);
    schedule[2] = read_be32(block + 8);
    SHA_ROUND(g, h, a, b, c, d, e, f, k[2], schedule[2]);
    schedule[3] = read_be32(block + 12);
    SHA_ROUND(f, g, h, a, b, c, d, e, k[3], schedule[3]);
    schedule[4] = read_be32(block + 16);
    SHA_ROUND(e, f, g, h, a, b, c, d, k[4], schedule[4]);
    schedule[5] = read_be32(block + 20);
    SHA_ROUND(d, e, f, g, h, a, b, c, k[5], schedule[5]);
    schedule[6] = read_be32(block + 24);
    SHA_ROUND(c, d, e, f, g, h, a, b, k[6], schedule[6]);
    schedule[7] = read_be32(block + 28);
    SHA_ROUND(b, c, d, e, f, g, h, a, k[7], schedule[7]);
    schedule[8] = read_be32(block + 32);
    SHA_ROUND(a, b, c, d, e, f, g, h, k[8], schedule[8]);
    schedule[9] = read_be32(block + 36);
    SHA_ROUND(h, a, b, c, d, e, f, g, k[9], schedule[9]);
    schedule[10] = read_be32(block + 40);
    SHA_ROUND(g, h, a, b, c, d, e, f, k[10], schedule[10]);
    schedule[11] = read_be32(block + 44);
    SHA_ROUND(f, g, h, a, b, c, d, e, k[11], schedule[11]);
    schedule[12] = read_be32(block + 48);
    SHA_ROUND(e, f, g, h, a, b, c, d, k[12], schedule[12]);
    schedule[13] = read_be32(block + 52);
    SHA_ROUND(d, e, f, g, h, a, b, c, k[13], schedule[13]);
    schedule[14] = read_be32(block + 56);
    SHA_ROUND(c, d, e, f, g, h, a, b, k[14], schedule[14]);
    schedule[15] = read_be32(block + 60);
    SHA_ROUND(b, c, d, e, f, g, h, a, k[15], schedule[15]);
    for (i = 16; i < 64; i += 16) {
        SHA_ROUND(a, b, c, d, e, f, g, h, k[i + 0], SHA_EXPAND16(0));
        SHA_ROUND(h, a, b, c, d, e, f, g, k[i + 1], SHA_EXPAND16(1));
        SHA_ROUND(g, h, a, b, c, d, e, f, k[i + 2], SHA_EXPAND16(2));
        SHA_ROUND(f, g, h, a, b, c, d, e, k[i + 3], SHA_EXPAND16(3));
        SHA_ROUND(e, f, g, h, a, b, c, d, k[i + 4], SHA_EXPAND16(4));
        SHA_ROUND(d, e, f, g, h, a, b, c, k[i + 5], SHA_EXPAND16(5));
        SHA_ROUND(c, d, e, f, g, h, a, b, k[i + 6], SHA_EXPAND16(6));
        SHA_ROUND(b, c, d, e, f, g, h, a, k[i + 7], SHA_EXPAND16(7));
        SHA_ROUND(a, b, c, d, e, f, g, h, k[i + 8], SHA_EXPAND16(8));
        SHA_ROUND(h, a, b, c, d, e, f, g, k[i + 9], SHA_EXPAND16(9));
        SHA_ROUND(g, h, a, b, c, d, e, f, k[i + 10], SHA_EXPAND16(10));
        SHA_ROUND(f, g, h, a, b, c, d, e, k[i + 11], SHA_EXPAND16(11));
        SHA_ROUND(e, f, g, h, a, b, c, d, k[i + 12], SHA_EXPAND16(12));
        SHA_ROUND(d, e, f, g, h, a, b, c, k[i + 13], SHA_EXPAND16(13));
        SHA_ROUND(c, d, e, f, g, h, a, b, k[i + 14], SHA_EXPAND16(14));
        SHA_ROUND(b, c, d, e, f, g, h, a, k[i + 15], SHA_EXPAND16(15));
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
