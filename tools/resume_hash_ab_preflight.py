#!/usr/bin/env python3
"""Bind the optional HDL resume-hash experiment to the frozen A/B baseline.

The checkpoint experiment is EE-only. A valid CI artifact must therefore keep
both frozen baseline ELFs exact, build one checkpoint ELF for PROFILE OFF and
one for PROFILE ON, and keep each experiment's embedded hdl_stream IRX byte-
identical to the corresponding frozen profiler mode.

The emitted identity and sample templates are the authoritative bridge between
CI artifacts and real-PS2 measurements. Experiment ELF hashes are intentionally
computed from the current artifact rather than hard-coded forever; any runtime
change therefore creates a new identity instead of silently inheriting an old
benchmark label.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path
from typing import Any

FROZEN_SOURCE_GIT_SHA = "7875b14d837d6332f5edc37f1c12a55527d7dd87"
FROZEN = {
    "ON": {
        "elf_bytes": 638388,
        "elf_sha256": "964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7",
        "irx_bytes": 9861,
        "irx_sha256": "8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db",
    },
    "OFF": {
        "elf_bytes": 632884,
        "elf_sha256": "4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916",
        "irx_bytes": 8405,
        "irx_sha256": "f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751",
    },
}
DEFAULT_ORDER = ("BASE", "EXP", "EXP", "BASE", "EXP", "BASE", "BASE", "EXP")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_blob(path: Path) -> dict[str, Any]:
    return {"bytes": path.stat().st_size, "sha256": _sha256(path)}


def verify_frozen(path: Path, mode: str, kind: str) -> dict[str, Any]:
    observed = inspect_blob(path)
    expected_bytes = int(FROZEN[mode][f"{kind}_bytes"])
    expected_sha = str(FROZEN[mode][f"{kind}_sha256"])
    if observed["bytes"] != expected_bytes:
        raise ValueError(
            f"{path}: {mode} {kind} size {observed['bytes']}, expected {expected_bytes}"
        )
    if observed["sha256"] != expected_sha:
        raise ValueError(
            f"{path}: {mode} {kind} sha256 {observed['sha256']}, expected {expected_sha}"
        )
    return observed


def build_identity(
    project_git_sha: str,
    baseline: dict[str, dict[str, Any]],
    experiment: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    if not project_git_sha:
        raise ValueError("project_git_sha must be non-empty")

    profiles: dict[str, Any] = {}
    for mode in ("OFF", "ON"):
        base = baseline[mode]
        exp = experiment[mode]
        if exp["elf"]["sha256"] == base["elf"]["sha256"]:
            raise ValueError(f"PROFILE {mode} experiment ELF is byte-identical to baseline")
        if exp["irx"] != base["irx"]:
            raise ValueError(
                f"PROFILE {mode} experiment IRX differs from frozen baseline; "
                "resume-hash must remain EE-only"
            )
        profiles[mode] = {
            "baseline": base,
            "experiment": exp,
        }

    return {
        "project_git_sha": project_git_sha,
        "frozen_baseline_source_git_sha": FROZEN_SOURCE_GIT_SHA,
        "experiment": "HDL_RESUME_HASH_CHECKPOINT=1",
        "profiles": profiles,
    }


def sample_template(identity: dict[str, Any], profile: str) -> dict[str, Any]:
    profile = profile.upper()
    if profile not in ("OFF", "ON"):
        raise ValueError("profile must be OFF or ON")
    pair = identity["profiles"][profile]
    project_git_sha = identity["project_git_sha"]
    samples: list[dict[str, Any]] = []
    for run, mode in enumerate(DEFAULT_ORDER, start=1):
        variant = pair["baseline" if mode == "BASE" else "experiment"]
        samples.append({
            "run": run,
            "profile": profile,
            "mode": mode,
            "project_git_sha": project_git_sha,
            "benchmark_elf_sha256": variant["elf"]["sha256"],
            "hdl_stream_irx_sha256": variant["irx"]["sha256"],
            "workload_kind": "FILL_ME",
            "workload_id": "FILL_ME",
            "correctness_hash": "FILL_ME",
            "source_bytes": 0,
            "total_us": 0,
            "copy_us": 0,
            "verify_us": 0,
            "resume_gate_us": 0,
            "completed_sectors": 0,
            "checkpoint_status": "FILL_ME",
            "checkpoint_restored_bytes": 0,
        })
    return {
        "identity": {
            "project_git_sha": project_git_sha,
            "profile": profile,
            "baseline_elf_sha256": pair["baseline"]["elf"]["sha256"],
            "experiment_elf_sha256": pair["experiment"]["elf"]["sha256"],
            "hdl_stream_irx_sha256": pair["baseline"]["irx"]["sha256"],
        },
        "note": (
            "Replace every FILL_ME and zero measurement. For recovery workloads, "
            "checkpoint_status is restored or fallback for EXP and not-applicable "
            "for BASE. Keep one workload/depth per comparator input."
        ),
        "samples": samples,
    }


def selftest() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "fixture.bin"
        payload = b"resume-hash-preflight\n"
        path.write_bytes(payload)
        observed = inspect_blob(path)
        assert observed["bytes"] == len(payload)
        assert observed["sha256"] == hashlib.sha256(payload).hexdigest()

    baseline: dict[str, dict[str, Any]] = {}
    experiment: dict[str, dict[str, Any]] = {}
    for mode in ("OFF", "ON"):
        base_irx = {"bytes": 10 if mode == "OFF" else 11, "sha256": mode.lower() * 32}
        baseline[mode] = {
            "elf": {"bytes": 100, "sha256": ("a" if mode == "OFF" else "b") * 64},
            "irx": base_irx,
        }
        experiment[mode] = {
            "elf": {"bytes": 110, "sha256": ("c" if mode == "OFF" else "d") * 64},
            "irx": dict(base_irx),
        }
    identity = build_identity("head-fixture", baseline, experiment)
    template = sample_template(identity, "OFF")
    assert len(template["samples"]) == 8
    assert sum(sample["mode"] == "BASE" for sample in template["samples"]) == 4
    assert sum(sample["mode"] == "EXP" for sample in template["samples"]) == 4

    broken = {mode: {key: dict(value) if isinstance(value, dict) else value
                     for key, value in experiment[mode].items()}
              for mode in experiment}
    broken["OFF"]["irx"] = {"bytes": 99, "sha256": "e" * 64}
    try:
        build_identity("head-fixture", baseline, broken)
    except ValueError as error:
        assert "IRX differs" in str(error)
    else:
        raise AssertionError("IRX drift must fail")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-profile-on", type=Path,
                        default=Path("PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_ON.ELF"))
    parser.add_argument("--base-profile-off", type=Path,
                        default=Path("PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF"))
    parser.add_argument("--base-irx-on", type=Path,
                        default=Path("HDL_STREAM_PROFILE_ON.irx"))
    parser.add_argument("--base-irx-off", type=Path,
                        default=Path("HDL_STREAM_PROFILE_OFF.irx"))
    parser.add_argument("--exp-profile-on", type=Path,
                        default=Path("PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_ON.ELF"))
    parser.add_argument("--exp-profile-off", type=Path,
                        default=Path("PS2_HDD_BOOTSTRAP_MANAGER_RESUME_HASH_PROFILE_OFF.ELF"))
    parser.add_argument("--exp-irx-on", type=Path,
                        default=Path("HDL_STREAM_RESUME_HASH_PROFILE_ON.irx"))
    parser.add_argument("--exp-irx-off", type=Path,
                        default=Path("HDL_STREAM_RESUME_HASH_PROFILE_OFF.irx"))
    parser.add_argument("--project-git-sha", default=FROZEN_SOURCE_GIT_SHA)
    parser.add_argument("--identity-output", type=Path)
    parser.add_argument("--profile-off-template", type=Path)
    parser.add_argument("--profile-on-template", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("resume_hash_ab_preflight selftest: PASS")
        return 0

    try:
        baseline = {
            "OFF": {
                "elf": verify_frozen(args.base_profile_off, "OFF", "elf"),
                "irx": verify_frozen(args.base_irx_off, "OFF", "irx"),
            },
            "ON": {
                "elf": verify_frozen(args.base_profile_on, "ON", "elf"),
                "irx": verify_frozen(args.base_irx_on, "ON", "irx"),
            },
        }
        experiment = {
            "OFF": {
                "elf": inspect_blob(args.exp_profile_off),
                "irx": inspect_blob(args.exp_irx_off),
            },
            "ON": {
                "elf": inspect_blob(args.exp_profile_on),
                "irx": inspect_blob(args.exp_irx_on),
            },
        }
        identity = build_identity(args.project_git_sha, baseline, experiment)
    except (OSError, ValueError) as error:
        print(f"resume_hash_ab_preflight: {error}", file=sys.stderr)
        return 2

    for profile in ("OFF", "ON"):
        pair = identity["profiles"][profile]
        print(
            f"PROFILE {profile} BASE {pair['baseline']['elf']['bytes']} B "
            f"{pair['baseline']['elf']['sha256']}"
        )
        print(
            f"PROFILE {profile} EXP  {pair['experiment']['elf']['bytes']} B "
            f"{pair['experiment']['elf']['sha256']}"
        )
        print(
            f"PROFILE {profile} IRX  PASS {pair['experiment']['irx']['sha256']}"
        )

    if args.identity_output:
        args.identity_output.write_text(
            json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    if args.profile_off_template:
        args.profile_off_template.write_text(
            json.dumps(sample_template(identity, "OFF"), indent=2) + "\n",
            encoding="utf-8",
        )
    if args.profile_on_template:
        args.profile_on_template.write_text(
            json.dumps(sample_template(identity, "ON"), indent=2) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
