#ifndef PS2_HDD_BOOTSTRAP_MANAGER_BOOT_PAYLOAD_CATALOG_H
#define PS2_HDD_BOOTSTRAP_MANAGER_BOOT_PAYLOAD_CATALOG_H

/*
 * Exact identities of known HDD bootstrap payloads.
 *
 * The manager never decrypts a KELF, so it cannot name an encrypted payload
 * from its contents. It can, however, recognise byte-identical copies of
 * published files. Each entry is an exact SHA-256 identity claim; anything
 * not listed keeps the existing evidence-based family heuristics.
 *
 * Add entries only with tools/payload_catalog_entry.py on a file you have.
 */

#define BOOT_PAYLOAD_CATALOG_PRESIGNED 0x0001u

typedef struct {
    const char *family;
    const char *version;
    const char *source;
    unsigned int flags;
    unsigned char sector_sha256[32]; /* whole-sector image behind osdSize */
    unsigned char kelf_sha256[32];   /* declared header + content bytes */
} boot_payload_catalog_entry_t;

/* Either digest may be NULL. Returns NULL when nothing matches. */
const boot_payload_catalog_entry_t *boot_payload_catalog_find(
    const unsigned char *sector_sha256, const unsigned char *kelf_sha256);

unsigned int boot_payload_catalog_count(void);

#endif
