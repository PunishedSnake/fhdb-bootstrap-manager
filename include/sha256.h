#ifndef PS2_HDD_BOOTSTRAP_MANAGER_SHA256_H
#define PS2_HDD_BOOTSTRAP_MANAGER_SHA256_H

/* Small streaming SHA-256 interface shared by the PS2 build and host tests. */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    unsigned char block[64];
    size_t block_used;
} sha256_context_t;

void sha256_init(sha256_context_t *context);
void sha256_update(sha256_context_t *context, const void *data, size_t size);
void sha256_final(sha256_context_t *context, unsigned char digest[32]);
void sha256_buffer(const void *data, size_t size, unsigned char digest[32]);
void sha256_hex(const unsigned char digest[32], char output[65]);

#endif
