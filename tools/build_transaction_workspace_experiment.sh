#!/bin/sh
set -eu

# Build the active isolated Phase-5 incremental experiment without changing
# default runtime sources. CI #724 remains frozen workspace v1 and CI #739 is
# the frozen workspace-v1 + source-fingerprint-malloc reference.
#
# Active experiment additionally removes explicit 64-byte alignment from two
# 1024-byte storage scratch buffers whose only consumers are ordinary fileXio
# reads plus EE CPU parse/memcmp. No direct DMA, libpad or hdl0: buffer changes.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BACKUP=$(mktemp -d)
TRANSACTION="$ROOT/src/hdl_tools/transaction.inc"
SOURCE_UI="$ROOT/src/hdl_tools/source_ui.inc"
HEADER_BACKUP="$ROOT/src/header_backup.c"
REPAIR_SNAPSHOT="$ROOT/src/repair_snapshot.c"

restore_sources() {
    for name in transaction.inc source_ui.inc header_backup.c repair_snapshot.c; do
        if [ -f "$BACKUP/$name" ]; then
            case "$name" in
                transaction.inc) cp "$BACKUP/$name" "$TRANSACTION" ;;
                source_ui.inc) cp "$BACKUP/$name" "$SOURCE_UI" ;;
                header_backup.c) cp "$BACKUP/$name" "$HEADER_BACKUP" ;;
                repair_snapshot.c) cp "$BACKUP/$name" "$REPAIR_SNAPSHOT" ;;
            esac
        fi
    done
    rm -rf "$BACKUP"
}
trap restore_sources EXIT HUP INT TERM

cp "$TRANSACTION" "$BACKUP/transaction.inc"
cp "$SOURCE_UI" "$BACKUP/source_ui.inc"
cp "$HEADER_BACKUP" "$BACKUP/header_backup.c"
cp "$REPAIR_SNAPSHOT" "$BACKUP/repair_snapshot.c"

python3 "$ROOT/tools/materialize_transaction_workspace.py" \
    "$TRANSACTION" "$TRANSACTION"
python3 "$ROOT/tools/materialize_source_fingerprint_malloc.py" \
    "$SOURCE_UI"
python3 "$ROOT/tools/materialize_storage_scratch_natural_alignment.py" \
    "$HEADER_BACKUP" "$REPAIR_SNAPSHOT"

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
hdl_transaction_workspace_version: "1"
hdl_transaction_workspace_materializer: "tools/materialize_transaction_workspace.py"
hdl_transaction_workspace_bytes: "65536"
hdl_transaction_workspace_alignment: "64"
hdl_source_fingerprint_heap_experiment: "malloc"
hdl_source_fingerprint_alignment_requirement: "ordinary-fileXio-no-64B-contract"
storage_scratch_alignment_experiment: "natural"
storage_scratch_alignment_sites: "header_backup.backup_scratch repair_snapshot.snapshot_verify"
EOF
}

cd "$ROOT"
build_variant 0 OFF
build_variant 1 ON
