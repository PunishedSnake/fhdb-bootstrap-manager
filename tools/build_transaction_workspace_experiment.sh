#!/bin/sh
set -eu

# Build the active isolated Phase-5 incremental experiment without changing
# default runtime sources. Frozen references:
#   workspace v1                        CI #724
#   workspace v1 + fingerprint malloc   CI #739
#   bounded HDDMETA read-back v1        CI #749
#
# Active bounded-v2 experiment keeps the 64 KiB exact read-back scratch but
# removes the two seek RPCs used by v1. It reads exactly the expected bytes and
# then requires one extra byte read to report EOF, preserving truncation and
# trailing-data detection. An independent APAMETA1 reference vector is validated
# before materializing the runtime experiment so future streaming work has a
# byte-exact oracle that does not share the runtime implementation.
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

cd "$ROOT"
python3 tools/forensic_snapshot_reference.py --selftest

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
forensic_snapshot_bounded_verify_version: "2"
forensic_snapshot_verify_chunk_bytes: "65536"
forensic_snapshot_verify_policy: "exact-byte-compare"
forensic_snapshot_size_check: "exact-bytes-plus-eof-read"
forensic_snapshot_seek_rpcs_per_verify: "0"
forensic_snapshot_reference_format: "APAMETA1"
forensic_snapshot_reference_image_sha256: "601ba74fc619738dac19baa2a6cb53054b67803e00b1fccb6bf89c69ef4bab6f"
EOF
}

build_variant 0 OFF
build_variant 1 ON
