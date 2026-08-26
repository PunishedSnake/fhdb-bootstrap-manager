EE_BIN = PS2_HDD_BOOTSTRAP_MANAGER.ELF
EE_MAP = PS2_HDD_BOOTSTRAP_MANAGER.map
EE_OBJS = main.o manager_menu_ps2.o app_ui_ps2.o disk_status_ps2.o gs_ui_ps2.o gs_debug_compat_ps2.o app_error.o bootstrap_controller_ps2.o diagnostics_controller_ps2.o forensic_controller_ps2.o platform.o storage.o video_mode.o ui_layout.o ui_font.o spleen_font_data.o header_backup.o repair_snapshot.o forensic_snapshot.o rescue_image.o rescue_storage.o bootstrap_source.o bootstrap_signing.o apa.o apa_repair.o apa_forensic.o repair_health.o hdd_bounds.o hdd_read.o hdd_write.o hdd_repair_ps2.o hdd_forensic_repair_ps2.o repair_controller_ps2.o hdd_recovery_wrap.o bootstrap_transaction.o bootstrap_transaction_ps2.o boot_chain.o boot_chain_ps2.o boot_payload.o boot_payload_ps2.o boot_diagnostics_ps2.o boot_report.o boot_report_ps2.o boot_report_session.o session_log.o kelf.o sha256.o capsule_format.o mbr_compat.o hdl_iso.o hdl_partition.o hdl_transaction.o hdl_installer_ps2.o r5900_perf.o
EE_LIBS = -ldebug -ldraw -lgraph -lpacket -ldma -lm -lpad -lfileXio -lpatches -lpoweroff -lsecr -lkernel
# LTO lets the R5900 compiler optimize across the deliberately small modules
# while section GC still removes unused recovery/UI helpers from the final ELF.
EE_CFLAGS = -O2 -flto -G0 -Wall -Wextra -Werror -std=gnu99 -fdata-sections -ffunction-sections -Iinclude
# Phase 2 experiment: use size optimization only for controller code that is
# explicitly off the steady-state path. This preserves -O2 for normal runtime
# while testing whether cold recovery/UI code can buy back I-cache footprint.
EE_COLD_CFLAGS = $(filter-out -O2,$(EE_CFLAGS)) -Os
# Keep a linker map for every build. The R5900 has a 16 KiB I-cache, so archive
# provenance, section growth and final placement are performance data, not just
# link-time trivia. This also lets CI explain why heavyweight Newlib routines
# survive instead of merely observing them in objdump afterwards.
EE_LDFLAGS = -flto -Wl,--gc-sections -Wl,-Map,$(EE_MAP) -Wl,--wrap=fileXioOpen -Wl,--wrap=fileXioDevctl -Wl,--wrap=scr_printf -Wl,--wrap=scr_vprintf -Wl,--wrap=scr_clear

# Filesystem, MagicGate, USB mass-storage, power, and APA HDD services are
# embedded so the manager does not depend on whichever IOP modules launched it.
IRX_FILES = iomanX.irx fileXio.irx secrman.irx freesio2.irx freepad.irx \
	mcman.irx mcserv.irx secrsif.irx poweroff.irx bdm.irx \
	bdmfs_fatfs.irx usbd.irx usbmass_bd.irx ps2dev9.irx ata_bd.irx \
	ps2fs.irx
EE_OBJS += $(IRX_FILES:.irx=_irx.o)
CUSTOM_IRX = hdl_stream.irx
EE_OBJS += ps2hdd_posix_irx.o hdl_stream_irx.o

