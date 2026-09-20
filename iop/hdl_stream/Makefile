IOP_BIN ?= hdl_stream.irx
IOP_OBJS = hdl_stream.o imports.o

# PS2SDK's generic IOP rules default to -Os. hdl_stream is not a resident-size
# utility in our workload: it is the 37.5 MHz R3000A hot path that must keep
# USB, DEV9 and SIF fed during multi-gigabyte ISO installs. Put -O2 after the
# SDK default so GCC schedules/optimizes this module for throughput instead of
# minimum code size. We deliberately stop at -O2 because the IOP has a tiny
# cache and -O3 loop growth can cost more than it saves on this core.
IOP_CFLAGS += -O2 -DIOMANX_OLD_NAME_COMPATIBILITY=0 \
	-DIOMANX_OLD_NAME_ADDDELDRV=0
IOP_WARNFLAGS = -Wall -Wextra -Werror
IOP_INCS += -I../../include

all: $(IOP_BIN)

clean:
	rm -f -r $(IOP_OBJS_DIR) $(IOP_BIN) \
		$(IOP_BIN:.irx=.notiopmod.elf) \
		$(IOP_BIN:.irx=.notiopmod.stripped.elf)

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
