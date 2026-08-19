EE_BIN = PS2_HDD_BOOTSTRAP_MANAGER.ELF
EE_OBJS = main.o
EE_LIBS = -ldebug -lpad -lfileXio -lpatches -lpoweroff -lsecr -lkernel
EE_CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99 -fdata-sections -ffunction-sections
EE_LDFLAGS = -Wl,--gc-sections

# Filesystem, MagicGate, USB mass-storage, power, and APA HDD services are
# embedded so the manager does not depend on whichever IOP modules launched it.
IRX_FILES = iomanX.irx fileXio.irx secrman.irx freesio2.irx freepad.irx \
	mcman.irx mcserv.irx secrsif.irx poweroff.irx bdm.irx \
	bdmfs_fatfs.irx usbd.irx usbmass_bd.irx ps2dev9.irx ps2atad.irx \
	ps2hdd.irx
EE_OBJS += $(IRX_FILES:.irx=_irx.o)

all: $(EE_BIN)

release: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(IRX_FILES:.irx=_irx.c)

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
