# Corpus-v2 Phase-1 application/runtime link policies.
#
# Keep the normal project Makefile unchanged so every policy remains an explicit
# A/B layer. These small objects are linked before the PS2SDK/Newlib archives and
# replace only contracts that the Phase-1 source audits prove unused.

include Makefile

EE_OBJS += filexio_fdman_policy_ps2.o printf_policy_ps2.o
$(EE_BIN): filexio_fdman_policy_ps2.o printf_policy_ps2.o
EE_LDFLAGS += -Wl,--wrap=snprintf -Wl,--wrap=vsnprintf

filexio_fdman_policy_ps2.o: src/filexio_fdman_policy_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

# GNU ld --wrap rewrites references after the LTO plugin has performed its own
# reachability pass. Keep these two externally-named shims as ordinary object
# code so LTO cannot discard them before ld creates __wrap_* references.
printf_policy_ps2.o: src/printf_policy_ps2.c
	$(EE_CC) $(EE_CFLAGS) -fno-lto $(EE_INCS) -c $< -o $@
