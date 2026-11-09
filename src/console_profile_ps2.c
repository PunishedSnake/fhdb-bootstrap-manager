/*
 * PS2-side evidence for console_profile: ROM text files plus read-only
 * MechaCon S-commands through the ROM CDVDFSV RPC (public libcdvd API).
 *
 * Only SCMD 0x03 (version) and 0x0A (NVM word read) are issued. No write
 * command is present. Runs from user-requested diagnostics only.
 */

#include <tamtypes.h>
#include <libcdvd.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include <string.h>

#include "console_profile_ps2.h"

static int read_rom_text(const char *path, char *buffer, unsigned int size)
{
    int fd;
    int result;

    memset(buffer, 0, size);
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    result = fileXioRead(fd, buffer, (int)size - 1);
    fileXioClose(fd);
    return result;
}

static int read_nvm_word(u16 offset, u16 *value)
{
    u8 input[2];
    u8 output[16];

    input[0] = (u8)(offset >> 8);
    input[1] = (u8)offset;
    memset(output, 0, sizeof(output));
    if (!sceCdApplySCmd(0x0A, input, sizeof(input), output))
        return -1;
    if (output[0] != 0)
        return -2;
    *value = (u16)(((u16)output[1] << 8) | output[2]);
    return 0;
}

static void probe_mechacon(console_profile_t *profile)
{
    u8 subcommand = 0;
    u8 output[16];
    unsigned int i;

    profile->mecha_rc = -1;
    profile->nvm_rc = -1;
    if (!sceCdInit(SCECdINoD))
        return;
    memset(output, 0, sizeof(output));
    if (sceCdApplySCmd(0x03, &subcommand, 1, output) &&
        (output[0] & 0x80u) == 0u) {
        memcpy(profile->mecha_version, output, 4);
        profile->mecha_version[0] &= 0x7fu;
        profile->mecha_rc = 0;
    }
    /* The MechaPwn key seed only exists on Dragon (major >= 5) MechaCons. */
    if (profile->mecha_rc == 0 && profile->mecha_version[1] >= 5u) {
        profile->nvm_rc = 0;
        for (i = 0; i < CONSOLE_PROFILE_KEYSEED_WORDS; i++) {
            if (read_nvm_word((u16)(227u + i), &profile->nvm_keyseed[i]) < 0) {
                profile->nvm_rc = -2;
                break;
            }
        }
    }
    sceCdInit(SCECdEXIT);
}

void console_profile_collect(console_profile_t *profile)
{
    char psxver[8];

    if (profile == NULL)
        return;
    memset(profile, 0, sizeof(*profile));
    if (read_rom_text("rom0:ROMVER", profile->romver,
                      sizeof(profile->romver)) < 6)
        return;
    profile->is_psx = read_rom_text("rom0:PSXVER", psxver,
                                    sizeof(psxver)) >= 0;
    probe_mechacon(profile);
    profile->collected = 1;
    console_profile_classify(profile);
}
