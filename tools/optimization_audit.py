#!/usr/bin/env python3
"""Static optimization report for the unstripped PS2 EE ELF.

The report is intentionally compiler-output based. It tells us what survived LTO,
which functions dominate the 16 KiB R5900 I-cache budget, whether expensive 64-bit
runtime helpers leaked into hot code, and whether two named functions still contain
byte-identical instruction streams after optimization.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


FUNCTION_TYPES = set("tTwW")
EXPENSIVE_HELPERS = (
    "__divdi3",
    "__udivdi3",
    "__moddi3",
    "__umoddi3",
    "__muldi3",
    "__ashldi3",
    "__ashrdi3",
    "__lshrdi3",
)


@dataclass
class Function:
    name: str
    address: int = 0
    size: int = 0
    words: list[str] | None = None
    mnemonics: collections.Counter[str] | None = None


def run(command: list[str]) -> str:
    process = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if process.returncode != 0:
        raise RuntimeError(
            f"command failed ({process.returncode}): {' '.join(command)}\n"
            f"{process.stderr.strip()}"
        )
    return process.stdout


def find_tool(explicit: str | None, candidates: tuple[str, ...]) -> str:
    if explicit:
        if shutil.which(explicit) is None:
            raise RuntimeError(f"tool not found: {explicit}")
        return explicit
    for candidate in candidates:
        path = shutil.which(candidate)
        if path is not None:
            return path
    raise RuntimeError(f"none of these tools were found: {', '.join(candidates)}")


def parse_nm(text: str) -> dict[str, Function]:
    functions: dict[str, Function] = {}
    pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)$"
    )
    for line in text.splitlines():
        match = pattern.match(line.strip())
        if not match or match.group(3) not in FUNCTION_TYPES:
            continue
        address = int(match.group(1), 16)
        size = int(match.group(2), 16)
        name = match.group(4).strip()
        functions[name] = Function(name=name, address=address, size=size)
    return functions


def parse_disassembly(text: str, functions: dict[str, Function]) -> collections.Counter[str]:
    header = re.compile(r"^[0-9a-fA-F]+\s+<(.+)>:$")
    instruction = re.compile(
        r"^\s*[0-9a-fA-F]+:\s+([0-9a-fA-F]{8})\s+([.$A-Za-z_][.$A-Za-z0-9_]*)"
    )
    current: Function | None = None
    global_mnemonics: collections.Counter[str] = collections.Counter()

    for line in text.splitlines():
        match = header.match(line)
        if match:
            name = match.group(1)
            current = functions.get(name)
            if current is None:
                current = Function(name=name)
                functions[name] = current
            current.words = []
            current.mnemonics = collections.Counter()
            continue

        match = instruction.match(line)
        if match is None or current is None:
            continue
        word = match.group(1).lower()
        mnemonic = match.group(2).lower()
        current.words.append(word)
        current.mnemonics[mnemonic] += 1
        global_mnemonics[mnemonic] += 1

    return global_mnemonics


def duplicate_groups(functions: dict[str, Function]) -> list[list[Function]]:
    fingerprints: dict[tuple[int, str], list[Function]] = collections.defaultdict(list)
    for function in functions.values():
        if function.words is None or len(function.words) < 4:
            continue
        raw = "".join(function.words).encode("ascii")
        digest = hashlib.sha256(raw).hexdigest()
        fingerprints[(len(function.words), digest)].append(function)
    groups = [group for group in fingerprints.values() if len(group) > 1]
    groups.sort(key=lambda group: (len(group[0].words or []), len(group)), reverse=True)
    return groups


def format_bytes(value: int) -> str:
    if value >= 1024:
        return f"{value / 1024.0:.2f} KiB"
    return f"{value} B"


def write_report(
    path: Path,
    elf: Path,
    functions: dict[str, Function],
    global_mnemonics: collections.Counter[str],
    nm_text: str,
) -> None:
    sized = [function for function in functions.values() if function.size > 0]
    sized.sort(key=lambda function: function.size, reverse=True)
    text_bytes = sum(function.size for function in sized)
    duplicates = duplicate_groups(functions)

    helper_hits = [helper for helper in EXPENSIVE_HELPERS if helper in nm_text]
    all_instruction_count = sum(global_mnemonics.values())

    lines: list[str] = []
    lines.append("PS2 HDD Bootstrap Manager - compiler optimization audit")
    lines.append(f"ELF: {elf}")
    lines.append(f"named text functions: {len(sized)}")
    lines.append(f"sum of named function sizes: {text_bytes} bytes ({format_bytes(text_bytes)})")
    lines.append(f"disassembled instructions: {all_instruction_count}")
    lines.append("")
    lines.append("R5900 cache context")
    lines.append("  I-cache: 16 KiB, 64-byte lines, 2-way associative")
    lines.append("  D-cache:  8 KiB, 64-byte lines, 2-way associative")
    lines.append("  Audit rule: avoid enlarging hot code blindly; optimize measured loops and passes.")
    lines.append("")

    lines.append("Largest functions")
    for function in sized[:40]:
        instruction_count = len(function.words or [])
        pressure = "  [>25% I-cache]" if function.size > 4096 else ""
        lines.append(
            f"  {function.size:6d} B  {instruction_count:5d} insn  {function.name}{pressure}"
        )
    lines.append("")

    lines.append("Most common emitted instructions")
    for mnemonic, count in global_mnemonics.most_common(30):
        percent = (count * 100.0 / all_instruction_count) if all_instruction_count else 0.0
        lines.append(f"  {mnemonic:12s} {count:7d}  {percent:6.2f}%")
    lines.append("")

    lines.append("Exact duplicate machine-code functions")
    if not duplicates:
        lines.append("  none (for functions with at least four instructions)")
    else:
        for group in duplicates[:50]:
            lines.append(
                f"  {len(group[0].words or []):5d} insn x {len(group):2d}: "
                + ", ".join(function.name for function in group)
            )
    lines.append("")

    lines.append("Potentially expensive 64-bit libgcc helpers")
    if helper_hits:
        for helper in helper_hits:
            lines.append(f"  PRESENT: {helper}")
    else:
        lines.append("  none of the watched helpers are defined in the final EE ELF")
    lines.append("")

    lines.append("Large per-function instruction mixes")
    for function in sized[:20]:
        if not function.mnemonics:
            continue
        mix = ", ".join(
            f"{mnemonic}={count}"
            for mnemonic, count in function.mnemonics.most_common(8)
        )
        lines.append(f"  {function.name}: {mix}")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--nm")
    parser.add_argument("--objdump")
    args = parser.parse_args()

    if not args.elf.is_file():
        print(f"ELF not found: {args.elf}", file=sys.stderr)
        return 2

    try:
        nm = find_tool(
            args.nm,
            (
                "mips64r5900el-ps2-elf-nm",
                "mips64r5900el-none-elf-nm",
                "mipsel-none-elf-nm",
                "nm",
            ),
        )
        objdump = find_tool(
            args.objdump,
            (
                "mips64r5900el-ps2-elf-objdump",
                "mips64r5900el-none-elf-objdump",
                "mipsel-none-elf-objdump",
                "objdump",
            ),
        )
        nm_text = run([nm, "-S", "--size-sort", "--radix=x", "--defined-only", str(args.elf)])
        disassembly = run([objdump, "-d", "-w", str(args.elf)])
        functions = parse_nm(nm_text)
        mnemonics = parse_disassembly(disassembly, functions)
        write_report(args.output, args.elf, functions, mnemonics, nm_text)
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
