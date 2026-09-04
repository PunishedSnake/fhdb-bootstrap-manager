#!/bin/sh
set -eu

# Build the isolated Phase-5 transaction-workspace experiment without changing
# the default runtime source. The frozen PROFILE pair remains the baseline.
#
# The experiment changes only EE ownership of the existing 64 KiB / 64-byte
# aligned transaction I/O buffer. IOP code and transport are expected to remain
# byte-identical to the corresponding frozen PROFILE variant.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BACKUP=$(mktemp -d)
TRANSACTION="$ROOT/src/hdl_tools/transaction.inc"

restore_sources() {
    if [ -f "$BACKUP/transaction.inc" ]; then
        cp "$BACKUP/transaction.inc" "$TRANSACTION"
    fi
    rm -rf "$BACKUP"
}
trap restore_sources EXIT HUP INT TERM

cp "$TRANSACTION" "$BACKUP/transaction.inc"
python3 "$ROOT/tools/materialize_transaction_workspace.py" \
    "$TRANSACTION" "$TRANSACTION"

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
hdl_transaction_workspace_materializer: "tools/materialize_transaction_workspace.py"
hdl_transaction_workspace_bytes: "65536"
hdl_transaction_workspace_alignment: "64"
EOF
}

cd "$ROOT"
build_variant 0 OFF
build_variant 1 ON
