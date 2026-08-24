#!/usr/bin/env python3
"""Static optimization report for the unstripped PlayStation 2 EE ELF.

The report is compiler-output based. It inspects what actually survived LTO rather
than assuming source-level duplication becomes machine-code duplication. It tracks
R5900 I-cache pressure, instruction mix, exact duplicate functions, costly libc/libgcc
call sites, and source lines that can pull heavyweight formatting/scanning machinery.
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
    "__divdi3", "__udivdi3", "__moddi3", "__umoddi3", "__muldi3",
    "__ashldi3", "__ashrdi3", "__lshrdi3",
)
HEAVY_LIBC = ("__ssvfiscanf_r", "_svfprintf_r", "_vfiprintf_r", "_dtoa_r")
SOURCE_PATTERNS = (
    ("scanf-family", re.compile(r"\b(?:s|f|v)?scanf\s*\(")),
    ("sprintf-family", re.compile(r"\b(?:s|v)?(?:n)?printf\s*\(")),
    ("floating-format", re.compile(r"%[-+ #0-9.*hlLjzt]*[aAeEfFgG]")),
)

@dataclass
class Function:
    name: str
    address: int = 0
    size: int = 0
    words: list[str] | None = None
    mnemonics: collections.Counter[str] | None = None


def run(command: list[str]) -> str:
    process = subprocess.run(command, check=False, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    if process.returncode != 0:
        raise RuntimeError(f"command failed ({process.returncode}): {' '.join(command)}\n{process.stderr.strip()}")
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
    pattern = re.compile(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)$")
    for line in text.splitlines():
        match = pattern.match(line.strip())
        if not match or match.group(3) not in FUNCTION_TYPES:
            continue
        name = match.group(4).strip()
        functions[name] = Function(name=name, address=int(match.group(1), 16), size=int(match.group(2), 16))
    return functions


def parse_disassembly(text: str, functions: dict[str, Function]):
    header = re.compile(r"^[0-9a-fA-F]+\s+<(.+)>:$")
    instruction = re.compile(r"^\s*[0-9a-fA-F]+:\s+([0-9a-fA-F]{8})\s+([.$A-Za-z_][.$A-Za-z0-9_]*)(.*)$")
    target = re.compile(r"<([^>]+)>")
    current: Function | None = None
    global_mnemonics: collections.Counter[str] = collections.Counter()
    calls: dict[str, collections.Counter[str]] = collections.defaultdict(collections.Counter)
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
        word, mnemonic, operands = match.groups()
        mnemonic = mnemonic.lower()
        current.words.append(word.lower())
        current.mnemonics[mnemonic] += 1
        global_mnemonics[mnemonic] += 1
        if mnemonic in ("jal", "jalr"):
            called = target.search(operands)
            if called:
                calls[called.group(1)][current.name] += 1
    return global_mnemonics, calls


def duplicate_groups(functions: dict[str, Function]) -> list[list[Function]]:
    fingerprints: dict[tuple[int, str], list[Function]] = collections.defaultdict(list)
    for function in functions.values():
        if function.words is None or len(function.words) < 4:
            continue
        raw = "".join(function.words).encode("ascii")
        fingerprints[(len(function.words), hashlib.sha256(raw).hexdigest())].append(function)
    groups = [group for group in fingerprints.values() if len(group) > 1]
    groups.sort(key=lambda group: (len(group[0].words or []), len(group)), reverse=True)
    return groups


def scan_sources(root: Path):
    hits: dict[str, list[str]] = collections.defaultdict(list)
    for directory in (root / "src", root / "include"):
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.suffix not in (".c", ".h", ".inc"):
                continue
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except UnicodeDecodeError:
                continue
            for number, line in enumerate(lines, 1):
                for label, pattern in SOURCE_PATTERNS:
                    if pattern.search(line):
                        relative = path.relative_to(root)
                        hits[label].append(f"{relative}:{number}: {line.strip()}")
    return hits


def format_bytes(value: int) -> str:
    return f"{value / 1024.0:.2f} KiB" if value >= 1024 else f"{value} B"


def write_report(path: Path, elf: Path, functions: dict[str, Function], global_mnemonics,
                 calls, nm_text: str, source_hits) -> None:
    emitted = [f for f in functions.values() if f.size > 0 and f.words]
    emitted.sort(key=lambda function: function.size, reverse=True)
    text_bytes = sum(function.size for function in emitted)
    duplicates = duplicate_groups(functions)
    helper_hits = [helper for helper in EXPENSIVE_HELPERS if helper in nm_text]
    all_instruction_count = sum(global_mnemonics.values())
    lines = [
        "PS2 HDD Bootstrap Manager - compiler optimization audit", f"ELF: {elf}",
        f"emitted named text functions: {len(emitted)}",
        f"sum of emitted named function sizes: {text_bytes} bytes ({format_bytes(text_bytes)})",
        f"disassembled instructions: {all_instruction_count}", "", "R5900 cache context",
        "  I-cache: 16 KiB, 64-byte lines, 2-way associative",
        "  D-cache:  8 KiB, 64-byte lines, 2-way associative",
        "  Audit rule: optimize measured hot paths; do not trade them for blind -O3 code growth.", "",
        "Largest emitted functions",
    ]
    for function in emitted[:40]:
        pressure = "  [>25% I-cache]" if function.size > 4096 else ""
        lines.append(f"  {function.size:6d} B  {len(function.words or []):5d} insn  {function.name}{pressure}")
    lines += ["", "Most common emitted instructions"]
    for mnemonic, count in global_mnemonics.most_common(30):
        percent = count * 100.0 / all_instruction_count if all_instruction_count else 0.0
        lines.append(f"  {mnemonic:12s} {count:7d}  {percent:6.2f}%")
    lines += ["", "Exact duplicate machine-code functions"]
    if not duplicates:
        lines.append("  none (for functions with at least four instructions)")
    else:
        for group in duplicates[:50]:
            lines.append(f"  {len(group[0].words or []):5d} insn x {len(group):2d}: " + ", ".join(f.name for f in group))
    lines += ["", "Potentially expensive 64-bit libgcc helpers and callers"]
    if not helper_hits:
        lines.append("  none of the watched helpers are defined in the final EE ELF")
    for helper in helper_hits:
        caller_list = calls.get(helper, {})
        pretty = ", ".join(f"{name}({count})" for name, count in caller_list.most_common())
        lines.append(f"  {helper}: {pretty or 'PRESENT, no direct jal caller resolved'}")
    lines += ["", "Heavy libc entry points and direct callers"]
    for symbol in HEAVY_LIBC:
        if symbol in nm_text:
            caller_list = calls.get(symbol, {})
            pretty = ", ".join(f"{name}({count})" for name, count in caller_list.most_common())
            lines.append(f"  {symbol}: {pretty or 'PRESENT, no direct jal caller resolved'}")
    lines += ["", "Source sites that can pull heavyweight libc"]
    for label, _ in SOURCE_PATTERNS:
        lines.append(f"  [{label}]")
        matches = source_hits.get(label, [])
        if not matches:
            lines.append("    none")
        else:
            lines.extend(f"    {match}" for match in matches[:200])
            if len(matches) > 200:
                lines.append(f"    ... {len(matches) - 200} more")
    lines += ["", "Large per-function instruction mixes"]
    for function in emitted[:20]:
        if function.mnemonics:
            mix = ", ".join(f"{mnemonic}={count}" for mnemonic, count in function.mnemonics.most_common(8))
            lines.append(f"  {function.name}: {mix}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--nm")
    parser.add_argument("--objdump")
    args = parser.parse_args()
    if not args.elf.is_file():
        print(f"ELF not found: {args.elf}", file=sys.stderr)
        return 2
    try:
        nm = find_tool(args.nm, ("mips64r5900el-ps2-elf-nm", "mips64r5900el-none-elf-nm", "mipsel-none-elf-nm", "nm"))
        objdump = find_tool(args.objdump, ("mips64r5900el-ps2-elf-objdump", "mips64r5900el-none-elf-objdump", "mipsel-none-elf-objdump", "objdump"))
        nm_text = run([nm, "-S", "--size-sort", "--radix=x", "--defined-only", str(args.elf)])
        disassembly = run([objdump, "-d", "-w", str(args.elf)])
        functions = parse_nm(nm_text)
        mnemonics, calls = parse_disassembly(disassembly, functions)
        write_report(args.output, args.elf, functions, mnemonics, calls, nm_text,
                     scan_sources(args.root.resolve()))
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
