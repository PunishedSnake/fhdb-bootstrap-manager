# Corpus-v2 build overlay.
#
# GNU make prefers GNUmakefile over Makefile. Define the non-LTO UI primitive
# object as an override before loading the normal project build so the object is
# present in EE_OBJS while Makefile.eeglobal constructs the final link rule.
# The rest of the project's build remains in Makefile.

override EE_OBJS += gs_ui_draw_minimal_ps2.o

include Makefile

# Keep this compatibility object out of LTO on purpose. With a normal object in
# the explicit link list, BFD ld can resolve the five draw2d symbols before it
# scans libdraw.a. The PS2SDK draw2d archive member is itself an LTO object and
# otherwise gets claimed by the plugin as a whole, retaining unrelated arc/trig
# code. This is a link-layout experiment, not a global no-LTO policy.
gs_ui_draw_minimal_ps2.o: src/gs_ui_draw_minimal_ps2.c
	$(EE_CC) $(filter-out -flto,$(EE_CFLAGS)) $(EE_INCS) -c $< -o $@
