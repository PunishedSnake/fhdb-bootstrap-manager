#!/usr/bin/env python3
"""Emit a complete runtime allocation/free inventory for corpus-v2 review.

This is deliberately a source inventory, not an allocator-performance claim.
The output keeps every malloc/calloc/realloc/memalign/free occurrence instead of
the sample-limited evidence section in the broader project audit, and associates
it with the containing C function when the existing source parser can resolve
one.
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

ALLOC_RE = re.compile(r"\b(malloc|calloc|realloc|memalign|free)\s*\(")
ALLOCATORS = {"malloc", "calloc", "realloc", "memalign"}


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
                event = {
                    "path": rel,
                    "line": lineno,
                    "function": scope,
                    "operation": operation,
                    "kind": "allocate" if operation in ALLOCATORS else "free",
                    "source": line.strip(),
                }
                events.append(event)
                operation_counts[operation] += 1
                file_counts[rel] += 1
                function_counts[f"{rel}:{scope}"] += 1

    events.sort(key=lambda item: (item["path"], item["line"], item["operation"]))
    return {
        "epistemic_status": "CURRENT IMPLEMENTATION: static source inventory",
        "runtime_roots": list(RUNTIME_ROOTS),
        "total_events": len(events),
        "allocation_events": sum(
            count for operation, count in operation_counts.items()
            if operation in ALLOCATORS
        ),
        "free_events": operation_counts["free"],
        "operation_counts": dict(sorted(operation_counts.items())),
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
        result = collect(root)
        assert result["total_events"] == 5
        assert result["allocation_events"] == 3
        assert result["free_events"] == 2
        assert result["operation_counts"] == {
            "free": 2,
            "malloc": 1,
            "memalign": 1,
            "realloc": 1,
        }
        assert all(
            event["function"] == "phase" for event in result["events"]
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
        f"{result['total_events']} total"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
