# Corpus-v2 Phase-1 fileXio/newlib link policy experiment.
#
# Keep the normal project Makefile unchanged. This PS2-only object is linked
# before libfileXio so its application-specific _ps2sdk_fileXio_init/deinit
# policy can be measured against the unmodified PS2SDK archive path.

include Makefile

EE_OBJS += filexio_fdman_policy_ps2.o
$(EE_BIN): filexio_fdman_policy_ps2.o

filexio_fdman_policy_ps2.o: src/filexio_fdman_policy_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
