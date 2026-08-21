#ifndef PS2_HDD_BOOTSTRAP_MANAGER_KELF_H
#define PS2_HDD_BOOTSTRAP_MANAGER_KELF_H

/*
 * Portable structural checks for the PS2 KELF container used by HDD updates.
 *
 * The byte offsets below mirror the public SecrKELFHeader_t / BIT block layout
 * exposed by PS2SDK, but this module intentionally does not include PS2SDK
 * headers. Reading the fields explicitly as little-endian bytes keeps the same
 * on-console rules while making malformed/truncated fixtures testable on a PC.
 */

#define KELF_FIXED_HEADER_SIZE 32u
#define KELF_BIT_BLOCK_SIZE 16u
#define KELF_MAX_BIT_COUNT 63u

/* Stable validation results retained from the Torii implementation. */
typedef enum {
    KELF_VALID = 0,
    KELF_ERR_TOO_SMALL = -1,
    KELF_ERR_PLAIN_ELF = -2,
    KELF_ERR_HEADER_SIZE = -3,
    KELF_ERR_BIT_COUNT = -4,
    KELF_ERR_FILE_SIZE = -5,
    KELF_ERR_BIT_TABLE = -6,
    KELF_ERR_VARIABLE_SECTION = -7,
    KELF_ERR_KEY_AREA = -8
} kelf_validation_result_t;

/* Stable recovery results for a sector-padded KELF image read from the HDD. */
typedef enum {
    KELF_IMAGE_VALID = 0,
    KELF_IMAGE_ERR_TOO_SMALL = -1,
    KELF_IMAGE_ERR_SIZE_FIELDS = -2,
    KELF_IMAGE_ERR_CALCULATED_SIZE = -3,
    KELF_IMAGE_ERR_LAYOUT = -4
} kelf_image_result_t;

/* Validate one complete, unpadded KELF file without decrypting its payload. */
int kelf_validate_layout(const unsigned char *data, unsigned int size);

/*
 * Recover the real KELF file length from a sector-aligned disk image. The
 * caller owns the image buffer; only the header fields and structural layout
 * are inspected and no data is modified.
 */
int kelf_size_from_disk_image(const unsigned char *data,
                              unsigned int disk_bytes,
                              unsigned int *file_bytes);

#endif
