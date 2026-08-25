# Corpus-v2 build overlay.
#
# GNU make prefers GNUmakefile over Makefile. Keep the production build rules in
# Makefile and layer only measured corpus experiments here, so reverting this
# branch never perturbs the known-good HDL development line.

include Makefile

EE_OBJS += gs_ui_draw_minimal_ps2.o
EE_LIBS := $(subst -ldraw,corpus_libdraw.a,$(EE_LIBS))
$(EE_BIN): gs_ui_draw_minimal_ps2.o corpus_libdraw.a

# Keep this compatibility object out of LTO on purpose. Current PS2SDK's
# draw2d.o is a fat-LTO archive member. The linker plugin claims that member as
# a unit even when these symbols are already provided by a normal object, which
# creates duplicate strong definitions and still retains unrelated trig code.
# Instead, link a byte-for-byte copy of libdraw with only draw2d.o removed, and
# provide the five draw2d entry points the manager actually uses here.
gs_ui_draw_minimal_ps2.o: src/gs_ui_draw_minimal_ps2.c
	$(EE_CC) $(filter-out -flto,$(EE_CFLAGS)) $(EE_INCS) -c $< -o $@

corpus_libdraw.a:
	cp $(PS2SDK)/ee/lib/libdraw.a $@
	$(EE_AR) d $@ draw2d.o
