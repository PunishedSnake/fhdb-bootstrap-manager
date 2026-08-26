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

case "$HDL_PROFILE_VALUE" in
    0|1) ;;
    *)
        printf 'HDL_PROFILE must be 0 or 1, got %s\n' "$HDL_PROFILE_VALUE" >&2
        exit 2
        ;;
esac

# A development environment may preserve the ps2sdk .git directory. Prefer the
# exact installed checkout when available. Tagged ps2dev Docker images strip
# source metadata after installation, so CI passes the source ref/SHA that the
# image build scripts selected instead of pretending an absent .git means an
# unknown software stack.
if [ "${PS2SDK:-}" != "" ] && git -C "$PS2SDK" rev-parse HEAD >/dev/null 2>&1; then
    PS2SDK_SHA=$(git -C "$PS2SDK" rev-parse HEAD)
    PS2SDK_REF=$(git -C "$PS2SDK" describe --always --tags 2>/dev/null || printf '%s' "$PS2SDK_REF")
fi

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
build_flags: "-O2 -flto -G0 -fdata-sections -ffunction-sections -DHDL_PROFILE_ENABLED=$HDL_PROFILE_VALUE; ld --gc-sections"
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
