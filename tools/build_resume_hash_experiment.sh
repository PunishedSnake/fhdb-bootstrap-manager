#!/bin/sh
set -eu

# Build the resume-hash experiment without letting its alternate fragments
# become the default source tree. The frozen PROFILE pair is built and validated
# separately before this script is run in CI.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BACKUP=$(mktemp -d)
SOURCE_UI="$ROOT/src/hdl_tools/source_ui.inc"
TRANSACTION="$ROOT/src/hdl_tools/transaction.inc"

restore_sources() {
    if [ -f "$BACKUP/source_ui.inc" ]; then
        cp "$BACKUP/source_ui.inc" "$SOURCE_UI"
    fi
    if [ -f "$BACKUP/transaction.inc" ]; then
        cp "$BACKUP/transaction.inc" "$TRANSACTION"
    fi
    rm -rf "$BACKUP"
}
trap restore_sources EXIT HUP INT TERM

cp "$SOURCE_UI" "$BACKUP/source_ui.inc"
cp "$TRANSACTION" "$BACKUP/transaction.inc"
cp "$ROOT/src/hdl_tools/source_ui_resume_hash.inc" "$SOURCE_UI"
cp "$ROOT/src/hdl_tools/transaction_resume_hash.inc" "$TRANSACTION"

cd "$ROOT"
make clean
make HDL_PROFILE=0 HDL_RESUME_HASH_CHECKPOINT=1
python3 tools/optimization_audit.py \
    --elf PS2_HDD_BOOTSTRAP_MANAGER.ELF \
    --output OPTIMIZATION_AUDIT_RESUME_HASH.txt
make HDL_PROFILE=0 HDL_RESUME_HASH_CHECKPOINT=1 release
cp PS2_HDD_BOOTSTRAP_MANAGER.ELF PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF
cp PS2_HDD_BOOTSTRAP_MANAGER.map PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.map
sha256sum PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF \
    > PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF.sha256
wc -c PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF \
    > PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF.size
