#!/usr/bin/env python3
"""Whole-project PS2 Optimization Research Library v2 audit.

This is intentionally conservative. It reports source-level evidence and review
candidates; it does not claim that a regex hit is automatically a performance
bug. The authoritative decision remains: identify the bottleneck, inspect the
relevant PS2 corpus/current source, then benchmark on real hardware.

Runtime source roots are src/, include/ and iop/. Tests and tooling are counted
separately so host-only code does not get mistaken for an EE/IOP hotspot.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

RUNTIME_ROOTS = ("src", "include", "iop")
SUPPORT_ROOTS = ("tests", "tools", ".github")
SOURCE_SUFFIXES = {".c", ".h", ".inc", ".s", ".S"}

CORPUS_ROUTING = (
    ("R5900/core/cache/codegen", "MIPS_R5900_optimization_research_corpus_v2.md"),
    ("ELF/ABI/layout", "PS2_Linker_ELF_ABI_Layout_optimization_research_corpus.md"),
    ("data layout/lifetime", "PS2_Data_Oriented_Design_optimization_research_corpus_v2.md"),
    ("allocators/alignment", "PS2_Memory_Allocators_optimization_research_corpus_v2.md"),
    ("whole-system scheduling", "PS2_Whole_System_Scheduling_research_corpus_v2.md"),
    ("IOP/SIF", "PS2_IOP_SIF_optimization_research_corpus_v2.md"),
    ("HDD/APA/PFS/HDL", "PS2_HDD_APA_PFS_HDL_filesystem_optimization_research_corpus_v2.md"),
    ("USB 1.1", "PS2_USB_1_1_optimization_research_corpus.md"),
    ("GS", "PS2_Graphics_Synthesizer_optimization_research_corpus_v2.md"),
    ("PS2SDK integration", "PS2_PS2SDK_optimization_research_corpus_v2.md"),
)

PATTERNS = collections.OrderedDict([
    ("dynamic-allocation", re.compile(r"\b(?:malloc|calloc|realloc|memalign|free)\s*\(")),
    ("bulk-copy", re.compile(r"\b(?:memcpy|memmove)\s*\(")),
    ("bulk-zero", re.compile(r"\bmemset\s*\(")),
    ("heavy-formatting", re.compile(r"\b(?:snprintf|vsnprintf|sprintf|vsprintf|printf|fprintf|scr_printf|scr_vprintf)\s*\(")),
    ("sort", re.compile(r"\bqsort\s*\(")),
    ("filexio-data", re.compile(r"\bfileXio(?:Read|Write|Lseek|Lseek64)\s*\(")),
    ("filexio-control", re.compile(r"\bfileXio(?:Open|Close|Dopen|Dclose|Dread|Devctl|Ioctl|Ioctl2|Sync)\s*\(")),
    ("sif", re.compile(r"\b(?:sceSif|Sif)(?:CallRpc|SetDma|DmaStat|SendCmd|SendCmdIntr|ExecRequest|BindRpc|InitRpc)\s*\(")),
    ("cache-maintenance", re.compile(r"\b(?:SyncDCache|InvalidDCache|FlushCache|sceSifWriteBackDCache)\s*\(")),
    ("blocking-wait", re.compile(r"\b(?:WaitSema|SleepThread|DelayThread|fileXioSync|sceSifCallRpc)\s*\(")),
    ("timer", re.compile(r"\b(?:GetTimerSystemTime|TimerBusClock2USec|TimerUSec2BusClock)\s*\(")),
    ("alignment-attribute", re.compile(r"__attribute__\s*\(\(\s*aligned\s*\(\s*(\d+)\s*\)\s*\)\)")),
])

BAD_GLOBAL_FLAGS = (
    "-O3", "-Ofast", "-ffast-math", "-funroll-loops", "-funroll-all-loops",
    "-falign-functions=64", "-falign-functions=128", "-G8",
)
GOOD_BASELINE_FLAGS = ("-O2", "-flto", "-G0", "-ffunction-sections", "-fdata-sections", "--gc-sections")

CONTROL_WORDS = {"if", "for", "while", "switch", "return", "sizeof"}
FUNC_HEAD = re.compile(
    r"(?m)^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?(?:__attribute__\s*\(\([^\n]*\)\)\s*)?"
    r"(?:[A-Za-z_][\w\s\*]*?)[ \t]+([A-Za-z_]\w*)[ \t]*\([^;{}]*\)[ \t]*"
    r"(?:__attribute__\s*\(\([^{};]*\)\)\s*)?\{"
)

STATIC_ARRAY = re.compile(
    r"(?m)^[ \t]*static\s+(?:const\s+)?"
    r"(unsigned\s+char|signed\s+char|char|uint8_t|int8_t|uint16_t|int16_t|uint32_t|int32_t|uint64_t|int64_t|u8|s8|u16|s16|u32|s32|u64|s64)"
    r"(?:\s+|\s*\*\s*)([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]"
)

TYPE_SIZE = {
    "unsigned char": 1, "signed char": 1, "char": 1, "uint8_t": 1, "int8_t": 1, "u8": 1, "s8": 1,
    "uint16_t": 2, "int16_t": 2, "u16": 2, "s16": 2,
    "uint32_t": 4, "int32_t": 4, "u32": 4, "s32": 4,
    "uint64_t": 8, "int64_t": 8, "u64": 8, "s64": 8,
}


@dataclass
class FunctionInfo:
    path: str
    name: str
    start_line: int
    end_line: int
    body: str

    @property
    def lines(self) -> int:
        return self.end_line - self.start_line + 1


def runtime_files(root: Path) -> list[Path]:
    result: list[Path] = []
    for directory_name in RUNTIME_ROOTS:
        directory = root / directory_name
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                result.append(path)
    return sorted(result)


def support_files(root: Path) -> list[Path]:
    result: list[Path] = []
    for directory_name in SUPPORT_ROOTS:
        directory = root / directory_name
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if path.is_file() and (path.suffix in SOURCE_SUFFIXES or path.suffix in {".py", ".sh", ".yml", ".yaml"}):
                result.append(path)
    return sorted(result)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="utf-8", errors="replace")


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def find_matching_brace(text: str, opening: int) -> int | None:
    depth = 0
    i = opening
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "/" and nxt == "/":
                state = "line-comment"
                i += 1
            elif ch == "/" and nxt == "*":
                state = "block-comment"
                i += 1
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return i
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\":
                i += 1
            elif ch == "'":
                state = "code"
        elif state == "line-comment":
            if ch == "\n":
                state = "code"
        elif state == "block-comment":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 1
        i += 1
    return None


def extract_functions(path: Path, root: Path) -> list[FunctionInfo]:
    text = read_text(path)
    functions: list[FunctionInfo] = []
    for match in FUNC_HEAD.finditer(text):
        name = match.group(1)
        if name in CONTROL_WORDS:
            continue
        opening = text.find("{", match.start(), match.end())
        if opening < 0:
            continue
        closing = find_matching_brace(text, opening)
        if closing is None:
            continue
        start = line_number(text, match.start())
        end = line_number(text, closing)
        functions.append(FunctionInfo(str(path.relative_to(root)), name, start, end, text[opening:closing + 1]))
    return functions


def normalize_body(body: str) -> str:
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"\s+", "", body)
    return body


def collect_pattern_hits(files: list[Path], root: Path):
    totals = collections.Counter()
    per_file: dict[str, collections.Counter[str]] = {}
    examples: dict[str, list[str]] = collections.defaultdict(list)
    alignments = collections.Counter()
    for path in files:
        relative = str(path.relative_to(root))
        counter: collections.Counter[str] = collections.Counter()
        for lineno, line in enumerate(read_text(path).splitlines(), 1):
            for label, pattern in PATTERNS.items():
                matches = list(pattern.finditer(line))
                if not matches:
                    continue
                counter[label] += len(matches)
                totals[label] += len(matches)
                if len(examples[label]) < 40:
                    examples[label].append(f"{relative}:{lineno}: {line.strip()}")
                if label == "alignment-attribute":
                    for found in matches:
                        alignments[found.group(1)] += 1
        if counter:
            per_file[relative] = counter
    return totals, per_file, examples, alignments


def static_arrays(files: list[Path], root: Path):
    arrays = []
    for path in files:
        text = read_text(path)
        for match in STATIC_ARRAY.finditer(text):
            kind, name, count_text = match.groups()
            count = int(count_text)
            size = TYPE_SIZE[kind] * count
            if size >= 1024:
                arrays.append((size, str(path.relative_to(root)), line_number(text, match.start()), name, kind, count))
    return sorted(arrays, reverse=True)


def suspicious_function_traits(functions: list[FunctionInfo]):
    rows = []
    for fn in functions:
        body = fn.body
        traits = []
        if PATTERNS["dynamic-allocation"].search(body):
            traits.append("alloc")
        if PATTERNS["bulk-copy"].search(body):
            traits.append("copy")
        if PATTERNS["heavy-formatting"].search(body):
            traits.append("format")
        if PATTERNS["filexio-data"].search(body) or PATTERNS["filexio-control"].search(body):
            traits.append("fileXio")
        if PATTERNS["blocking-wait"].search(body):
            traits.append("wait")
        if re.search(r"\b(?:for|while)\s*\(", body):
            traits.append("loop")
        if traits and (fn.lines >= 80 or ("loop" in traits and any(t in traits for t in ("alloc", "format", "fileXio", "wait")))):
            rows.append((fn.lines, fn.path, fn.start_line, fn.name, ",".join(traits)))
    return sorted(rows, reverse=True)


def duplicate_source_functions(functions: list[FunctionInfo]):
    groups: dict[tuple[int, str], list[FunctionInfo]] = collections.defaultdict(list)
    for fn in functions:
        normalized = normalize_body(fn.body)
        if len(normalized) < 80:
            continue
        digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()
        groups[(len(normalized), digest)].append(fn)
    result = [group for group in groups.values() if len(group) > 1]
    result.sort(key=lambda group: len(normalize_body(group[0].body)), reverse=True)
    return result


def formatter_literals(files: list[Path], root: Path):
    # Formatting a literal string with no conversion is needless formatter machinery.
    pattern = re.compile(r"\b(?:snprintf|sprintf)\s*\([^,]+,\s*(?:[^,]+,\s*)?\"([^\"]*)\"\s*\)")
    hits = []
    for path in files:
        for lineno, line in enumerate(read_text(path).splitlines(), 1):
            match = pattern.search(line)
            if match and "%" not in match.group(1):
                hits.append(f"{path.relative_to(root)}:{lineno}: {line.strip()}")
    return hits


def build_flags(root: Path):
    paths = [root / "Makefile", root / "iop" / "hdl_stream" / "Makefile"]
    text = "\n".join(read_text(path) for path in paths if path.is_file())
    good = [flag for flag in GOOD_BASELINE_FLAGS if flag in text]
    bad = [flag for flag in BAD_GLOBAL_FLAGS if flag in text]
    return good, bad


def write_report(root: Path, output: Path) -> None:
    runtime = runtime_files(root)
    support = support_files(root)
    functions = [fn for path in runtime for fn in extract_functions(path, root)]
    totals, per_file, examples, alignments = collect_pattern_hits(runtime, root)
    arrays = static_arrays(runtime, root)
    suspicious = suspicious_function_traits(functions)
    duplicates = duplicate_source_functions(functions)
    literals = formatter_literals(runtime, root)
    good_flags, bad_flags = build_flags(root)

    runtime_lines = sum(len(read_text(path).splitlines()) for path in runtime)
    support_lines = sum(len(read_text(path).splitlines()) for path in support)

    lines = [
        "PS2 HDD Bootstrap Manager - Optimization Research Library v2 whole-project audit",
        "",
        "Epistemic status",
        "  CURRENT IMPLEMENTATION: source inventory and static pattern counts below.",
        "  INFERENCJA: review priorities derived from corpus rules.",
        "  HIPOTEZA DO TESTU: any performance claim still requires a real-PS2 benchmark.",
        "",
        "Source-of-truth routing used by this audit",
    ]
    lines += [f"  {topic:26s} -> {document}" for topic, document in CORPUS_ROUTING]
    lines += [
        "",
        "Project inventory",
        f"  runtime source files: {len(runtime)}",
        f"  runtime source lines: {runtime_lines}",
        f"  approximate C functions: {len(functions)}",
        f"  support/test/tool files: {len(support)}",
        f"  support/test/tool lines: {support_lines}",
        "  third_party is intentionally excluded from source-policy findings; its emitted code is still visible in the ELF audit.",
        "",
        "Build policy",
        f"  baseline flags present: {', '.join(good_flags) if good_flags else 'none detected'}",
        f"  aggressive global flags detected: {', '.join(bad_flags) if bad_flags else 'none'}",
        "  Rule: keep -O2 as baseline; test -O3/-Os per proven hotspot/cold TU instead of globally.",
        "",
        "Runtime source pattern totals",
    ]
    for label in PATTERNS:
        lines.append(f"  {label:22s} {totals[label]:5d}")
    lines += ["", "Alignment attribute inventory"]
    if alignments:
        for value, count in sorted(alignments.items(), key=lambda item: int(item[0])):
            lines.append(f"  aligned({value}) x {count}")
    else:
        lines.append("  none")
    lines += [
        "  Review rule: allocator alignment, EE cache-line alignment, DMA/device alignment and packet alignment are separate contracts.",
        "",
        "Files with the largest optimization-relevant source footprint",
    ]
    scored = []
    for path, counts in per_file.items():
        score = sum(counts.values())
        scored.append((score, path, counts))
    for score, path, counts in sorted(scored, reverse=True)[:40]:
        mix = ", ".join(f"{label}={count}" for label, count in counts.most_common())
        lines.append(f"  {score:4d}  {path}: {mix}")

    lines += ["", "Large/static buffers (>= 1 KiB, explicit fixed arrays)"]
    if arrays:
        for size, path, lineno, name, kind, count in arrays[:80]:
            lines.append(f"  {size:7d} B  {path}:{lineno}  {name}[{count}] ({kind})")
    else:
        lines.append("  none detected by the simple fixed-array scanner")

    lines += ["", "Large or mixed-responsibility function candidates"]
    lines.append("  Heuristic only: long functions and loops that also allocate/format/wait/fileXio are review targets, not automatic bugs.")
    for length, path, start, name, traits in suspicious[:80]:
        lines.append(f"  {length:4d} lines  {path}:{start}  {name}  [{traits}]")

    lines += ["", "Exact normalized source-body duplicates"]
    if not duplicates:
        lines.append("  none detected for bodies >= 80 normalized characters")
    else:
        for group in duplicates[:60]:
            size = len(normalize_body(group[0].body))
            where = ", ".join(f"{fn.path}:{fn.start_line}:{fn.name}" for fn in group)
            lines.append(f"  {size:5d} chars x {len(group)}: {where}")
    lines.append("  Note: final machine-code duplication remains authoritative because LTO may merge or clone source functions.")

    lines += ["", "Literal-only formatter candidates"]
    if literals:
        lines.extend(f"  {item}" for item in literals[:100])
    else:
        lines.append("  none detected")

    lines += ["", "Pattern evidence samples"]
    for label in PATTERNS:
        lines.append(f"  [{label}]")
        sample = examples.get(label, [])
        if not sample:
            lines.append("    none")
        else:
            lines.extend(f"    {item}" for item in sample)

    lines += [
        "",
        "Corpus-v2 review gates",
        "  1. Establish the probable bottleneck before changing code.",
        "  2. Remove work / frequency / bytes before micro-optimizing instructions.",
        "  3. For every large dataset record producer, consumer, lifetime, representation, alignment, transport, batch size, deadline and ownership states.",
        "  4. Treat malloc/free, copies, formatting, fileXio/RPC and waits inside loops as review triggers.",
        "  5. Use submit-early/wait-late and buffering only with explicit ownership/fences.",
        "  6. Keep IOP service code small; prefer device-local consumers and coarse control-plane calls.",
        "  7. Keep final data close to the consumer; avoid IOP->EE->IOP payload round trips.",
        "  8. Use specialized hardware only after the workload is regular enough and measured.",
        "  9. Re-run the compiler ELF audit after each structural optimization because the bottleneck and I-cache footprint may move.",
        " 10. Hardware performance results must report p50/p95/p99/max/deadline misses where latency matters.",
    ]

    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, default=Path("CORPUS_V2_PROJECT_AUDIT.txt"))
    args = parser.parse_args()
    root = args.root.resolve()
    write_report(root, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
