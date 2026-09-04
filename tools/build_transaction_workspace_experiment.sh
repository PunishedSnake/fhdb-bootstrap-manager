#!/bin/sh
set -eu

# Build the active isolated Phase-5 transaction-workspace experiment without
# changing default runtime sources. V1 is frozen at CI #724. Active v2 extends
# ownership backwards through execute_transaction() source admission so its
# fingerprint, COPY/source-hash and HDD verify phases borrow one 64 KiB /
# 64-byte-aligned EE workspace. The pre-confirmation UI fingerprint remains
# helper-owned and short-lived.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BACKUP=$(mktemp -d)
TRANSACTION="$ROOT/src/hdl_tools/transaction.inc"
SOURCE_UI="$ROOT/src/hdl_tools/source_ui.inc"

restore_sources() {
    if [ -f "$BACKUP/transaction.inc" ]; then
        cp "$BACKUP/transaction.inc" "$TRANSACTION"
    fi
    if [ -f "$BACKUP/source_ui.inc" ]; then
        cp "$BACKUP/source_ui.inc" "$SOURCE_UI"
    fi
    rm -rf "$BACKUP"
}
trap restore_sources EXIT HUP INT TERM

cp "$TRANSACTION" "$BACKUP/transaction.inc"
cp "$SOURCE_UI" "$BACKUP/source_ui.inc"
python3 "$ROOT/tools/materialize_transaction_workspace_v2.py" \
    "$TRANSACTION" "$SOURCE_UI"

# Record the full source-level allocation inventory while v2 is materialized.
# The transaction should now have one 64 KiB owner instead of separate source
# admission, bulk-copy/source-hash and target-verify allocation lifetimes.
python3 "$ROOT/tools/allocation_inventory.py" \
    --root "$ROOT" \
    --output "$ROOT/ALLOCATION_INVENTORY_TX_WORKSPACE.json"

build_variant()
{
    profile=$1
    label=$2
    elf="PS2_HDD_BOOTSTRAP_MANAGER_TX_WORKSPACE_PROFILE_${label}.ELF"
    map="PS2_HDD_BOOTSTRAP_MANAGER_TX_WORKSPACE_PROFILE_${label}.map"
    irx="HDL_STREAM_TX_WORKSPACE_PROFILE_${label}.irx"
    audit="OPTIMIZATION_AUDIT_TX_WORKSPACE_PROFILE_${label}.txt"
    provenance="BENCHMARK_PROVENANCE_TX_WORKSPACE_PROFILE_${label}.yml"

    make clean
    make -C iop/hdl_stream clean \
        IOP_BIN="$ROOT/hdl_stream.irx" \
        HDL_PROFILE="$profile"
    make HDL_PROFILE="$profile"
    cp hdl_stream.irx "$irx"
    python3 tools/optimization_audit.py \
        --elf PS2_HDD_BOOTSTRAP_MANAGER.ELF \
        --output "$audit"
    make HDL_PROFILE="$profile" release
    cp PS2_HDD_BOOTSTRAP_MANAGER.ELF "$elf"
    cp PS2_HDD_BOOTSTRAP_MANAGER.map "$map"
    sha256sum "$elf" > "$elf.sha256"
    wc -c "$elf" | awk '{print $1}' > "$elf.size"
    HDL_PROFILE="$profile" \
    BENCHMARK_ELF="$elf" \
    HDL_STREAM_IRX="$irx" \
        sh tools/build_benchmark_provenance.sh "$provenance"
    cat >> "$provenance" <<EOF
hdl_transaction_workspace_enabled: "1"
hdl_transaction_workspace_version: "2"
hdl_transaction_workspace_materializer: "tools/materialize_transaction_workspace_v2.py"
hdl_transaction_workspace_bytes: "65536"
hdl_transaction_workspace_alignment: "64"
hdl_transaction_workspace_source_admission: "borrowed"
hdl_preconfirmation_fingerprint_allocation: "unchanged"
EOF
}

cd "$ROOT"
build_variant 0 OFF
build_variant 1 ON
