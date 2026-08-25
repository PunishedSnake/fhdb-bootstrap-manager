# Corpus-v2 build overlay.
#
# GNU make prefers GNUmakefile over Makefile. Load the normal project build
# first, then append the one link-layout experiment object. Adding a prerequisite
# to the already-defined EE target keeps the normal dependency graph intact;
# Makefile.eeglobal expands EE_OBJS when it executes the link recipe, so the
# appended object is also present in the explicit link list before libdraw.a.

include Makefile

EE_OBJS += gs_ui_draw_minimal_ps2.o
$(EE_BIN): gs_ui_draw_minimal_ps2.o

# Keep this compatibility object out of LTO on purpose. With a normal object in
# the explicit link list, BFD ld can resolve the five draw2d symbols before it
# scans libdraw.a. The PS2SDK draw2d archive member is itself an LTO object and
# otherwise gets claimed by the plugin as a whole, retaining unrelated arc/trig
# code. This is a link-layout experiment, not a global no-LTO policy.
gs_ui_draw_minimal_ps2.o: src/gs_ui_draw_minimal_ps2.c
	$(EE_CC) $(filter-out -flto,$(EE_CFLAGS)) $(EE_INCS) -c $< -o $@
