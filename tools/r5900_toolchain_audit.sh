#!/bin/sh
set -eu

CC=${EE_CC:-mips64r5900el-ps2-elf-gcc}
AS=${EE_AS:-mips64r5900el-ps2-elf-as}
LD=${EE_LD:-mips64r5900el-ps2-elf-ld}
OUT=${1:-GCC_R5900_TARGET.txt}
TMP_BASE=${TMPDIR:-/tmp}/r5900-target-probe
TMP_C=${TMP_BASE}.c
TMP_OBJ=${TMP_BASE}.o
TMP_DEFAULT=${TMP_BASE}-default.s
TMP_EXPLICIT=${TMP_BASE}-explicit.s

cat > "$TMP_C" <<'EOF'
unsigned int r5900_schedule_probe(const unsigned int *a,
                                  const unsigned int *b,
                                  unsigned int count)
{
    unsigned int x = 0x13579bdfu;
    unsigned int y = 0x2468ace0u;
    unsigned int i;

    for (i = 0; i < count; ++i) {
        unsigned int av = a[i];
        unsigned int bv = b[i];
        x = (x + av) ^ (bv << (i & 7u));
        y = (y ^ bv) + (av >> ((i + 1u) & 7u));
    }
    return x ^ y;
}
EOF

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

    echo "== driver expansion for a normal -O2 compile =="
    "$CC" -### -O2 -c "$TMP_C" -o "$TMP_OBJ" 2>&1 || true
    rm -f "$TMP_OBJ"
    echo

    echo "== default target vs explicit R5900 scheduling probe =="
    "$CC" -O2 -G0 -S "$TMP_C" -o "$TMP_DEFAULT"
    "$CC" -O2 -G0 -march=r5900 -mtune=r5900 -mfix-r5900 \
        -S "$TMP_C" -o "$TMP_EXPLICIT"
    if cmp -s "$TMP_DEFAULT" "$TMP_EXPLICIT"; then
        echo "IDENTICAL: default PS2DEV target emits the same probe assembly as explicit -march=r5900 -mtune=r5900 -mfix-r5900"
    else
        echo "DIFFERENT: explicit R5900 flags change the scheduling probe; inspect the following unified diff"
        diff -u "$TMP_DEFAULT" "$TMP_EXPLICIT" || true
    fi
    echo

    echo "== assembler identity =="
    "$AS" --version | head -n 2
    echo
    echo "== linker identity =="
    "$LD" --version | head -n 2
} > "$OUT"

rm -f "$TMP_C" "$TMP_OBJ" "$TMP_DEFAULT" "$TMP_EXPLICIT"
