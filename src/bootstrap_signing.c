/* PS2 MagicGate signing adapter for a prepared HDD bootstrap KELF. */

#include <libsecr.h>

#include "bootstrap_signing.h"
#include "kelf.h"

void bootstrap_signing_init(void)
{
    SecrInit();
}

int bootstrap_signing_sign(int memory_card_port, unsigned char *payload,
                           unsigned int payload_size,
                           bootstrap_signing_result_t *result)
{
    int code;

    if (result != NULL) {
        result->stage = BOOTSTRAP_SIGNING_STAGE_NONE;
        result->code = 0;
    }
    if (result == NULL || payload == NULL || payload_size == 0 ||
        memory_card_port < 0 || memory_card_port > 1) {
        if (result != NULL)
            result->stage = BOOTSTRAP_SIGNING_STAGE_MAGICGATE;
        return -1;
    }

    if (SecrDownloadFile(2 + memory_card_port, 0, payload) == NULL) {
        result->stage = BOOTSTRAP_SIGNING_STAGE_MAGICGATE;
        result->code = 0;
        return -1;
    }

    code = kelf_validate_layout(payload, payload_size);
    if (code < 0) {
        result->stage = BOOTSTRAP_SIGNING_STAGE_KELF;
        result->code = code;
        return code;
    }

    return 0;
}
