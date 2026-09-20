#!/usr/bin/env python3
"""Static whole-project audit against PS2 Optimization Research Library v2.

The report is evidence, not an automatic optimizer. Regex hits are review
candidates only. Runtime performance claims still require the subsystem corpus,
current implementation/source and a real-PS2 benchmark.
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
    ("filexio-control", re.compile(r"\bfileXio(?:Open|Close|Dopen|Dclose|Dread|Devctl|Ioctl|Ioctl2|Sync|GetStat)\s*\(")),
    ("sif", re.compile(r"\b(?:sceSif|Sif)(?:CallRpc|SetDma|DmaStat|SendCmd|SendCmdIntr|ExecRequest|BindRpc|InitRpc)\s*\(")),
    ("cache-maintenance", re.compile(r"\b(?:SyncDCache|InvalidDCache|FlushCache|sceSifWriteBackDCache)\s*\(")),
    ("blocking-wait", re.compile(r"\b(?:WaitSema|SleepThread|DelayThread|fileXioSync|sceSifCallRpc)\s*\(")),
    ("timer", re.compile(r"\b(?:GetTimerSystemTime|TimerBusClock2USec|TimerUSec2BusClock)\s*\(")),
    ("alignment-attribute", re.compile(r"__attribute__\s*\(\(\s*aligned\s*\(\s*(\d+)\s*\)\s*\)\)")),
])

GOOD_BASELINE_FLAGS = ("-O2", "-flto", "-G0", "-ffunction-sections", "-fdata-sections", "--gc-sections")
BAD_GLOBAL_FLAGS = ("-O3", "-Ofast", "-ffast-math", "-funroll-loops", "-funroll-all-loops", "-G8")

# This intentionally handles normal project C, not every legal GNU C declarator.
FUNC_HEAD = re.compile(
    r"(?m)^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?"
    r"(?:[A-Za-z_]\w*[ \t\*]+)+([A-Za-z_]\w*)[ \t]*"
    r"\([^;{}]*\)[ \t\n]*(?:__attribute__\s*\(\([^{};]*\)\)\s*)?\{"
)
CONTROL_WORDS = {"if", "for", "while", "switch", "return", "sizeof"}

STATIC_ARRAY = re.compile(
    r"(?m)^[ \t]*static\s+(?:const\s+)?"
    r"(unsigned\s+char|signed\s+char|char|uint8_t|int8_t|uint16_t|int16_t|uint32_t|int32_t|uint64_t|int64_t|u8|s8|u16|s16|u32|s32|u64|s64)"
    r"\s+([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]"
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


def source_files(root: Path, roots: tuple[str, ...], suffixes: set[str] | None = None) -> list[Path]:
    result: list[Path] = []
    for name in roots:
        directory = root / name
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file():
                continue
            if suffixes is None or path.suffix in suffixes:
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
    state = "code"
    i = opening
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == '"': state = "string"
            elif ch == "'": state = "char"
            elif ch == "/" and nxt == "/": state = "line"; i += 1
            elif ch == "/" and nxt == "*": state = "block"; i += 1
            elif ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0: return i
        elif state == "string":
            if ch == "\\": i += 1
            elif ch == '"': state = "code"
        elif state == "char":
            if ch == "\\": i += 1
            elif ch == "'": state = "code"
        elif state == "line":
            if ch == "\n": state = "code"
        elif state == "block" and ch == "*" and nxt == "/":
            state = "code"; i += 1
        i += 1
    return None


def functions_in(path: Path, root: Path) -> list[FunctionInfo]:
    text = read_text(path)
    out: list[FunctionInfo] = []
    for match in FUNC_HEAD.finditer(text):
        name = match.group(1)
        if name in CONTROL_WORDS:
            continue
        opening = text.find("{", match.start(), match.end())
        closing = find_matching_brace(text, opening)
        if opening < 0 or closing is None:
            continue
        out.append(FunctionInfo(
            str(path.relative_to(root)), name,
            line_number(text, match.start()), line_number(text, closing),
            text[opening:closing + 1]))
    return out


def normalize_body(body: str) -> str:
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return re.sub(r"\s+", "", body)


def collect_hits(files: list[Path], root: Path):
    totals = collections.Counter()
    per_file: dict[str, collections.Counter[str]] = {}
    examples: dict[str, list[str]] = collections.defaultdict(list)
    alignments = collections.Counter()
    for path in files:
        rel = str(path.relative_to(root))
        counts = collections.Counter()
        for lineno, line in enumerate(read_text(path).splitlines(), 1):
            # Strip // comments so comments like "do not use -O3" are not evidence.
            code = line.split("//", 1)[0]
            for label, pattern in PATTERNS.items():
                matches = list(pattern.finditer(code))
                if not matches:
                    continue
                counts[label] += len(matches)
                totals[label] += len(matches)
                if len(examples[label]) < 32:
                    examples[label].append(f"{rel}:{lineno}: {line.strip()}")
                if label == "alignment-attribute":
                    for hit in matches:
                        alignments[hit.group(1)] += 1
        if counts:
            per_file[rel] = counts
    return totals, per_file, examples, alignments


def fixed_arrays(files: list[Path], root: Path):
    rows = []
    for path in files:
        text = read_text(path)
        for hit in STATIC_ARRAY.finditer(text):
            kind, name, count_s = hit.groups()
            count = int(count_s)
            size = TYPE_SIZE[kind] * count
            if size >= 1024:
                rows.append((size, str(path.relative_to(root)), line_number(text, hit.start()), name))
    return sorted(rows, reverse=True)


def function_candidates(functions: list[FunctionInfo]):
    rows = []
    for fn in functions:
        traits = []
        body = fn.body
        for label in ("dynamic-allocation", "bulk-copy", "heavy-formatting", "filexio-data", "filexio-control", "blocking-wait", "sif"):
            if PATTERNS[label].search(body): traits.append(label)
        if re.search(r"\b(?:for|while)\s*\(", body): traits.append("loop")
        if fn.lines >= 100 or ("loop" in traits and any(t in traits for t in ("dynamic-allocation", "heavy-formatting", "filexio-data", "blocking-wait", "sif"))):
            rows.append((fn.lines, fn.path, fn.start_line, fn.name, ",".join(traits) or "large"))
    return sorted(rows, reverse=True)


def duplicate_functions(functions: list[FunctionInfo]):
    groups: dict[tuple[int, str], list[FunctionInfo]] = collections.defaultdict(list)
    for fn in functions:
        normalized = normalize_body(fn.body)
        if len(normalized) < 80:
            continue
        key = (len(normalized), hashlib.sha256(normalized.encode()).hexdigest())
        groups[key].append(fn)
    out = [group for group in groups.values() if len(group) > 1]
    out.sort(key=lambda g: len(normalize_body(g[0].body)), reverse=True)
    return out


def literal_formatter_hits(files: list[Path], root: Path):
    # Intentionally narrow: snprintf(dst, sizeof(dst), "literal") only.
    pattern = re.compile(r"\bsnprintf\s*\([^,]+,\s*sizeof\s*\([^)]*\)\s*,\s*\"([^\"]*)\"\s*\)\s*;")
    rows = []
    for path in files:
        for lineno, line in enumerate(read_text(path).splitlines(), 1):
            hit = pattern.search(line)
            if hit and "%" not in hit.group(1):
                rows.append(f"{path.relative_to(root)}:{lineno}: {line.strip()}")
    return rows


def actual_build_flag_text(root: Path) -> str:
    # Only variable/recipe lines, with make/sh comments removed. This prevents
    # prose such as "do not use -O3" from becoming a false global-flag hit.
    lines = []
    for path in (root / "Makefile", root / "iop" / "hdl_stream" / "Makefile"):
        if not path.is_file(): continue
        for line in read_text(path).splitlines():
            stripped = line.lstrip()
            if not stripped or stripped.startswith("#"): continue
            lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def write_report(root: Path, output: Path) -> None:
    runtime = source_files(root, RUNTIME_ROOTS, SOURCE_SUFFIXES)
    support = source_files(root, SUPPORT_ROOTS, SOURCE_SUFFIXES | {".py", ".sh", ".yml", ".yaml"})
    functions = [fn for path in runtime if path.suffix in {".c", ".inc"} for fn in functions_in(path, root)]
    totals, per_file, examples, alignments = collect_hits(runtime, root)
    arrays = fixed_arrays(runtime, root)
    candidates = function_candidates(functions)
    duplicates = duplicate_functions(functions)
    literal_hits = literal_formatter_hits(runtime, root)
    build_text = actual_build_flag_text(root)
    good = [flag for flag in GOOD_BASELINE_FLAGS if flag in build_text]
    bad = [flag for flag in BAD_GLOBAL_FLAGS if flag in build_text]

    runtime_lines = sum(len(read_text(path).splitlines()) for path in runtime)
    support_lines = sum(len(read_text(path).splitlines()) for path in support)

    lines = [
        "PS2 HDD Bootstrap Manager - Optimization Research Library v2 whole-project audit",
        "",
        "Epistemic status",
        "  CURRENT IMPLEMENTATION: source inventory/static pattern counts.",
        "  INFERENCJA: review priorities derived from corpus rules.",
        "  HIPOTEZA DO TESTU: performance claims require a real-PS2 benchmark.",
        "",
        "Source-of-truth routing",
    ]
    lines += [f"  {topic:26s} -> {doc}" for topic, doc in CORPUS_ROUTING]
    lines += [
        "",
        "Project inventory",
        f"  runtime source files: {len(runtime)}",
        f"  runtime source lines: {runtime_lines}",
        f"  approximate C functions: {len(functions)}",
        f"  support/test/tool files: {len(support)}",
        f"  support/test/tool lines: {support_lines}",
        "  third_party excluded from source-policy findings; emitted third-party code remains visible in the ELF audit.",
        "",
        "Build policy",
        f"  baseline flags present: {', '.join(good) if good else 'none detected'}",
        f"  aggressive global flags detected: {', '.join(bad) if bad else 'none'}",
        "  Policy: -O2 baseline; -O3/-Os only as measured per-TU/per-kernel experiments.",
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
    lines.append("  Rule: allocator, cache-line, DMA/device and packet alignment are separate contracts.")

    lines += ["", "Files with the largest optimization-relevant source footprint"]
    scored = [(sum(counts.values()), path, counts) for path, counts in per_file.items()]
    for score, path, counts in sorted(scored, reverse=True)[:40]:
        mix = ", ".join(f"{label}={count}" for label, count in counts.most_common())
        lines.append(f"  {score:4d}  {path}: {mix}")

    lines += ["", "Large/static fixed arrays (>= 1 KiB)"]
    if arrays:
        for size, path, lineno, name in arrays[:80]:
            lines.append(f"  {size:7d} B  {path}:{lineno}  {name}")
    else:
        lines.append("  none detected by the fixed-array scanner")

    lines += ["", "Large or mixed-responsibility function candidates"]
    lines.append("  Heuristic only. Machine-code size and hardware profiles remain authoritative.")
    if candidates:
        for length, path, start, name, traits in candidates[:100]:
            lines.append(f"  {length:4d} lines  {path}:{start}  {name}  [{traits}]")
    else:
        lines.append("  none detected")

    lines += ["", "Exact normalized source-body duplicates"]
    if duplicates:
        for group in duplicates[:60]:
            where = ", ".join(f"{fn.path}:{fn.start_line}:{fn.name}" for fn in group)
            lines.append(f"  x{len(group)}: {where}")
    else:
        lines.append("  none detected for non-trivial bodies")
    lines.append("  Final machine-code duplication is authoritative because LTO can merge/clone source functions.")

    lines += ["", "Literal-only snprintf candidates"]
    if literal_hits:
        lines.extend(f"  {hit}" for hit in literal_hits[:100])
    else:
        lines.append("  none detected")

    lines += ["", "Pattern evidence samples"]
    for label in PATTERNS:
        lines.append(f"  [{label}]")
        sample = examples.get(label, [])
        lines.extend(f"    {item}" for item in sample) if sample else lines.append("    none")

    lines += [
        "",
        "Corpus-v2 review gates",
        "  1. Establish the probable bottleneck before changing code.",
        "  2. Remove work/frequency/bytes before instruction micro-optimization.",
        "  3. Record producer, consumer, lifetime, representation, alignment, transport, batch, deadline and ownership for large data.",
        "  4. Treat alloc/copy/format/fileXio/RPC/wait inside loops as review triggers.",
        "  5. Submit early, wait late; buffering requires explicit ownership/fences.",
        "  6. Keep IOP services small and device-local where the final consumer is IOP-side.",
        "  7. Avoid payload IOP->EE->IOP round trips without a measured reason.",
        "  8. Specialized hardware/manual asm only for proven regular hot kernels.",
        "  9. Re-run ELF/map audit after structural changes because the bottleneck and I-cache footprint can move.",
        " 10. Real-time benchmarks report p50/p95/p99/max and deadline misses, not average alone.",
    ]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, default=Path("CORPUS_V2_PROJECT_AUDIT.txt"))
    args = parser.parse_args()
    write_report(args.root.resolve(), args.output)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
