#!/usr/bin/env python3
"""Compare same-source HDL PROFILE=0/1 real-hardware samples.

Input is a JSON array (or an object with a ``samples`` array). Each sample must
record ``mode`` (``OFF``/``ON``), ``project_git_sha``, the frozen mode-specific
ELF/IRX hashes, ``workload_id``, ``correctness_hash``, ``source_bytes`` and
``total_us``. ``copy_us`` and ``verify_us`` are optional, but a metric is
compared only when every accepted sample in both modes provides it.

The tool intentionally does not invent PROFILE=0 latency distributions from the
PROFILE=1 telemetry log. ``parse_hdl_perf.py`` remains the source for the rich
PROFILE=1 EE/IOP histograms; this script quantifies the profiler's end-to-end
perturbation.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

EXPECTED_BINARY_HASHES = {
    "ON": {
        "benchmark_elf_sha256": "964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7",
        "hdl_stream_irx_sha256": "8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db",
    },
    "OFF": {
        "benchmark_elf_sha256": "4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916",
        "hdl_stream_irx_sha256": "f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751",
    },
}

REQUIRED = (
    "mode",
    "project_git_sha",
    "benchmark_elf_sha256",
    "hdl_stream_irx_sha256",
    "workload_id",
    "correctness_hash",
    "source_bytes",
    "total_us",
)
OPTIONAL_TIMES = ("copy_us", "verify_us")


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


def _delta_percent(on_value: int, off_value: int) -> float:
    if off_value == 0:
        raise ValueError("PROFILE OFF baseline metric cannot be zero")
    return round((on_value - off_value) * 100.0 / off_value, 4)


def _throughput_kib_s(source_bytes: int, usec: int) -> int:
    if source_bytes <= 0 or usec <= 0:
        raise ValueError("source_bytes and time must be positive")
    return (source_bytes * 1_000_000) // (usec * 1024)


def _normalize(raw: Any) -> list[dict[str, Any]]:
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
        mode = str(sample["mode"]).upper()
        if mode not in ("OFF", "ON"):
            raise ValueError(f"sample {index}: mode must be OFF or ON")
        normalized = dict(sample)
        normalized["mode"] = mode
        for key in ("source_bytes", "total_us", *OPTIONAL_TIMES):
            if key in normalized:
                value = normalized[key]
                if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                    raise ValueError(f"sample {index}: {key} must be a positive integer")
        for key in (
            "project_git_sha",
            "benchmark_elf_sha256",
            "hdl_stream_irx_sha256",
            "workload_id",
            "correctness_hash",
        ):
            if not isinstance(normalized[key], str) or not normalized[key]:
                raise ValueError(f"sample {index}: {key} must be a non-empty string")

        expected = EXPECTED_BINARY_HASHES[mode]
        for key in ("benchmark_elf_sha256", "hdl_stream_irx_sha256"):
            if normalized[key].lower() != expected[key]:
                raise ValueError(
                    f"sample {index}: PROFILE {mode} {key} does not match frozen pair"
                )
            normalized[key] = normalized[key].lower()
        samples.append(normalized)
    return samples


def compare_samples(samples: list[dict[str, Any]], min_samples: int = 4) -> dict[str, Any]:
    groups = {mode: [sample for sample in samples if sample["mode"] == mode]
              for mode in ("OFF", "ON")}
    for mode, group in groups.items():
        if len(group) < min_samples:
            raise ValueError(
                f"PROFILE {mode} has {len(group)} samples; need at least {min_samples}"
            )

    shas = {sample["project_git_sha"] for sample in samples}
    workloads = {sample["workload_id"] for sample in samples}
    hashes = {sample["correctness_hash"] for sample in samples}
    source_sizes = {sample["source_bytes"] for sample in samples}
    if len(shas) != 1:
        raise ValueError("samples do not share one project_git_sha")
    if len(workloads) != 1:
        raise ValueError("samples do not share one workload_id")
    if len(hashes) != 1:
        raise ValueError("correctness_hash differs across samples")
    if len(source_sizes) != 1:
        raise ValueError("source_bytes differs across samples")

    source_bytes = next(iter(source_sizes))
    result: dict[str, Any] = {
        "project_git_sha": next(iter(shas)),
        "binary_hashes": EXPECTED_BINARY_HASHES,
        "workload_id": next(iter(workloads)),
        "correctness_hash": next(iter(hashes)),
        "source_bytes": source_bytes,
        "sample_counts": {mode: len(groups[mode]) for mode in ("OFF", "ON")},
        "metrics": {},
    }

    metric_sources: dict[str, str] = {"total_us": "total_us"}
    for key in OPTIONAL_TIMES:
        if all(key in sample for sample in samples):
            metric_sources[key] = key

    metrics: dict[str, Any] = result["metrics"]
    for label, key in metric_sources.items():
        off_values = [int(sample[key]) for sample in groups["OFF"]]
        on_values = [int(sample[key]) for sample in groups["ON"]]
        off_dist = _distribution(off_values)
        on_dist = _distribution(on_values)
        metrics[label] = {
            "off": off_dist,
            "on": on_dist,
            "on_vs_off_percent": {
                percentile: _delta_percent(on_dist[percentile], off_dist[percentile])
                for percentile in ("p50", "p95", "p99", "max")
            },
        }

    for timing_key, rate_label in (
        ("total_us", "end_to_end_kib_per_second"),
        ("copy_us", "copy_kib_per_second"),
    ):
        if timing_key not in metric_sources:
            continue
        off_rates = [_throughput_kib_s(source_bytes, int(sample[timing_key]))
                     for sample in groups["OFF"]]
        on_rates = [_throughput_kib_s(source_bytes, int(sample[timing_key]))
                    for sample in groups["ON"]]
        off_dist = _distribution(off_rates)
        on_dist = _distribution(on_rates)
        metrics[rate_label] = {
            "off": off_dist,
            "on": on_dist,
            "on_vs_off_percent": {
                percentile: _delta_percent(on_dist[percentile], off_dist[percentile])
                for percentile in ("p50", "p95", "p99", "max")
            },
        }

    return result


def selftest() -> None:
    samples: list[dict[str, Any]] = []
    for mode, totals, copies, verifies in (
        ("OFF", [1000, 1010, 990, 1005], [700, 705, 695, 700], [200, 205, 195, 200]),
        ("ON", [1020, 1030, 1010, 1025], [714, 719, 709, 714], [204, 209, 199, 204]),
    ):
        expected = EXPECTED_BINARY_HASHES[mode]
        for total, copy, verify in zip(totals, copies, verifies):
            samples.append({
                "mode": mode,
                "project_git_sha": "abc123",
                "benchmark_elf_sha256": expected["benchmark_elf_sha256"],
                "hdl_stream_irx_sha256": expected["hdl_stream_irx_sha256"],
                "workload_id": "fixture-iso-a",
                "correctness_hash": "deadbeef",
                "source_bytes": 1024 * 1024,
                "total_us": total,
                "copy_us": copy,
                "verify_us": verify,
            })
    normalized = _normalize(samples)
    result = compare_samples(normalized)
    assert result["sample_counts"] == {"OFF": 4, "ON": 4}
    assert result["metrics"]["total_us"]["off"]["p50"] == 1000
    assert result["metrics"]["total_us"]["on"]["p50"] == 1020
    assert result["metrics"]["total_us"]["on_vs_off_percent"]["p50"] == 2.0
    assert result["metrics"]["copy_us"]["on_vs_off_percent"]["p50"] == 2.0
    assert result["metrics"]["end_to_end_kib_per_second"]["off"]["samples"] == 4

    broken_sha = [dict(sample) for sample in samples]
    broken_sha[-1]["project_git_sha"] = "different"
    try:
        compare_samples(_normalize(broken_sha))
    except ValueError as error:
        assert "project_git_sha" in str(error)
    else:
        raise AssertionError("mismatched project SHA must fail")

    broken_binary = [dict(sample) for sample in samples]
    broken_binary[0]["benchmark_elf_sha256"] = "0" * 64
    try:
        _normalize(broken_binary)
    except ValueError as error:
        assert "frozen pair" in str(error)
    else:
        raise AssertionError("mismatched binary hash must fail")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("samples", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--min-samples", type=int, default=4)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("compare_hdl_profile_ab selftest: PASS")
        return 0
    if args.samples is None:
        parser.error("samples is required unless --selftest is used")
    if args.min_samples < 1:
        parser.error("--min-samples must be positive")

    try:
        raw = json.loads(args.samples.read_text(encoding="utf-8"))
        samples = _normalize(raw)
        compared = compare_samples(samples, args.min_samples)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"compare_hdl_profile_ab: {error}", file=sys.stderr)
        return 2

    rendered = json.dumps(compared, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
