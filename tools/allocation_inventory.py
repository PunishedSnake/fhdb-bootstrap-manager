#!/usr/bin/env python3
"""Emit a complete direct runtime allocation/free inventory for corpus-v2 review.

This is deliberately a source inventory, not an allocator-performance claim.
The output keeps every direct EE libc malloc/calloc/realloc/memalign/free call
and every direct IOP SysMem AllocSysMemory/FreeSysMemory call instead of the
sample-limited evidence section in the broader project audit. Events are bound
to the containing C function when the existing source parser can resolve one.

ThreadMan objects (CreateThread/CreateSema/etc.) are intentionally not reported
as heap allocations here: their implementation-owned memory cost belongs in the
runtime IOP-resource budget and must not be guessed from an API call count.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import tempfile
from pathlib import Path
from typing import Any

from corpus_v2_project_audit import (
    RUNTIME_ROOTS,
    SOURCE_SUFFIXES,
    functions_in,
    read_text,
    source_files,
)

ALLOC_RE = re.compile(
    r"\b(malloc|calloc|realloc|memalign|free|AllocSysMemory|FreeSysMemory)\s*\("
)
ALLOCATORS = {"malloc", "calloc", "realloc", "memalign", "AllocSysMemory"}
FREES = {"free", "FreeSysMemory"}


def _domain(operation: str) -> str:
    if operation in {"AllocSysMemory", "FreeSysMemory"}:
        return "IOP SysMem"
    return "EE/libc heap"


def _function_index(root: Path, files: list[Path]) -> dict[str, list[Any]]:
    return {
        str(path.relative_to(root)): functions_in(path, root)
        for path in files
        if path.suffix in {".c", ".inc"}
    }


def _scope_for(functions: list[Any], lineno: int) -> str:
    for fn in functions:
        if fn.start_line <= lineno <= fn.end_line:
            return fn.name
    return "<file-scope>"


def collect(root: Path) -> dict[str, Any]:
    files = source_files(root, RUNTIME_ROOTS, SOURCE_SUFFIXES)
    by_path = _function_index(root, files)
    events: list[dict[str, Any]] = []
    operation_counts: collections.Counter[str] = collections.Counter()
    domain_counts: collections.Counter[str] = collections.Counter()
    file_counts: collections.Counter[str] = collections.Counter()
    function_counts: collections.Counter[str] = collections.Counter()

    for path in files:
        rel = str(path.relative_to(root))
        functions = by_path.get(rel, [])
        for lineno, line in enumerate(read_text(path).splitlines(), 1):
            code = line.split("//", 1)[0]
            for match in ALLOC_RE.finditer(code):
                operation = match.group(1)
                scope = _scope_for(functions, lineno)
                domain = _domain(operation)
                event = {
                    "path": rel,
                    "line": lineno,
                    "function": scope,
                    "operation": operation,
                    "kind": "allocate" if operation in ALLOCATORS else "free",
                    "memory_domain": domain,
                    "source": line.strip(),
                }
                events.append(event)
                operation_counts[operation] += 1
                domain_counts[domain] += 1
                file_counts[rel] += 1
                function_counts[f"{rel}:{scope}"] += 1

    events.sort(key=lambda item: (item["path"], item["line"], item["operation"]))
    return {
        "epistemic_status": "CURRENT IMPLEMENTATION: static direct allocator-call inventory",
        "coverage_notes": [
            "EE libc heap calls: malloc/calloc/realloc/memalign/free",
            "IOP SysMem calls: AllocSysMemory/FreeSysMemory",
            "ThreadMan object backing memory is implementation-owned and not inferred from CreateThread/CreateSema call counts",
            "allocator-internal allocations not visible as direct source calls are outside this static inventory",
        ],
        "runtime_roots": list(RUNTIME_ROOTS),
        "total_events": len(events),
        "allocation_events": sum(
            count for operation, count in operation_counts.items()
            if operation in ALLOCATORS
        ),
        "free_events": sum(
            count for operation, count in operation_counts.items()
            if operation in FREES
        ),
        "operation_counts": dict(sorted(operation_counts.items())),
        "memory_domain_event_counts": dict(sorted(domain_counts.items())),
        "file_counts": dict(sorted(file_counts.items())),
        "function_counts": dict(sorted(function_counts.items())),
        "events": events,
    }


def selftest() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "src").mkdir()
        (root / "include").mkdir()
        (root / "iop").mkdir()
        (root / "src" / "fixture.c").write_text(
            """
#include <stdlib.h>
static void *phase(unsigned int n)
{
    void *a = malloc(n);
    void *b = memalign(64, n);
    a = realloc(a, n + 1);
    free(b);
    free(a);
    return 0;
}
""".lstrip(),
            encoding="utf-8",
        )
        (root / "iop" / "fixture.c").write_text(
            """
static void iop_phase(unsigned int n)
{
    void *p = AllocSysMemory(0, n, 0);
    if (p != 0)
        FreeSysMemory(p);
}
""".lstrip(),
            encoding="utf-8",
        )
        result = collect(root)
        assert result["total_events"] == 7
        assert result["allocation_events"] == 4
        assert result["free_events"] == 3
        assert result["operation_counts"] == {
            "AllocSysMemory": 1,
            "FreeSysMemory": 1,
            "free": 2,
            "malloc": 1,
            "memalign": 1,
            "realloc": 1,
        }
        assert result["memory_domain_event_counts"] == {
            "EE/libc heap": 5,
            "IOP SysMem": 2,
        }
        assert any(
            event["function"] == "iop_phase" and
            event["memory_domain"] == "IOP SysMem"
            for event in result["events"]
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path,
                        default=Path("ALLOCATION_INVENTORY.json"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("allocation_inventory selftest: PASS")
        return 0

    root = args.root.resolve()
    result = collect(root)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "allocation inventory: "
        f"{result['allocation_events']} allocate events, "
        f"{result['free_events']} free events, "
        f"{result['total_events']} total; "
        + ", ".join(
            f"{domain}={count}"
            for domain, count in result["memory_domain_event_counts"].items()
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
