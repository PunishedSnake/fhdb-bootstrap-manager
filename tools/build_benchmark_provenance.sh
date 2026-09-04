#!/bin/sh
set -eu

OUT=${1:-BENCHMARK_PROVENANCE.yml}
CC=${EE_CC:-mips64r5900el-ps2-elf-gcc}
GIT_SHA=${PROJECT_GIT_SHA:-$(git rev-parse HEAD 2>/dev/null || printf 'unavailable')}
GIT_REF=${PROJECT_GIT_REF:-$(git rev-parse --abbrev-ref HEAD 2>/dev/null || printf 'unavailable')}
CC_TARGET=$($CC -dumpmachine 2>/dev/null || printf 'unavailable')
CC_VERSION=$($CC -dumpfullversion -dumpversion 2>/dev/null || printf 'unavailable')
PS2SDK_PATH_VALUE=${PS2SDK:-unavailable}
PS2SDK_REF=${PS2SDK_SOURCE_REF:-unavailable}
PS2SDK_SHA=${PS2SDK_SOURCE_SHA:-unavailable}
PS2DEV_BUNDLE_REF=${PS2DEV_BUNDLE_REF:-unavailable}
HDL_PROFILE_VALUE=${HDL_PROFILE:-1}
HDL_RESUME_HASH_CHECKPOINT_VALUE=${HDL_RESUME_HASH_CHECKPOINT:-0}
BENCHMARK_ELF_PATH=${BENCHMARK_ELF:-PS2_HDD_BOOTSTRAP_MANAGER.ELF}
HDL_STREAM_IRX_PATH=${HDL_STREAM_IRX:-hdl_stream.irx}

case "$HDL_PROFILE_VALUE" in
    0|1) ;;
    *)
        printf 'HDL_PROFILE must be 0 or 1, got %s\n' "$HDL_PROFILE_VALUE" >&2
        exit 2
        ;;
esac

case "$HDL_RESUME_HASH_CHECKPOINT_VALUE" in
    0|1) ;;
    *)
        printf 'HDL_RESUME_HASH_CHECKPOINT must be 0 or 1, got %s\n' \
            "$HDL_RESUME_HASH_CHECKPOINT_VALUE" >&2
        exit 2
        ;;
esac

if [ "$HDL_RESUME_HASH_CHECKPOINT_VALUE" = "1" ]; then
    RESUME_HASH_EE_FLAG=" -DHDL_RESUME_HASH_CHECKPOINT_ENABLED=1"
else
    RESUME_HASH_EE_FLAG=""
fi

# A development environment may preserve the ps2sdk .git directory. Prefer the
# exact installed checkout when available. Tagged ps2dev Docker images strip
# source metadata after installation, so CI passes the source ref/SHA that the
# image build scripts selected instead of pretending an absent .git means an
# unknown software stack.
if [ "${PS2SDK:-}" != "" ] && git -C "$PS2SDK" rev-parse HEAD >/dev/null 2>&1; then
    PS2SDK_SHA=$(git -C "$PS2SDK" rev-parse HEAD)
    PS2SDK_REF=$(git -C "$PS2SDK" describe --always --tags 2>/dev/null || printf '%s' "$PS2SDK_REF")
fi

file_sha256()
{
    if [ -f "$1" ]; then
        sha256sum "$1" | awk '{print $1}'
    else
        printf 'UNRECORDED'
    fi
}

file_bytes()
{
    if [ -f "$1" ]; then
        wc -c < "$1" | tr -d ' '
    else
        printf 'UNRECORDED'
    fi
}

BENCHMARK_ELF_SHA=$(file_sha256 "$BENCHMARK_ELF_PATH")
BENCHMARK_ELF_BYTES=$(file_bytes "$BENCHMARK_ELF_PATH")
HDL_STREAM_IRX_SHA=$(file_sha256 "$HDL_STREAM_IRX_PATH")
HDL_STREAM_IRX_BYTES=$(file_bytes "$HDL_STREAM_IRX_PATH")

cat > "$OUT" <<EOF
# Build-side half of the corpus-v2 benchmark record.
# Hardware/workload fields are deliberately left UNRECORDED until a real PS2
# run supplies them. Never infer an SCPH or adapter from the build machine.
project: fhdb-bootstrap-manager
project_git_sha: "$GIT_SHA"
project_git_ref: "$GIT_REF"
console_scp: UNRECORDED
hardware_revision: UNRECORDED
romver: UNRECORDED
network_adapter: UNRECORDED
storage_adapter: UNRECORDED
ps2dev_bundle_ref: "$PS2DEV_BUNDLE_REF"
ps2sdk_path: "$PS2SDK_PATH_VALUE"
ps2sdk_ref: "$PS2SDK_REF"
ps2sdk_commit: "$PS2SDK_SHA"
toolchain_target: "$CC_TARGET"
toolchain_gcc: "$CC_VERSION"
toolchain_container: "ps2dev/ps2dev:v2.0.0"
hdl_profile_enabled: "$HDL_PROFILE_VALUE"
hdl_resume_hash_checkpoint_enabled: "$HDL_RESUME_HASH_CHECKPOINT_VALUE"
build_flags: "EE: -O2 -flto -G0 -fdata-sections -ffunction-sections -DHDL_PROFILE_ENABLED=$HDL_PROFILE_VALUE$RESUME_HASH_EE_FLAG; ld --gc-sections; hdl_stream IOP: PS2SDK -Os baseline with appended -O2 -DHDL_PROFILE_ENABLED=$HDL_PROFILE_VALUE"
benchmark_elf: "$BENCHMARK_ELF_PATH"
benchmark_elf_sha256: "$BENCHMARK_ELF_SHA"
benchmark_elf_bytes: "$BENCHMARK_ELF_BYTES"
hdl_stream_irx: "$HDL_STREAM_IRX_PATH"
hdl_stream_irx_sha256: "$HDL_STREAM_IRX_SHA"
hdl_stream_irx_bytes: "$HDL_STREAM_IRX_BYTES"
active_irx: UNRECORDED
embedded_irx: "iomanX fileXio secrman freesio2 freepad mcman mcserv secrsif poweroff bdm bdmfs_fatfs usbd usbmass_bd ps2dev9 ata_bd ps2fs ps2hdd-bdm hdl_stream"
workload: UNRECORDED
direction: UNRECORDED
buffering: UNRECORDED
alignment: UNRECORDED
sample_count: UNRECORDED
units: UNRECORDED
correctness_hash: UNRECORDED
p50: UNRECORDED
p95: UNRECORDED
p99: UNRECORDED
max: UNRECORDED
deadline_misses: UNRECORDED
EOF
