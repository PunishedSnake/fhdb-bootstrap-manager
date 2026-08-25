# Corpus-v2 Phase-1 link experiment overlay.
#
# GNU make loads this before Makefile. The production build stays untouched;
# this branch only prepends one compatibility symbol before the PS2SDK archives
# so we can measure whether graph_config.o is unnecessary retained work.

include Makefile

EE_OBJS += graph_config_link_policy_ps2.o
$(EE_BIN): graph_config_link_policy_ps2.o

graph_config_link_policy_ps2.o: src/graph_config_link_policy_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
