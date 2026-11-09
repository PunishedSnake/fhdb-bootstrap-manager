/* Exact SHA-256 identities of published HDD bootstrap payloads. */

#include "boot_payload_catalog.h"

#include <stddef.h>
#include <string.h>

static const boot_payload_catalog_entry_t catalog[] = {
    /* OSDMenu MBR 1.3.0: 389632 bytes, 761 sectors, declared KELF 389528 bytes. */
    {"OSDMenu MBR", "1.3.0", "PSBBN Definitive Project", BOOT_PAYLOAD_CATALOG_PRESIGNED,
     {
         0x6e, 0xe6, 0x0d, 0x75, 0xfd, 0x33, 0x36, 0x44,
         0x15, 0x38, 0x65, 0x5b, 0xaa, 0x06, 0xf7, 0x5a,
         0x6c, 0x95, 0x18, 0x91, 0x7f, 0xd1, 0x47, 0x5a,
         0xa2, 0x48, 0x12, 0xd7, 0x98, 0x0c, 0x5e, 0x9f},
     {
         0x0c, 0x3a, 0xe9, 0xb3, 0xa0, 0x25, 0xae, 0xbf,
         0x6f, 0xe6, 0x6d, 0x5c, 0xa5, 0x14, 0xf0, 0x5f,
         0x1c, 0xa6, 0x28, 0xa5, 0x99, 0xf0, 0x15, 0x24,
         0x83, 0xf7, 0x22, 0x67, 0xe5, 0x3b, 0x20, 0x57}},
};

static int digest_present(const unsigned char *digest)
{
    unsigned int i;

    if (digest == NULL)
        return 0;
    for (i = 0; i < 32u; i++) {
        if (digest[i] != 0)
            return 1;
    }
    return 0;
}

const boot_payload_catalog_entry_t *boot_payload_catalog_find(
    const unsigned char *sector_sha256, const unsigned char *kelf_sha256)
{
    unsigned int i;

    /* An all-zero digest means "not computed" in boot_chain_info_t. */
    if (!digest_present(sector_sha256))
        sector_sha256 = NULL;
    if (!digest_present(kelf_sha256))
        kelf_sha256 = NULL;
    for (i = 0; i < sizeof(catalog) / sizeof(catalog[0]); i++) {
        if ((sector_sha256 != NULL &&
             memcmp(catalog[i].sector_sha256, sector_sha256, 32) == 0) ||
            (kelf_sha256 != NULL &&
             memcmp(catalog[i].kelf_sha256, kelf_sha256, 32) == 0))
            return &catalog[i];
    }
    return NULL;
}

unsigned int boot_payload_catalog_count(void)
{
    return (unsigned int)(sizeof(catalog) / sizeof(catalog[0]));
}
