#!/usr/bin/env python3
"""Validate recovery-specific evidence before resume-hash A/B comparison.

The generic comparator checks timing identity and restored byte counts. This
companion gate proves that PAYLOAD_VERIFIED experiment samples came from the
new source-free stage-4 path rather than an older checkpoint build that still
re-opened the ISO before restoring the same digest.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def validate(raw: Any) -> int:
    if isinstance(raw, dict):
        raw = raw.get("samples")
    if not isinstance(raw, list) or not raw:
        raise ValueError("input must contain a non-empty samples array")

    payload_restores = 0
    for index, sample in enumerate(raw):
        if not isinstance(sample, dict):
            raise ValueError(f"sample {index}: expected object")
        for key in ("mode", "workload_kind", "checkpoint_status",
                    "source_reopen_skipped"):
            if key not in sample:
                raise ValueError(f"sample {index}: missing {key}")
        skipped = sample["source_reopen_skipped"]
        if not isinstance(skipped, bool):
            raise ValueError(
                f"sample {index}: source_reopen_skipped must be boolean"
            )
        mode = str(sample["mode"]).upper()
        workload = str(sample["workload_kind"])
        status = str(sample["checkpoint_status"])

        expected = (
            mode == "EXP"
            and workload == "payload_verified_resume"
            and status == "restored"
        )
        if skipped != expected:
            if expected:
                raise ValueError(
                    f"sample {index}: restored PAYLOAD_VERIFIED EXP lacks "
                    "source-reopen-skip evidence"
                )
            raise ValueError(
                f"sample {index}: source_reopen_skipped is true outside a "
                "restored PAYLOAD_VERIFIED EXP run"
            )
        if expected:
            payload_restores += 1
    return payload_restores


def selftest() -> None:
    valid = {
        "samples": [
            {
                "mode": "BASE",
                "workload_kind": "payload_verified_resume",
                "checkpoint_status": "not-applicable",
                "source_reopen_skipped": False,
            },
            {
                "mode": "EXP",
                "workload_kind": "payload_verified_resume",
                "checkpoint_status": "restored",
                "source_reopen_skipped": True,
            },
            {
                "mode": "EXP",
                "workload_kind": "payload_verified_resume",
                "checkpoint_status": "fallback",
                "source_reopen_skipped": False,
            },
            {
                "mode": "EXP",
                "workload_kind": "copy_resume",
                "checkpoint_status": "restored",
                "source_reopen_skipped": False,
            },
        ]
    }
    assert validate(valid) == 1

    bad = json.loads(json.dumps(valid))
    bad["samples"][1]["source_reopen_skipped"] = False
    try:
        validate(bad)
    except ValueError as error:
        assert "lacks" in str(error)
    else:
        raise AssertionError("missing source-free evidence must fail")

    bad = json.loads(json.dumps(valid))
    bad["samples"][3]["source_reopen_skipped"] = True
    try:
        validate(bad)
    except ValueError as error:
        assert "outside" in str(error)
    else:
        raise AssertionError("false source-free evidence must fail")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("samples", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("validate_resume_hash_samples selftest: PASS")
        return 0
    if args.samples is None:
        parser.error("samples is required unless --selftest is used")

    try:
        count = validate(json.loads(args.samples.read_text(encoding="utf-8")))
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"validate_resume_hash_samples: {error}", file=sys.stderr)
        return 2
    print(f"resume-hash sample evidence: PASS payload_source_free_restores={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
