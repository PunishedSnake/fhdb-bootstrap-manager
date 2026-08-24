#!/bin/sh
set -eu

CC=${EE_CC:-mips64r5900el-ps2-elf-gcc}
AS=${EE_AS:-mips64r5900el-ps2-elf-as}
LD=${EE_LD:-mips64r5900el-ps2-elf-ld}
OUT=${1:-GCC_R5900_TARGET.txt}
TMP_OBJ=${TMPDIR:-/tmp}/r5900-target-probe.o

{
    echo "PS2 HDD Bootstrap Manager - R5900 toolchain audit"
    echo
    echo "== compiler identity =="
    "$CC" --version | head -n 1
    printf 'dumpmachine: '
    "$CC" -dumpmachine
    printf 'dumpversion: '
    "$CC" -dumpfullversion -dumpversion
    echo

    echo "== effective GCC target options at -O2 =="
    "$CC" -Q -O2 --help=target -x c -c /dev/null -o "$TMP_OBJ" 2>&1
    rm -f "$TMP_OBJ"
    echo

    echo "== predefined target macros =="
    "$CC" -dM -E -x c /dev/null 2>/dev/null | \
        grep -E '(^#define (__mips|_MIPS|__ps2sdk__|R5900|__R5900))' || true
    echo

    echo "== assembler identity =="
    "$AS" --version | head -n 2
    echo
    echo "== linker identity =="
    "$LD" --version | head -n 2
} > "$OUT"
