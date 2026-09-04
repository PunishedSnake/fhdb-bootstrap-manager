#!/usr/bin/env python3
"""Guard the reviewed explicit 64-byte alignment set.

The policy is intentionally conservative. Existing aligned(64) sites are not all
endorsed: docs/ALIGNMENT_CONTRACT_AUDIT.md classifies most as cleanup candidates.
This guard merely prevents new blanket 64-byte attributes from appearing without
an explicit review/update, while separately enforcing the one current libpad
buffer whose 64-byte contract is mandatory.
"""

from __future__ import annotations

import argparse
import collections
import re
import tempfile
from pathlib import Path

from corpus_v2_project_audit import RUNTIME_ROOTS, SOURCE_SUFFIXES, read_text, source_files

ALIGN64_RE = re.compile(
    r"__attribute__\s*\(\(\s*aligned\s*\(\s*64\s*\)\s*\)\)"
)

EXPECTED_BY_FILE = {
    "src/gs_ui_ps2.c": 1,
    "src/hdd_forensic_repair_ps2.c": 4,
    "src/hdd_read.c": 1,
    "src/hdd_recovery_wrap.c": 1,
    "src/hdd_repair_ps2.c": 2,
    "src/hdd_write.c": 3,
    "src/hdl_installer_ps2.c": 3,
    "src/hdl_tools/catalog.inc": 2,
    "src/hdl_tools/source_ui.inc": 1,
    "src/hdl_tools/source_ui_resume_hash.inc": 1,
    "src/hdl_tools/transaction.inc": 1,
    "src/hdl_tools/transaction_resume_hash.inc": 1,
    "src/header_backup.c": 1,
    "src/main.c": 1,
    "src/platform.c": 1,
    "src/repair_snapshot.c": 1,
}

PAD_DECL_RE = re.compile(
    r"static\s+unsigned\s+char\s+pad_buffer\s*\[\s*256\s*\]"
    r"\s*__attribute__\s*\(\(\s*aligned\s*\(\s*64\s*\)\s*\)\)\s*;",
    re.S,
)


def collect(root: Path) -> dict[str, int]:
    counts: collections.Counter[str] = collections.Counter()
    for path in source_files(root, RUNTIME_ROOTS, SOURCE_SUFFIXES):
        rel = str(path.relative_to(root))
        text = read_text(path)
        # The declaration itself can span lines. Strip comments only to avoid a
        # documentation example becoming a policy hit.
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        count = len(ALIGN64_RE.findall(text))
        if count:
            counts[rel] = count
    return dict(sorted(counts.items()))


def check(root: Path) -> list[str]:
    errors: list[str] = []
    observed = collect(root)
    expected = dict(sorted(EXPECTED_BY_FILE.items()))
    if observed != expected:
        all_paths = sorted(set(observed) | set(expected))
        for path in all_paths:
            got = observed.get(path, 0)
            want = expected.get(path, 0)
            if got != want:
                errors.append(
                    f"{path}: aligned(64) count {got}, audited count {want}; "
                    "review docs/ALIGNMENT_CONTRACT_AUDIT.md and update policy intentionally"
                )

    platform = root / "src" / "platform.c"
    if not platform.is_file() or not PAD_DECL_RE.search(read_text(platform)):
        errors.append(
            "src/platform.c: pad_buffer must remain a 256-byte, 64-byte-aligned "
            "libpad area under the current PS2SDK contract"
        )
    return errors


def selftest() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in RUNTIME_ROOTS:
            (root / name).mkdir(parents=True, exist_ok=True)
        # Unit-test collection independently from the repository-specific
        # EXPECTED_BY_FILE table.
        (root / "src" / "a.c").write_text(
            "static unsigned char x[8] __attribute__((aligned(64)));\n",
            encoding="utf-8",
        )
        (root / "src" / "b.c").write_text(
            "/* __attribute__((aligned(64))) */\n"
            "static unsigned char y[8];\n",
            encoding="utf-8",
        )
        assert collect(root) == {"src/a.c": 1}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("check_alignment_policy selftest: PASS")
        return 0

    errors = check(args.root.resolve())
    if errors:
        for error in errors:
            print(f"alignment policy: {error}")
        return 2
    print(
        "alignment policy: PASS "
        f"({sum(EXPECTED_BY_FILE.values())} audited aligned(64) sites)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