HOST_CC ?= cc
HOST_APP_ERROR_TEST = tests/test_app_error
HOST_FORMAT_TEST = tests/test_formats
HOST_APA_REPAIR_TEST = tests/test_apa_repair
HOST_APA_FORENSIC_TEST = tests/test_apa_forensic
HOST_APA_FORENSIC_DORMANT_TEST = tests/test_apa_forensic_dormant_free
HOST_VIDEO_MODE_TEST = tests/test_video_mode
HOST_UI_LAYOUT_TEST = tests/test_ui_layout
HOST_UI_FONT_TEST = tests/test_ui_font
HOST_FORENSIC_FIXTURE_TEST = tests/test_forensic_fixtures
HOST_BOOT_CHAIN_TEST = tests/test_boot_chain
HOST_BOOT_PAYLOAD_TEST = tests/test_boot_payload
HOST_BOOT_REPORT_TEST = tests/test_boot_report
HOST_KELF_TEST = tests/test_kelf
HOST_BOOTSTRAP_TRANSACTION_TEST = tests/test_bootstrap_transaction
HOST_RESCUE_IMAGE_TEST = tests/test_rescue_image
HOST_HDD_FIXTURE_TEST = tests/test_hdd_fixtures
HOST_HDD_MUTATION_TEST = tests/test_hdd_mutations
HOST_HDD_REPAIR_FIXTURE_TEST = tests/test_hdd_repair_fixtures
HOST_HDL_ISO_TEST = tests/test_hdl_iso
HOST_HDL_PARTITION_TEST = tests/test_hdl_partition
HOST_HDL_TRANSACTION_TEST = tests/test_hdl_transaction
HOST_SPLEEN_GENERATED = tests/generated_spleen_font_data.c
HOST_HDD_FIXTURE_DIR = tests/generated_hdds
HOST_FORENSIC_FIXTURE_DIR = tests/generated_forensic_hdds
HOST_TESTS = $(HOST_APP_ERROR_TEST) $(HOST_FORMAT_TEST) $(HOST_APA_REPAIR_TEST) $(HOST_APA_FORENSIC_TEST) $(HOST_APA_FORENSIC_DORMANT_TEST) $(HOST_VIDEO_MODE_TEST) $(HOST_UI_LAYOUT_TEST) $(HOST_UI_FONT_TEST) $(HOST_FORENSIC_FIXTURE_TEST) $(HOST_BOOT_CHAIN_TEST) $(HOST_BOOT_PAYLOAD_TEST) $(HOST_BOOT_REPORT_TEST) $(HOST_KELF_TEST) $(HOST_BOOTSTRAP_TRANSACTION_TEST) $(HOST_RESCUE_IMAGE_TEST) $(HOST_HDD_FIXTURE_TEST) $(HOST_HDD_MUTATION_TEST) $(HOST_HDD_REPAIR_FIXTURE_TEST) $(HOST_HDL_ISO_TEST) $(HOST_HDL_PARTITION_TEST) $(HOST_HDL_TRANSACTION_TEST)

all: $(EE_BIN)

release: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

