EE_BIN = PS2_HDD_BOOTSTRAP_MANAGER.ELF
EE_OBJS = main.o manager_menu_ps2.o app_ui_ps2.o disk_status_ps2.o app_error.o bootstrap_controller_ps2.o diagnostics_controller_ps2.o forensic_controller_ps2.o platform.o storage.o header_backup.o repair_snapshot.o forensic_snapshot.o rescue_image.o rescue_storage.o bootstrap_source.o bootstrap_signing.o apa.o apa_repair.o apa_forensic.o repair_health.o hdd_bounds.o hdd_read.o hdd_write.o hdd_repair_ps2.o hdd_forensic_repair_ps2.o repair_controller_ps2.o hdd_recovery_wrap.o bootstrap_transaction.o bootstrap_transaction_ps2.o boot_chain.o boot_chain_ps2.o boot_payload.o boot_payload_ps2.o boot_diagnostics_ps2.o boot_report.o boot_report_ps2.o boot_report_session.o session_log.o kelf.o sha256.o capsule_format.o mbr_compat.o
EE_LIBS = -ldebug -lpad -lfileXio -lpatches -lpoweroff -lsecr -lkernel
EE_CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99 -fdata-sections -ffunction-sections -Iinclude
EE_LDFLAGS = -Wl,--gc-sections -Wl,--wrap=fileXioOpen -Wl,--wrap=fileXioDevctl

# Filesystem, MagicGate, USB mass-storage, power, and APA HDD services are
# embedded so the manager does not depend on whichever IOP modules launched it.
IRX_FILES = iomanX.irx fileXio.irx secrman.irx freesio2.irx freepad.irx \
	mcman.irx mcserv.irx secrsif.irx poweroff.irx bdm.irx \
	bdmfs_fatfs.irx usbd.irx usbmass_bd.irx ps2dev9.irx ps2atad.irx \
	ps2hdd.irx ps2fs.irx
EE_OBJS += $(IRX_FILES:.irx=_irx.o)

HOST_CC ?= cc
HOST_APP_ERROR_TEST = tests/test_app_error
HOST_FORMAT_TEST = tests/test_formats
HOST_APA_REPAIR_TEST = tests/test_apa_repair
HOST_APA_FORENSIC_TEST = tests/test_apa_forensic
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
HOST_HDD_FIXTURE_DIR = tests/generated_hdds
HOST_FORENSIC_FIXTURE_DIR = tests/generated_forensic_hdds
HOST_TESTS = $(HOST_APP_ERROR_TEST) $(HOST_FORMAT_TEST) $(HOST_APA_REPAIR_TEST) $(HOST_APA_FORENSIC_TEST) $(HOST_FORENSIC_FIXTURE_TEST) $(HOST_BOOT_CHAIN_TEST) $(HOST_BOOT_PAYLOAD_TEST) $(HOST_BOOT_REPORT_TEST) $(HOST_KELF_TEST) $(HOST_BOOTSTRAP_TRANSACTION_TEST) $(HOST_RESCUE_IMAGE_TEST) $(HOST_HDD_FIXTURE_TEST) $(HOST_HDD_MUTATION_TEST) $(HOST_HDD_REPAIR_FIXTURE_TEST)

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

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(IRX_FILES:.irx=_irx.c) $(HOST_TESTS)
	rm -rf $(HOST_HDD_FIXTURE_DIR) $(HOST_FORENSIC_FIXTURE_DIR)

main.o: src/main.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

manager_menu_ps2.o: src/manager_menu_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

app_ui_ps2.o: src/app_ui_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

disk_status_ps2.o: src/disk_status_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

app_error.o: src/app_error.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

bootstrap_controller_ps2.o: src/bootstrap_controller_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

diagnostics_controller_ps2.o: src/diagnostics_controller_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

forensic_controller_ps2.o: src/forensic_controller_ps2.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

platform.o: src/platform.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

storage.o: src/storage.c
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

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

PS2_GOALS := $(filter-out test-host clean,$(MAKECMDGOALS))
ifeq ($(strip $(MAKECMDGOALS)),)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
else ifneq ($(strip $(PS2_GOALS)),)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
endif

.PHONY: all release test-host clean
