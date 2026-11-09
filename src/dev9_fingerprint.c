/* Portable classification of the read-only DEV9/SPEED adapter identity. */

#include "dev9_fingerprint.h"

#include <stddef.h>

dev9_adapter_class_t dev9_fingerprint_classify(const dev9_fingerprint_t *fp)
{
    if (fp == NULL || !fp->sampled)
        return DEV9_ADAPTER_NOT_SAMPLED;
    /* An absent or unpowered device reads back an all-zero or all-one bus. */
    if ((fp->spd_rev3 == 0x0000u && fp->compat20 == 0x0000u &&
         fp->spd_rev1 == 0x0000u) ||
        (fp->spd_rev3 == 0xffffu && fp->compat20 == 0xffffu &&
         fp->spd_rev1 == 0xffffu))
        return DEV9_ADAPTER_UNREADABLE;
    return fp->compat20 == DEV9_FP_OFFICIAL_COMPAT ? DEV9_ADAPTER_OFFICIAL
                                                   : DEV9_ADAPTER_COMPATIBLE;
}

const char *dev9_adapter_class_name(dev9_adapter_class_t adapter)
{
    switch (adapter) {
        case DEV9_ADAPTER_NOT_SAMPLED: return "not sampled";
        case DEV9_ADAPTER_UNREADABLE: return "unreadable";
        case DEV9_ADAPTER_OFFICIAL: return "official Sony (SPEED 0x20=0x0001)";
        case DEV9_ADAPTER_COMPATIBLE: return "third-party compatible";
        default: return "unknown";
    }
}

const char *dev9_model_name(int model)
{
    switch (model) {
        case DEV9_FP_MODEL_PCMCIA: return "PCMCIA (CXD9566 class)";
        case DEV9_FP_MODEL_EXPBAY: return "expansion bay (CXD9611 class)";
        default: return "unknown";
    }
}

int dev9_psbbn_adapter_risk(const dev9_fingerprint_t *fp, int psbbn_present)
{
    return psbbn_present &&
           dev9_fingerprint_classify(fp) == DEV9_ADAPTER_COMPATIBLE;
}