# Portable tests deliberately work on a machine that has no PS2SDK installed.
test-host:
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_app_error.c src/app_error.c -o $(HOST_APP_ERROR_TEST)
	./$(HOST_APP_ERROR_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_formats.c src/apa.c src/sha256.c src/capsule_format.c \
		-o $(HOST_FORMAT_TEST)
	./$(HOST_FORMAT_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_apa_repair.c src/apa_repair.c src/apa.c \
		-o $(HOST_APA_REPAIR_TEST)
	./$(HOST_APA_REPAIR_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_apa_forensic.c src/apa_forensic.c src/apa.c \
		-o $(HOST_APA_FORENSIC_TEST)
	./$(HOST_APA_FORENSIC_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_apa_forensic_dormant_free.c src/apa_forensic.c src/apa.c \
		-o $(HOST_APA_FORENSIC_DORMANT_TEST)
	./$(HOST_APA_FORENSIC_DORMANT_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_video_mode.c src/video_mode.c -o $(HOST_VIDEO_MODE_TEST)
	./$(HOST_VIDEO_MODE_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_ui_layout.c src/ui_layout.c -o $(HOST_UI_LAYOUT_TEST)
	./$(HOST_UI_LAYOUT_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_ui_font.c src/ui_font.c src/spleen_font_data.c \
		-o $(HOST_UI_FONT_TEST)
	./$(HOST_UI_FONT_TEST)
	python3 tools/generate_spleen_font.py \
		--font-5x8 third_party/spleen/spleen-5x8.bdf.gz \
		--font-8x16 third_party/spleen/spleen-8x16.bdf.gz \
		--output $(HOST_SPLEEN_GENERATED)
	cmp src/spleen_font_data.c $(HOST_SPLEEN_GENERATED)
	python3 tools/generate_forensic_fixtures.py $(HOST_FORENSIC_FIXTURE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_forensic_fixtures.c src/apa_forensic.c src/apa.c \
		-o $(HOST_FORENSIC_FIXTURE_TEST)
	./$(HOST_FORENSIC_FIXTURE_TEST) $(HOST_FORENSIC_FIXTURE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_boot_chain.c src/boot_chain.c -o $(HOST_BOOT_CHAIN_TEST)
	./$(HOST_BOOT_CHAIN_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_boot_payload.c src/boot_payload.c src/kelf.c src/sha256.c \
		-o $(HOST_BOOT_PAYLOAD_TEST)
	./$(HOST_BOOT_PAYLOAD_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_boot_report.c src/boot_report.c src/sha256.c \
		-o $(HOST_BOOT_REPORT_TEST)
	./$(HOST_BOOT_REPORT_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_kelf.c src/kelf.c -o $(HOST_KELF_TEST)
	./$(HOST_KELF_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_bootstrap_transaction.c src/bootstrap_transaction.c \
		-o $(HOST_BOOTSTRAP_TRANSACTION_TEST)
	./$(HOST_BOOTSTRAP_TRANSACTION_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_rescue_image.c src/rescue_image.c src/capsule_format.c \
		src/apa.c src/kelf.c src/sha256.c -o $(HOST_RESCUE_IMAGE_TEST)
	./$(HOST_RESCUE_IMAGE_TEST)
	python3 tools/generate_hdd_fixtures.py $(HOST_HDD_FIXTURE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_hdd_fixtures.c src/apa.c src/hdd_bounds.c src/kelf.c \
		-o $(HOST_HDD_FIXTURE_TEST)
	./$(HOST_HDD_FIXTURE_TEST) $(HOST_HDD_FIXTURE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_hdd_mutations.c src/apa.c src/kelf.c \
		-o $(HOST_HDD_MUTATION_TEST)
	./$(HOST_HDD_MUTATION_TEST) $(HOST_HDD_FIXTURE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_hdd_repair_fixtures.c src/repair_health.c src/apa_repair.c \
		src/apa.c src/hdd_bounds.c src/kelf.c -o $(HOST_HDD_REPAIR_FIXTURE_TEST)
	./$(HOST_HDD_REPAIR_FIXTURE_TEST) $(HOST_HDD_FIXTURE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_hdl_iso.c src/hdl_iso.c -o $(HOST_HDL_ISO_TEST)
	./$(HOST_HDL_ISO_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_hdl_partition.c src/hdl_partition.c \
		-o $(HOST_HDL_PARTITION_TEST)
	./$(HOST_HDL_PARTITION_TEST)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Iinclude \
		tests/test_hdl_transaction.c src/hdl_transaction.c src/sha256.c \
		-o $(HOST_HDL_TRANSACTION_TEST)
	./$(HOST_HDL_TRANSACTION_TEST)

clean:
	rm -f $(EE_BIN) $(EE_MAP) $(EE_OBJS) $(IRX_FILES:.irx=_irx.c) $(HOST_TESTS)
	rm -f $(CUSTOM_IRX) hdl_stream_irx.c iop/hdl_stream/*.o \
		iop/hdl_stream/*.elf iop/hdl_stream/*.irx
	rm -f ps2hdd_posix_irx.c
	rm -f $(HOST_SPLEEN_GENERATED)
	rm -rf $(HOST_HDD_FIXTURE_DIR) $(HOST_FORENSIC_FIXTURE_DIR)

main.o: src/main.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

manager_menu_ps2.o: src/manager_menu_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

app_ui_ps2.o: src/app_ui_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
disk_status_ps2.o: src/disk_status_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

gs_ui_ps2.o: src/gs_ui_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

gs_debug_compat_ps2.o: src/gs_debug_compat_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

app_error.o: src/app_error.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

bootstrap_controller_ps2.o: src/bootstrap_controller_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
diagnostics_controller_ps2.o: src/diagnostics_controller_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

forensic_controller_ps2.o: src/forensic_controller_ps2.c
	$(EE_CC) $(EE_COLD_CFLAGS) $(EE_INCS) -c $< -o $@

platform.o: src/platform.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

storage.o: src/storage.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

video_mode.o: src/video_mode.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

ui_layout.o: src/ui_layout.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

ui_font.o: src/ui_font.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

spleen_font_data.o: src/spleen_font_data.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

header_backup.o: src/header_backup.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

repair_snapshot.o: src/repair_snapshot.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

forensic_snapshot.o: src/forensic_snapshot.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

rescue_image.o: src/rescue_image.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

rescue_storage.o: src/rescue_storage.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

bootstrap_source.o: src/bootstrap_source.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

bootstrap_signing.o: src/bootstrap_signing.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

apa.o: src/apa.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

apa_repair.o: src/apa_repair.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

apa_forensic.o: src/apa_forensic.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

repair_health.o: src/repair_health.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdd_bounds.o: src/hdd_bounds.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdd_read.o: src/hdd_read.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdd_write.o: src/hdd_write.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdd_repair_ps2.o: src/hdd_repair_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdd_forensic_repair_ps2.o: src/hdd_forensic_repair_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

repair_controller_ps2.o: src/repair_controller_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdd_recovery_wrap.o: src/hdd_recovery_wrap.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

bootstrap_transaction.o: src/bootstrap_transaction.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

bootstrap_transaction_ps2.o: src/bootstrap_transaction_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_chain.o: src/boot_chain.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_chain_ps2.o: src/boot_chain_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_payload.o: src/boot_payload.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_payload_ps2.o: src/boot_payload_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_diagnostics_ps2.o: src/boot_diagnostics_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_report.o: src/boot_report.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_report_ps2.o: src/boot_report_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

boot_report_session.o: src/boot_report_session.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

session_log.o: src/session_log.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

kelf.o: src/kelf.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

sha256.o: src/sha256.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

capsule_format.o: src/capsule_format.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

mbr_compat.o: src/mbr_compat.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdl_iso.o: src/hdl_iso.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdl_partition.o: src/hdl_partition.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdl_transaction.o: src/hdl_transaction.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

hdl_installer_ps2.o: src/hdl_installer_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

r5900_perf.o: src/r5900_perf.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

# The POSIX APA build is the PS2SDK variant that enables the public HDL type
# and physical-partition layout query. Its distinct generated symbol avoids a
# hyphenated C identifier while preserving the upstream IRX unchanged.
ps2hdd_posix_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/ps2hdd-bdm.irx $@ ps2hdd_posix_irx

hdl_stream.irx:
	$(MAKE) -C iop/hdl_stream IOP_BIN=$(abspath $@)

hdl_stream_irx.c: hdl_stream.irx
	$(PS2SDK)/bin/bin2c $< $@ hdl_stream_irx

PS2_GOALS := $(filter-out test-host clean,$(MAKECMDGOALS))
ifeq ($(strip $(MAKECMDGOALS)),)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
else ifneq ($(strip $(PS2_GOALS)),)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
endif

.PHONY: all release test-host clean
