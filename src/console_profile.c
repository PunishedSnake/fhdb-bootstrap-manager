/*
 * Portable console/region policy, ported from PS2 Memory Card Inspector's
 * console_profile.c. Only the decision logic lives here; the CDVD/ROM reads
 * are in console_profile_ps2.c.
 */

#include "console_profile.h"

#include <stddef.h>
#include <string.h>

/* MechaPwn's region patch leaves "MechaPwn\0 EC" in NVM words 227..231, read
 * back as these numeric words. Fingerprint only; no write path exists. */
static const uint16_t mechapwn_keyseed[CONSOLE_PROFILE_KEYSEED_WORDS] = {
    0x654du, 0x6863u, 0x5061u, 0x6e77u, 0xec00u
};

void console_profile_classify(console_profile_t *profile)
{
    if (profile == NULL)
        return;
    profile->romver_region = '?';
    profile->rom_is_dex = 0;
    profile->mecha_major = profile->mecha_minor = 0;
    profile->is_dragon = profile->is_deckard = 0;
    profile->mecha_dex_like = profile->mechapwn_signature = 0;
    profile->region_mismatch = profile->cross_region_required = 0;
    profile->unlock_kind = CONSOLE_UNLOCK_NONE;
    if (!profile->collected)
        return;

    if (strlen(profile->romver) >= 6u) {
        profile->romver_region = profile->romver[4];
        profile->rom_is_dex = profile->romver[5] == 'D';
    }
    if (profile->mecha_rc == 0) {
        unsigned int minor_raw = profile->mecha_version[2];

        profile->mecha_major = profile->mecha_version[1];
        profile->mecha_minor = minor_raw & ~1u;
        profile->is_dragon = profile->mecha_major >= 5u;
        profile->is_deckard = profile->mecha_major == 6u &&
                              profile->mecha_minor >= 6u;
        /* Dragon DEX/QA configurations expose the odd partner of the retail
         * minor revision. A capability signal, not a product name. */
        profile->mecha_dex_like = profile->is_dragon && (minor_raw & 1u) != 0u;
    }
    if (profile->is_dragon && profile->nvm_rc == 0)
        profile->mechapwn_signature =
            memcmp(profile->nvm_keyseed, mechapwn_keyseed,
                   sizeof(mechapwn_keyseed)) == 0;

    if (profile->rom_is_dex) {
        profile->unlock_kind = CONSOLE_UNLOCK_REAL_DEX;
    } else if (profile->mechapwn_signature) {
        profile->unlock_kind = CONSOLE_UNLOCK_MECHAPWN;
        if (profile->is_deckard) {
            if (profile->mecha_dex_like && profile->romver_region != 'A') {
                /* Deckard DEX policy uses region A; anything else is a
                 * pre-reboot/transitional state. */
                profile->region_mismatch = 1;
                profile->unlock_kind = CONSOLE_UNLOCK_AMBIGUOUS;
            } else if (!profile->mecha_dex_like) {
                profile->cross_region_required = 1;
            }
        }
    } else if (profile->mecha_dex_like) {
        profile->unlock_kind = CONSOLE_UNLOCK_DEX_LIKE;
    }
}

const char *console_unlock_kind_text(console_unlock_kind_t kind)
{
    switch (kind) {
        case CONSOLE_UNLOCK_REAL_DEX: return "real DEX";
        case CONSOLE_UNLOCK_MECHAPWN: return "MechaPwn region patch";
        case CONSOLE_UNLOCK_DEX_LIKE: return "DEX-like / region unlocked";
        case CONSOLE_UNLOCK_AMBIGUOUS: return "ambiguous region state";
        default: return "retail / region-locked";
    }
}

const char *console_profile_policy_text(const console_profile_t *profile)
{
    if (profile == NULL || !profile->collected)
        return "not collected";
    if (profile->region_mismatch)
        return "MechaPwn DEX transition; reboot before region-specific installs";
    if (profile->rom_is_dex)
        return "active ROMVER region + real DEX";
    if (profile->mechapwn_signature) {
        if (profile->is_deckard && profile->mecha_dex_like)
            return "MechaPwn Deckard DEX (region A)";
        if (profile->cross_region_required)
            return "MechaPwn Deckard CEX; region can move, keep cross-region setups";
        if (profile->mecha_dex_like)
            return "MechaPwn DEX-like; active ROMVER region";
        return "MechaPwn CEX region override";
    }
    if (profile->mecha_dex_like)
        return "active ROMVER region + DEX-like MechaCon";
    if (profile->mecha_rc != 0)
        return "active ROMVER region; MechaCon unavailable";
    return "active ROMVER region + retail MechaCon";
}
