#!/usr/bin/env python3
"""Validate the deterministic R5900 counter-calibration loop in objdump output.

This is a static build guard, not a timing claim.  It verifies that the backward
branch in calibration_integer_loop repeats exactly four instructions:

    addiu <accumulator>, <accumulator>, +1
    addiu <counter>, <counter>, -1
    bnez  <counter>, <first addiu>
    nop

Real-hardware performance-counter calibration is still required before PCR
results are treated as authoritative.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


SYMBOL = "calibration_integer_loop"
SYMBOL_RE = re.compile(r"^\s*([0-9a-fA-F]+)\s+<([^>]+)>:\s*$")
INSN_RE = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+[0-9a-fA-F]+\s+([A-Za-z0-9_.]+)(?:\s+(.*?))?\s*$"
)


@dataclass(frozen=True)
class Instruction:
    address: int
    mnemonic: str
    operands: str


def parse_function(text: str, symbol: str = SYMBOL) -> list[Instruction]:
    inside = False
    instructions: list[Instruction] = []

    for line in text.splitlines():
        symbol_match = SYMBOL_RE.match(line)
        if symbol_match:
            if inside:
                break
            inside = symbol_match.group(2) == symbol
            continue

        if not inside:
            continue

        match = INSN_RE.match(line)
        if match:
            instructions.append(
                Instruction(
                    address=int(match.group(1), 16),
                    mnemonic=match.group(2).lower(),
                    operands=(match.group(3) or "").strip(),
                )
            )

    if not instructions:
        raise ValueError(f"symbol <{symbol}> not found or contains no instructions")
    return instructions


def split_operands(operands: str) -> list[str]:
    return [part.strip() for part in operands.split(",")]


def parse_integer(value: str) -> int:
    value = value.strip()
    if value.startswith("-"):
        return -parse_integer(value[1:])
    if value.lower().startswith("0x"):
        return int(value, 16)
    return int(value, 10)


def validate_loop(instructions: list[Instruction]) -> str:
    branches = [i for i, insn in enumerate(instructions) if insn.mnemonic == "bnez"]
    if len(branches) != 1:
        raise ValueError(f"expected exactly one bnez in <{SYMBOL}>, found {len(branches)}")

    branch_index = branches[0]
    branch = instructions[branch_index]
    branch_operands = split_operands(branch.operands)
    if len(branch_operands) < 2:
        raise ValueError("bnez operands are malformed")

    counter_register = branch_operands[0]
    target_match = re.match(r"^([0-9a-fA-F]+)\b", branch_operands[1])
    if not target_match:
        raise ValueError(f"cannot parse bnez target from: {branch.operands!r}")
    target = int(target_match.group(1), 16)

    address_to_index = {insn.address: i for i, insn in enumerate(instructions)}
    if target not in address_to_index:
        raise ValueError(f"bnez target 0x{target:x} is outside <{SYMBOL}>")
    target_index = address_to_index[target]

    if branch_index + 1 >= len(instructions):
        raise ValueError("bnez has no visible delay-slot instruction")

    loop = instructions[target_index : branch_index + 2]
    mnemonics = [insn.mnemonic for insn in loop]
    expected = ["addiu", "addiu", "bnez", "nop"]
    if mnemonics != expected:
        raise ValueError(
            "calibration loop body changed: expected "
            + "/".join(expected)
            + ", got "
            + "/".join(mnemonics)
        )

    accumulator_ops = split_operands(loop[0].operands)
    if len(accumulator_ops) != 3:
        raise ValueError("first addiu operands are malformed")
    if accumulator_ops[0] != accumulator_ops[1]:
        raise ValueError("first addiu must update the accumulator in place")
    try:
        accumulator_step = parse_integer(accumulator_ops[2])
    except ValueError as exc:
        raise ValueError("first addiu immediate is not an integer") from exc
    if accumulator_step != 1:
        raise ValueError(f"first addiu must increment by 1, got {accumulator_step}")

    counter_ops = split_operands(loop[1].operands)
    if len(counter_ops) != 3:
        raise ValueError("second addiu operands are malformed")
    if counter_ops[0] != counter_register or counter_ops[1] != counter_register:
        raise ValueError("second addiu must decrement the same register tested by bnez")
    try:
        counter_step = parse_integer(counter_ops[2])
    except ValueError as exc:
        raise ValueError("second addiu immediate is not an integer") from exc
    if counter_step != -1:
        raise ValueError(f"second addiu must decrement by 1, got {counter_step}")

    return (
        f"PASS: <{SYMBOL}> repeats exactly addiu(+1)/addiu(-1)/bnez/nop "
        f"from 0x{target:x} to branch 0x{branch.address:x}"
    )


def validate_text(text: str) -> str:
    return validate_loop(parse_function(text))


def selftest() -> None:
    valid = """
001015f8 <calibration_integer_loop>:
  1015f8: 3c030001  lui v1,0x1
  1015fc: 346386a0  ori v1,v1,0x86a0
  101600: 00001025  move v0,zero
  101604: 24420001  addiu v0,v0,1
  101608: 2463ffff  addiu v1,v1,-1
  10160c: 1460fffd  bnez v1,101604 <calibration_integer_loop+0xc>
  101610: 00000000  nop
  101614: 00000000  nop
  101618: 03e00008  jr ra
  10161c: 00000000  nop

00101620 <next_function>:
"""
    validate_text(valid)

    invalid_cases = [
        valid.replace("101610: 00000000  nop", "101610: 24420001  addiu v0,v0,1"),
        valid.replace("bnez v1,101604", "bnez v1,101608"),
        valid.replace("addiu v1,v1,-1", "addiu v1,v1,-2"),
        valid.replace("<calibration_integer_loop>", "<calibration_integer_loop.constprop.0>"),
    ]
    for index, bad in enumerate(invalid_cases, 1):
        try:
            validate_text(bad)
        except ValueError:
            continue
        raise AssertionError(f"negative self-test {index} unexpectedly passed")

    print("R5900 calibration disassembly checker self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--disasm", type=Path, help="objdump -dr output to validate")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()

    if args.disasm is None:
        if args.selftest:
            return 0
        parser.error("--disasm is required unless --selftest is used")

    try:
        message = validate_text(args.disasm.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"R5900 calibration disassembly guard: FAIL: {exc}", file=sys.stderr)
        return 1

    print(f"R5900 calibration disassembly guard: {message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
