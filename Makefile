EE_BIN = PS2_HDD_BOOTSTRAP_MANAGER.ELF
EE_OBJS = main.o sha256.o capsule_format.o
EE_LIBS = -ldebug -lpad -lfileXio -lpatches -lpoweroff -lsecr -lkernel
EE_CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99 -fdata-sections -ffunction-sections -Iinclude
EE_LDFLAGS = -Wl,--gc-sections

# Filesystem, MagicGate, USB mass-storage, power, and APA HDD services are
# embedded so the manager does not depend on whichever IOP modules launched it.
IRX_FILES = iomanX.irx fileXio.irx secrman.irx freesio2.irx freepad.irx \
	mcman.irx mcserv.irx secrsif.irx poweroff.irx bdm.irx \
	bdmfs_fatfs.irx usbd.irx usbmass_bd.irx ps2dev9.irx ps2atad.irx \
	ps2hdd.irx ps2fs.irx
EE_OBJS += $(IRX_FILES:.irx=_irx.o)

HOST_CC ?= cc
HOST_TEST = tests/test_formats

all: $(EE_BIN)

release: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

# Portable tests deliberately work on a machine that has no PS2SDK installed.
test-host:
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_formats.c src/sha256.c src/capsule_format.c -o $(HOST_TEST)
	./$(HOST_TEST)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(IRX_FILES:.irx=_irx.c) $(HOST_TEST)

# Keep PS2 objects at the repository root. The legacy PS2SDK sample rules keep
# SDK include directories in EE_INCS, so custom rules for src/ must pass both
# EE_CFLAGS and EE_INCS just like the stock compilation rule does.
main.o: src/main.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

sha256.o: src/sha256.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

capsule_format.o: src/capsule_format.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

# Only PS2 build goals need the SDK's global rules. This makes `make test-host`
# and `make clean` useful on an ordinary development machine or CI runner.
PS2_GOALS := $(filter-out test-host clean,$(MAKECMDGOALS))
ifeq ($(strip $(MAKECMDGOALS)),)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
else ifneq ($(strip $(PS2_GOALS)),)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
endif

.PHONY: all release test-host clean
