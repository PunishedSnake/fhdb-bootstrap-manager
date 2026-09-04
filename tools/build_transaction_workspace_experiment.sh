#!/bin/sh
set -eu

# Build the active isolated Phase-5 incremental experiment without changing
# default runtime sources. Frozen references:
#   workspace v1                        CI #724
#   workspace v1 + fingerprint malloc   CI #739
#
# Active experiment additionally bounds forensic HDDMETA read-back verification
# scratch to 64 KiB while preserving exact byte-for-byte comparison.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BACKUP=$(mktemp -d)
TRANSACTION="$ROOT/src/hdl_tools/transaction.inc"
SOURCE_UI="$ROOT/src/hdl_tools/source_ui.inc"
FORENSIC_SNAPSHOT="$ROOT/src/forensic_snapshot.c"

restore_sources() {
    if [ -f "$BACKUP/transaction.inc" ]; then
        cp "$BACKUP/transaction.inc" "$TRANSACTION"
    fi
    if [ -f "$BACKUP/source_ui.inc" ]; then
        cp "$BACKUP/source_ui.inc" "$SOURCE_UI"
    fi
    if [ -f "$BACKUP/forensic_snapshot.c" ]; then
        cp "$BACKUP/forensic_snapshot.c" "$FORENSIC_SNAPSHOT"
    fi
    rm -rf "$BACKUP"
}
trap restore_sources EXIT HUP INT TERM

cp "$TRANSACTION" "$BACKUP/transaction.inc"
cp "$SOURCE_UI" "$BACKUP/source_ui.inc"
cp "$FORENSIC_SNAPSHOT" "$BACKUP/forensic_snapshot.c"

python3 "$ROOT/tools/materialize_transaction_workspace.py" \
    "$TRANSACTION" "$TRANSACTION"
python3 "$ROOT/tools/materialize_source_fingerprint_malloc.py" \
    "$SOURCE_UI"
python3 "$ROOT/tools/materialize_forensic_snapshot_bounded_verify.py" \
    "$FORENSIC_SNAPSHOT"

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
hdl_transaction_workspace_bytes: "65536"
hdl_transaction_workspace_alignment: "64"
hdl_source_fingerprint_heap_experiment: "malloc"
forensic_snapshot_bounded_verify_enabled: "1"
forensic_snapshot_verify_chunk_bytes: "65536"
forensic_snapshot_verify_policy: "exact-byte-compare"
EOF
}

cd "$ROOT"
build_variant 0 OFF
build_variant 1 ON
