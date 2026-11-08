/* Host tests for the read-only DEV9 adapter classifier. */

#include <stdio.h>
#include <string.h>

#include "dev9_fingerprint.h"

static int failures;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
            failures++; \
        } \
    } while (0)

int main(void)
{
    dev9_fingerprint_t fp;

    memset(&fp, 0, sizeof(fp));
    CHECK(dev9_fingerprint_classify(&fp) == DEV9_ADAPTER_NOT_SAMPLED,
          "unsampled");
    CHECK(dev9_fingerprint_classify(NULL) == DEV9_ADAPTER_NOT_SAMPLED, "NULL");

    fp.sampled = 1;
    fp.model = DEV9_FP_MODEL_EXPBAY;
    CHECK(dev9_fingerprint_classify(&fp) == DEV9_ADAPTER_UNREADABLE,
          "all-zero bus is unreadable");
    fp.spd_rev1 = fp.spd_rev3 = fp.compat20 = 0xffffu;
    CHECK(dev9_fingerprint_classify(&fp) == DEV9_ADAPTER_UNREADABLE,
          "floating bus is unreadable");

    fp.spd_rev1 = 0x0011u;
    fp.spd_rev3 = DEV9_FP_CAPS_SMAP | DEV9_FP_CAPS_ATA;
    fp.compat20 = 0x0001u;
    CHECK(dev9_fingerprint_classify(&fp) == DEV9_ADAPTER_OFFICIAL, "official");
    CHECK(!dev9_psbbn_adapter_risk(&fp, 1), "official adapter has no risk");

    fp.compat20 = 0x0000u;
    CHECK(dev9_fingerprint_classify(&fp) == DEV9_ADAPTER_COMPATIBLE,
          "compatible with SMAP claim");
    CHECK(dev9_psbbn_adapter_risk(&fp, 1), "PSBBN on compatible adapter");
    CHECK(!dev9_psbbn_adapter_risk(&fp, 0), "no PSBBN, no warning");

    CHECK(strcmp(dev9_model_name(DEV9_FP_MODEL_PCMCIA),
                 "PCMCIA (CXD9566 class)") == 0, "model name");
    CHECK(strcmp(dev9_model_name(7), "unknown") == 0, "unknown model");

    if (failures != 0)
        return 1;
    puts("All DEV9 adapter fingerprint tests passed.");
    return 0;
}
