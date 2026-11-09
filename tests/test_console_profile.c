/* Host tests for the read-only console/region policy. */

#include <stdio.h>
#include <string.h>

#include "console_profile.h"

static int failures;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
            failures++; \
        } \
    } while (0)

static const uint16_t seed[5] = {0x654du, 0x6863u, 0x5061u, 0x6e77u, 0xec00u};

static void make(console_profile_t *p, const char *romver, unsigned int major,
                 unsigned int minor_raw, int mechapwn)
{
    memset(p, 0, sizeof(*p));
    p->collected = 1;
    snprintf(p->romver, sizeof(p->romver), "%s", romver);
    p->mecha_version[1] = (uint8_t)major;
    p->mecha_version[2] = (uint8_t)minor_raw;
    if (mechapwn)
        memcpy(p->nvm_keyseed, seed, sizeof(seed));
    console_profile_classify(p);
}

int main(void)
{
    console_profile_t p;

    memset(&p, 0, sizeof(p));
    console_profile_classify(&p);
    CHECK(strcmp(console_profile_policy_text(&p), "not collected") == 0,
          "uncollected");

    make(&p, "0220EC20060210", 3, 6, 0); /* SCPH-50004-like, pre-Dragon */
    CHECK(p.romver_region == 'E' && !p.is_dragon &&
              p.unlock_kind == CONSOLE_UNLOCK_NONE,
          "retail CEX");
    CHECK(strcmp(console_profile_policy_text(&p),
                 "active ROMVER region + retail MechaCon") == 0, "retail text");

    make(&p, "0190JD20030110", 5, 4, 0);
    CHECK(p.rom_is_dex && p.unlock_kind == CONSOLE_UNLOCK_REAL_DEX, "real DEX");

    make(&p, "0230AC20080220", 5, 9, 0);
    CHECK(p.mecha_dex_like && p.mecha_minor == 8 &&
              p.unlock_kind == CONSOLE_UNLOCK_DEX_LIKE,
          "DEX-like Dragon without seed");

    make(&p, "0230AC20080220", 5, 8, 1);
    CHECK(p.mechapwn_signature && p.unlock_kind == CONSOLE_UNLOCK_MECHAPWN &&
              !p.cross_region_required,
          "pre-Deckard MechaPwn CEX");

    make(&p, "0250EC20100415", 6, 6, 1);
    CHECK(p.is_deckard && p.cross_region_required, "Deckard CEX MechaPwn");

    make(&p, "0250EC20100415", 6, 7, 1);
    CHECK(p.region_mismatch && p.unlock_kind == CONSOLE_UNLOCK_AMBIGUOUS,
          "Deckard DEX patch before reboot");
    make(&p, "0250AC20100415", 6, 7, 1);
    CHECK(!p.region_mismatch && p.unlock_kind == CONSOLE_UNLOCK_MECHAPWN,
          "Deckard DEX patch active in region A");

    make(&p, "0160HC20020426", 3, 2, 1);
    CHECK(!p.mechapwn_signature, "seed ignored on pre-Dragon MechaCon");

    p.mecha_rc = -1;
    console_profile_classify(&p);
    CHECK(strcmp(console_profile_policy_text(&p),
                 "active ROMVER region; MechaCon unavailable") == 0,
          "MechaCon unavailable");

    if (failures != 0)
        return 1;
    puts("All console profile policy tests passed.");
    return 0;
}
