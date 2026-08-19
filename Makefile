EE_BIN = FHDB_BOOTSTRAP_MANAGER.ELF
EE_OBJS = main.o
EE_LIBS = -ldebug -lpad -lfileXio -lpatches -lpoweroff -lkernel
EE_CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99 -fdata-sections -ffunction-sections
EE_LDFLAGS = -Wl,--gc-sections

IRX_FILES = iomanX.irx fileXio.irx poweroff.irx ps2dev9.irx ps2atad.irx ps2hdd.irx
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
