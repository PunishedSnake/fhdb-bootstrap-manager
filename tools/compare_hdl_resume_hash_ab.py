#!/usr/bin/env python3
"""Compare real-PS2 HDL resume-hash checkpoint A/B samples.

The identity JSON must come from ``resume_hash_ab_preflight.py`` in the same CI
artifact as the tested ELFs. This prevents a rebuilt or unrelated experiment
from inheriting another artifact's measurements.

For recovery workloads, experiment runs that report ``checkpoint_status`` as
``fallback`` are counted but excluded from optimized timing distributions. A
safe fallback is a correctness success, not evidence that checkpoint restore
was slow.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

REQUIRED = (
    "profile",
    "mode",
    "project_git_sha",
    "benchmark_elf_sha256",
    "hdl_stream_irx_sha256",
    "workload_kind",
    "workload_id",
    "correctness_hash",
    "source_bytes",
    "total_us",
    "checkpoint_status",
    "checkpoint_restored_bytes",
)
OPTIONAL_TIMES = ("copy_us", "verify_us", "resume_gate_us")
WORKLOAD_KINDS = ("uninterrupted", "copy_resume", "payload_verified_resume")
CHECKPOINT_STATUSES = ("not-applicable", "restored", "fallback")


def _percentile(values: list[int], percentile: int) -> int:
    if not values:
        raise ValueError("percentile requires at least one value")
    ordered = sorted(values)
    rank = max(1, math.ceil(len(ordered) * percentile / 100.0))
    return ordered[rank - 1]


def _distribution(values: list[int]) -> dict[str, int]:
    return {
        "samples": len(values),
        "p50": _percentile(values, 50),
        "p95": _percentile(values, 95),
        "p99": _percentile(values, 99),
        "max": max(values),
    }


def _delta_percent(exp_value: int, base_value: int) -> float:
    if base_value == 0:
        raise ValueError("baseline metric cannot be zero")
    return round((exp_value - base_value) * 100.0 / base_value, 4)


def _throughput_kib_s(source_bytes: int, usec: int) -> int:
    if source_bytes <= 0 or usec <= 0:
        raise ValueError("source_bytes and time must be positive")
    return (source_bytes * 1_000_000) // (usec * 1024)


def _load_identity(raw: Any) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise ValueError("identity must be an object")
    if not isinstance(raw.get("project_git_sha"), str) or not raw["project_git_sha"]:
        raise ValueError("identity project_git_sha must be non-empty")
    profiles = raw.get("profiles")
    if not isinstance(profiles, dict):
        raise ValueError("identity profiles must be an object")
    for profile in ("OFF", "ON"):
        pair = profiles.get(profile)
        if not isinstance(pair, dict):
            raise ValueError(f"identity missing PROFILE {profile}")
        for variant in ("baseline", "experiment"):
            entry = pair.get(variant)
            if not isinstance(entry, dict):
                raise ValueError(f"identity PROFILE {profile} missing {variant}")
            for kind in ("elf", "irx"):
                blob = entry.get(kind)
                if not isinstance(blob, dict):
                    raise ValueError(
                        f"identity PROFILE {profile} {variant} missing {kind}"
                    )
                if not isinstance(blob.get("sha256"), str) or not blob["sha256"]:
                    raise ValueError(
                        f"identity PROFILE {profile} {variant} {kind} sha256 invalid"
                    )
    return raw


def _positive_int(sample: dict[str, Any], key: str, index: int) -> None:
    value = sample[key]
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ValueError(f"sample {index}: {key} must be a positive integer")


def _nonnegative_int(sample: dict[str, Any], key: str, index: int) -> None:
    value = sample[key]
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError(f"sample {index}: {key} must be a non-negative integer")


def _normalize(raw: Any, identity: dict[str, Any]) -> list[dict[str, Any]]:
    if isinstance(raw, dict):
        raw = raw.get("samples")
    if not isinstance(raw, list) or not raw:
        raise ValueError("input must contain a non-empty samples array")

    samples: list[dict[str, Any]] = []
    for index, sample in enumerate(raw):
        if not isinstance(sample, dict):
            raise ValueError(f"sample {index}: expected object")
        missing = [key for key in REQUIRED if key not in sample]
        if missing:
            raise ValueError(f"sample {index}: missing {', '.join(missing)}")

        normalized = dict(sample)
        profile = str(normalized["profile"]).upper()
        mode = str(normalized["mode"]).upper()
        workload_kind = str(normalized["workload_kind"])
        checkpoint_status = str(normalized["checkpoint_status"])
        if profile not in ("OFF", "ON"):
            raise ValueError(f"sample {index}: profile must be OFF or ON")
        if mode not in ("BASE", "EXP"):
            raise ValueError(f"sample {index}: mode must be BASE or EXP")
        if workload_kind not in WORKLOAD_KINDS:
            raise ValueError(
                f"sample {index}: workload_kind must be one of {', '.join(WORKLOAD_KINDS)}"
            )
        if checkpoint_status not in CHECKPOINT_STATUSES:
            raise ValueError(
                f"sample {index}: invalid checkpoint_status {checkpoint_status}"
            )
        if mode == "BASE" and checkpoint_status != "not-applicable":
            raise ValueError(
                f"sample {index}: BASE checkpoint_status must be not-applicable"
            )
        if workload_kind == "uninterrupted" and checkpoint_status != "not-applicable":
            raise ValueError(
                f"sample {index}: uninterrupted runs cannot report checkpoint restore"
            )
        if workload_kind != "uninterrupted" and mode == "EXP" and \
                checkpoint_status == "not-applicable":
            raise ValueError(
                f"sample {index}: recovery EXP must report restored or fallback"
            )

        normalized["profile"] = profile
        normalized["mode"] = mode
        normalized["workload_kind"] = workload_kind
        normalized["checkpoint_status"] = checkpoint_status

        for key in ("source_bytes", "total_us"):
            _positive_int(normalized, key, index)
        for key in OPTIONAL_TIMES:
            if key in normalized:
                _positive_int(normalized, key, index)
        for key in ("checkpoint_restored_bytes", "completed_sectors"):
            if key in normalized:
                _nonnegative_int(normalized, key, index)

        for key in (
            "project_git_sha",
            "benchmark_elf_sha256",
            "hdl_stream_irx_sha256",
            "workload_id",
            "correctness_hash",
        ):
            if not isinstance(normalized[key], str) or not normalized[key]:
                raise ValueError(f"sample {index}: {key} must be a non-empty string")

        if normalized["project_git_sha"] != identity["project_git_sha"]:
            raise ValueError(f"sample {index}: project_git_sha does not match identity")
        pair = identity["profiles"][profile]
        variant = pair["baseline" if mode == "BASE" else "experiment"]
        if normalized["benchmark_elf_sha256"].lower() != variant["elf"]["sha256"]:
            raise ValueError(f"sample {index}: ELF hash does not match identity")
        if normalized["hdl_stream_irx_sha256"].lower() != variant["irx"]["sha256"]:
            raise ValueError(f"sample {index}: IRX hash does not match identity")
        normalized["benchmark_elf_sha256"] = normalized["benchmark_elf_sha256"].lower()
        normalized["hdl_stream_irx_sha256"] = normalized["hdl_stream_irx_sha256"].lower()
        samples.append(normalized)
    return samples


def compare_samples(
    samples: list[dict[str, Any]], identity: dict[str, Any], min_samples: int = 4
) -> dict[str, Any]:
    profiles = {sample["profile"] for sample in samples}
    workload_kinds = {sample["workload_kind"] for sample in samples}
    workloads = {sample["workload_id"] for sample in samples}
    correctness_hashes = {sample["correctness_hash"] for sample in samples}
    source_sizes = {sample["source_bytes"] for sample in samples}
    project_shas = {sample["project_git_sha"] for sample in samples}
    if len(profiles) != 1:
        raise ValueError("compare one PROFILE mode at a time")
    if len(workload_kinds) != 1:
        raise ValueError("compare one workload_kind at a time")
    if len(workloads) != 1:
        raise ValueError("compare one workload_id/depth at a time")
    if len(correctness_hashes) != 1:
        raise ValueError("correctness_hash differs across samples")
    if len(source_sizes) != 1:
        raise ValueError("source_bytes differs across samples")
    if project_shas != {identity["project_git_sha"]}:
        raise ValueError("project_git_sha differs from identity")

    profile = next(iter(profiles))
    workload_kind = next(iter(workload_kinds))
    source_bytes = int(next(iter(source_sizes)))
    groups = {
        mode: [sample for sample in samples if sample["mode"] == mode]
        for mode in ("BASE", "EXP")
    }
    if len(groups["BASE"]) < min_samples:
        raise ValueError(
            f"BASE has {len(groups['BASE'])} samples; need at least {min_samples}"
        )

    fallback_samples = [
        sample for sample in groups["EXP"] if sample["checkpoint_status"] == "fallback"
    ]
    if workload_kind == "uninterrupted":
        accepted_exp = groups["EXP"]
    else:
        accepted_exp = [
            sample for sample in groups["EXP"]
            if sample["checkpoint_status"] == "restored"
        ]
    if len(accepted_exp) < min_samples:
        raise ValueError(
            f"accepted EXP has {len(accepted_exp)} samples after fallback separation; "
            f"need at least {min_samples}"
        )

    expected_restored_bytes = 0
    completed_sectors = None
    if workload_kind == "copy_resume":
        sectors = {sample.get("completed_sectors") for sample in samples}
        if None in sectors or len(sectors) != 1:
            raise ValueError("copy_resume samples must share one completed_sectors value")
        completed_sectors = int(next(iter(sectors)))
        if completed_sectors <= 0:
            raise ValueError("copy_resume completed_sectors must be positive")
        expected_restored_bytes = completed_sectors * 2048
    elif workload_kind == "payload_verified_resume":
        expected_restored_bytes = source_bytes

    if expected_restored_bytes:
        for sample in accepted_exp:
            if int(sample["checkpoint_restored_bytes"]) != expected_restored_bytes:
                raise ValueError(
                    "restored EXP sample does not report the expected skipped source bytes"
                )
        for sample in fallback_samples:
            if int(sample["checkpoint_restored_bytes"]) != 0:
                raise ValueError("fallback EXP sample must report zero restored bytes")

    result: dict[str, Any] = {
        "project_git_sha": identity["project_git_sha"],
        "profile": profile,
        "workload_kind": workload_kind,
        "workload_id": next(iter(workloads)),
        "correctness_hash": next(iter(correctness_hashes)),
        "source_bytes": source_bytes,
        "completed_sectors": completed_sectors,
        "expected_checkpoint_restored_bytes": expected_restored_bytes,
        "sample_counts": {
            "base": len(groups["BASE"]),
            "exp_total": len(groups["EXP"]),
            "exp_accepted": len(accepted_exp),
            "exp_fallback": len(fallback_samples),
        },
        "binary_identity": identity["profiles"][profile],
        "metrics": {},
    }

    compared = groups["BASE"] + accepted_exp
    metric_keys = ["total_us"]
    for key in OPTIONAL_TIMES:
        if all(key in sample for sample in compared):
            metric_keys.append(key)

    metrics: dict[str, Any] = result["metrics"]
    for key in metric_keys:
        base_values = [int(sample[key]) for sample in groups["BASE"]]
        exp_values = [int(sample[key]) for sample in accepted_exp]
        base_dist = _distribution(base_values)
        exp_dist = _distribution(exp_values)
        metrics[key] = {
            "base": base_dist,
            "experiment": exp_dist,
            "experiment_vs_base_percent": {
                percentile: _delta_percent(exp_dist[percentile], base_dist[percentile])
                for percentile in ("p50", "p95", "p99", "max")
            },
        }

    if workload_kind == "uninterrupted" and "copy_us" in metric_keys:
        base_rates = [
            _throughput_kib_s(source_bytes, int(sample["copy_us"]))
            for sample in groups["BASE"]
        ]
        exp_rates = [
            _throughput_kib_s(source_bytes, int(sample["copy_us"]))
            for sample in accepted_exp
        ]
        base_dist = _distribution(base_rates)
        exp_dist = _distribution(exp_rates)
        metrics["copy_kib_per_second"] = {
            "base": base_dist,
            "experiment": exp_dist,
            "experiment_vs_base_percent": {
                percentile: _delta_percent(exp_dist[percentile], base_dist[percentile])
                for percentile in ("p50", "p95", "p99", "max")
            },
        }

    return result


def selftest() -> None:
    identity = {
        "project_git_sha": "head-fixture",
        "profiles": {
            "OFF": {
                "baseline": {
                    "elf": {"sha256": "a" * 64, "bytes": 100},
                    "irx": {"sha256": "b" * 64, "bytes": 10},
                },
                "experiment": {
                    "elf": {"sha256": "c" * 64, "bytes": 110},
                    "irx": {"sha256": "b" * 64, "bytes": 10},
                },
            },
            "ON": {
                "baseline": {
                    "elf": {"sha256": "d" * 64, "bytes": 120},
                    "irx": {"sha256": "e" * 64, "bytes": 11},
                },
                "experiment": {
                    "elf": {"sha256": "f" * 64, "bytes": 130},
                    "irx": {"sha256": "e" * 64, "bytes": 11},
                },
            },
        },
    }
    identity = _load_identity(identity)
    samples: list[dict[str, Any]] = []
    expected = 16384 * 2048
    for mode, totals in (
        ("BASE", [5000, 5100, 4900, 5050]),
        ("EXP", [1000, 1100, 900, 1050]),
    ):
        variant = identity["profiles"]["ON"][
            "baseline" if mode == "BASE" else "experiment"
        ]
        for total in totals:
            samples.append({
                "profile": "ON",
                "mode": mode,
                "project_git_sha": "head-fixture",
                "benchmark_elf_sha256": variant["elf"]["sha256"],
                "hdl_stream_irx_sha256": variant["irx"]["sha256"],
                "workload_kind": "copy_resume",
                "workload_id": "iso-a-depth-1",
                "correctness_hash": "deadbeef",
                "source_bytes": 1024 * 1024 * 1024,
                "total_us": total,
                "resume_gate_us": total // 2,
                "completed_sectors": 16384,
                "checkpoint_status": "not-applicable" if mode == "BASE" else "restored",
                "checkpoint_restored_bytes": 0 if mode == "BASE" else expected,
            })
    normalized = _normalize(samples, identity)
    result = compare_samples(normalized, identity)
    assert result["sample_counts"]["base"] == 4
    assert result["sample_counts"]["exp_accepted"] == 4
    assert result["expected_checkpoint_restored_bytes"] == expected
    assert result["metrics"]["total_us"]["experiment"]["p50"] == 1000

    fallback = [dict(sample) for sample in samples]
    fallback[-1]["checkpoint_status"] = "fallback"
    fallback[-1]["checkpoint_restored_bytes"] = 0
    try:
        compare_samples(_normalize(fallback, identity), identity)
    except ValueError as error:
        assert "accepted EXP" in str(error)
    else:
        raise AssertionError("fallback must not count toward optimized minimum")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("identity", nargs="?", type=Path)
    parser.add_argument("samples", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--min-samples", type=int, default=4)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("compare_hdl_resume_hash_ab selftest: PASS")
        return 0
    if args.identity is None or args.samples is None:
        parser.error("identity and samples are required unless --selftest is used")
    if args.min_samples < 1:
        parser.error("--min-samples must be positive")

    try:
        identity = _load_identity(
            json.loads(args.identity.read_text(encoding="utf-8"))
        )
        samples = _normalize(
            json.loads(args.samples.read_text(encoding="utf-8")), identity
        )
        result = compare_samples(samples, identity, args.min_samples)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"compare_hdl_resume_hash_ab: {error}", file=sys.stderr)
        return 2

    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
