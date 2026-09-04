#!/bin/sh
set -eu

# Build the resume-hash experiment without letting its alternate fragments
# become the default source tree. The frozen PROFILE pair is built and validated
# separately before this script is run in CI.
#
# Build both profiler modes so hardware work can keep one variable per A/B:
#   frozen PROFILE OFF vs resume-hash PROFILE OFF -> release-like timing
#   frozen PROFILE ON  vs resume-hash PROFILE ON  -> identical telemetry
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
python3 "$ROOT/tools/materialize_resume_hash_transaction.py" \
    "$ROOT/src/hdl_tools/transaction_resume_hash.inc" "$TRANSACTION"

build_variant()
{
    profile=$1
    label=$2
    elf="PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_${label}.ELF"
    map="PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_${label}.map"
    irx="HDL_STREAM_RESUME_HASH_PROFILE_${label}.irx"
    audit="OPTIMIZATION_AUDIT_RESUME_HASH_PROFILE_${label}.txt"
    provenance="BENCHMARK_PROVENANCE_RESUME_HASH_PROFILE_${label}.yml"

    make clean
    # Root clean removes the public hdl_stream.irx but deliberately does not
    # invoke the nested IOP Makefile. Its profile-specific objects and the
    # absolute-path notiopmod intermediates can therefore survive and make a
    # subsequent PROFILE variant reuse the wrong linked IRX. Clean the actual
    # producer explicitly so OFF/ON experiment builds are independent.
    make -C iop/hdl_stream clean \
        IOP_BIN="$ROOT/hdl_stream.irx" \
        HDL_PROFILE="$profile"
    make HDL_PROFILE="$profile" HDL_RESUME_HASH_CHECKPOINT=1
    cp hdl_stream.irx "$irx"
    python3 tools/optimization_audit.py \
        --elf PS2_HDD_BOOTSTRAP_MANAGER.ELF \
        --output "$audit"
    make HDL_PROFILE="$profile" HDL_RESUME_HASH_CHECKPOINT=1 release
    cp PS2_HDD_BOOTSTRAP_MANAGER.ELF "$elf"
    cp PS2_HDD_BOOTSTRAP_MANAGER.map "$map"
    sha256sum "$elf" > "$elf.sha256"
    wc -c "$elf" > "$elf.size"
    HDL_PROFILE="$profile" \
    HDL_RESUME_HASH_CHECKPOINT=1 \
    BENCHMARK_ELF="$elf" \
    HDL_STREAM_IRX="$irx" \
        sh tools/build_benchmark_provenance.sh "$provenance"
}

cd "$ROOT"
build_variant 0 OFF
build_variant 1 ON

# Preserve the original experiment artifact names as PROFILE OFF aliases while
# the benchmark documentation/tools migrate to the explicit pair names.
cp PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_OFF.ELF \
   PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF
cp PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_OFF.map \
   PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.map
cp PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_OFF.ELF.sha256 \
   PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF.sha256
cp PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_OFF.ELF.size \
   PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH.ELF.size
cp OPTIMIZATION_AUDIT_RESUME_HASH_PROFILE_OFF.txt \
   OPTIMIZATION_AUDIT_RESUME_HASH.txt
