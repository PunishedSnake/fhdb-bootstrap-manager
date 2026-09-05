EE_BIN = R5900_COUNTER_CALIBRATION.ELF
EE_OBJS = main.o r5900_perf.o
EE_LIBS = -ldebug -lkernel

EE_CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99 -I../../include
EE_LDFLAGS = -Wl,-Map,R5900_COUNTER_CALIBRATION.map

all: $(EE_BIN)

main.o: main.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

r5900_perf.o: ../../src/r5900_perf.c ../../include/r5900_perf.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c ../../src/r5900_perf.c -o $@

clean:
	rm -f $(EE_BIN) R5900_COUNTER_CALIBRATION.map $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

.PHONY: all clean
