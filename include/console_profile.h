#ifndef PS2_HDD_BOOTSTRAP_MANAGER_CONSOLE_PROFILE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_CONSOLE_PROFILE_H

/*
 * Read-only console/region profile, ported from PS2 Memory Card Inspector
 * (ps2-magicgate-inspector/src/console_profile.c, same author, MIT).
 *
 * Why the bootstrap manager wants it: HDD-OSD/HOSDMenu KELFs that are not
 * region-free fail on non-matching consoles, and DEX units and MechaPwn
 * region patches change which system-update rules apply. A boot report that
 * states the console's real policy saves guessing from a model sticker.
 *
 * The raw evidence is gathered PS2-side with public CDVD RPCs only:
 *   rom0:ROMVER / rom0:PSXVER, SCMD 0x03/00 (MechaCon version) and
 *   SCMD 0x0A reads of NVM words 227..231 (MechaPwn key-seed fingerprint).
 * No NVM or configuration write command exists anywhere in this code.
 * Classification below is pure and host-tested.
 */

#include <stdint.h>

#define CONSOLE_PROFILE_ROMVER_MAX 17u
#define CONSOLE_PROFILE_KEYSEED_WORDS 5u

typedef enum {
    CONSOLE_UNLOCK_NONE = 0,
    CONSOLE_UNLOCK_REAL_DEX,
    CONSOLE_UNLOCK_MECHAPWN,
    CONSOLE_UNLOCK_DEX_LIKE,
    CONSOLE_UNLOCK_AMBIGUOUS
} console_unlock_kind_t;

typedef struct {
    /* Raw evidence (filled by console_profile_ps2.c or a test). */
    int collected;
    char romver[CONSOLE_PROFILE_ROMVER_MAX];
    int is_psx;
    int mecha_rc;                    /* 0 when mecha_version is valid */
    uint8_t mecha_version[4];
    int nvm_rc;                      /* 0 when nvm_keyseed is valid */
    uint16_t nvm_keyseed[CONSOLE_PROFILE_KEYSEED_WORDS];

    /* Derived by console_profile_classify(). */
    char romver_region;
    int rom_is_dex;
    unsigned int mecha_major;
    unsigned int mecha_minor;        /* DEX bit cleared */
    int is_dragon;
    int is_deckard;
    int mecha_dex_like;
    int mechapwn_signature;
    int region_mismatch;
    int cross_region_required;
    console_unlock_kind_t unlock_kind;
} console_profile_t;

void console_profile_classify(console_profile_t *profile);
const char *console_unlock_kind_text(console_unlock_kind_t kind);
const char *console_profile_policy_text(const console_profile_t *profile);

#endif
