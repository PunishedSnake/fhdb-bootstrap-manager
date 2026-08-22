IOP_BIN ?= hdl_stream.irx

IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 \
	-DIOMANX_OLD_NAME_ADDDELDRV=0

IOP_IMPORT_INCS += \
	system/iomanx \
	system/loadcore \
	system/sysclib \
	system/sysmem

IOP_INCS += -I../../include
IOP_OBJS = hdl_stream.o imports.o

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.bin.make
include $(PS2SDKSRC)/iop/Rules.make
include $(PS2SDKSRC)/iop/Rules.release
