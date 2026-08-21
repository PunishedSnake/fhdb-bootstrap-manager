#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDD_LIMITS_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDD_LIMITS_H

/*
 * Shared geometry and policy limits for the __mbr bootstrap area.
 *
 * These values were already used by the Torii read/write paths. Keeping them
 * in one header prevents the new read-only transport from silently drifting
 * away from the still-monolithic write verification code during Michishirube.
 */
#define HDD_MBR_PAYLOAD_START 0x2000u
#define HDD_SECTOR_SIZE 512u
#define HDD_TRANSFER_SECTORS 2u
#define HDD_TRANSFER_BYTES (HDD_SECTOR_SIZE * HDD_TRANSFER_SECTORS)
#define HDD_MAX_MBR_PAYLOAD_SIZE (4u * 1024u * 1024u)

#endif
