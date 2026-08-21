#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APA_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APA_H

#include <stdint.h>

#define APA_HEADER_SIZE 1024u
#define APA_MAGIC_OFFSET 0x004u
#define APA_ID_OFFSET 0x010u
#define APA_START_OFFSET 0x040u
#define APA_LENGTH_OFFSET 0x044u
#define APA_TYPE_OFFSET 0x048u
#define APA_FLAGS_OFFSET 0x04au
#define APA_NSUB_OFFSET 0x04cu
#define APA_MAIN_OFFSET 0x058u
#define APA_NUMBER_OFFSET 0x05cu
#define APA_MBR_MAGIC_OFFSET 0x100u
#define APA_MBR_VERSION_OFFSET 0x120u
#define APA_MBR_NSECTOR_OFFSET 0x124u
#define APA_OSD_START_OFFSET 0x130u
#define APA_OSD_SIZE_OFFSET 0x134u
#define PC_MBR_SIGNATURE_OFFSET 0x1feu

#define APA_MASTER_TYPE_VALUE 0x0001u
#define APA_MASTER_VERSION_VALUE 2u

/*
 * Transitional names retained while pure APA parsing leaves main.c. These
 * routines contain no PS2SDK calls and are therefore suitable for host tests.
 */
uint16_t read_le16(const unsigned char *source);
uint32_t read_le32(const unsigned char *source);
uint32_t apa_checksum(const unsigned char *header);
int is_standard_apa_header(const unsigned char *header);
int is_hybrid_gpt(const unsigned char *header);
int headers_match_same_disk(const unsigned char *a, const unsigned char *b);

#endif
