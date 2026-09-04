#!/usr/bin/env python3
"""Validate and bind the active Phase-5 bounded HDDMETA verification A/B.

Frozen references:
- Phase-0 baseline: CI #666
- transaction workspace v1: CI #724
- workspace v1 + source-fingerprint malloc: CI #739

The active incremental change bounds forensic snapshot read-back scratch to
64 KiB while retaining exact byte-for-byte comparison.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

FROZEN = {
    "OFF": {
        "elf_sha256": "4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916",
        "elf_bytes": 632884,
        "irx_sha256": "f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751",
        "irx_bytes": 8405,
    },
    "ON": {
        "elf_sha256": "964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7",
        "elf_bytes": 638388,
        "irx_sha256": "8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db",
        "irx_bytes": 9861,
    },
}

FINGERPRINT_MALLOC = {
    "OFF": {
        "elf_sha256": "97e2a802952ae6f3b46c9fa0148359db8f8b69e22923f8105378f094de59c28b",
        "elf_bytes": 632756,
        "named_text": 229756,
        "instructions": 57488,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
    "ON": {
        "elf_sha256": "c8da50fe5147c3a24dc2f26d4ab910660bac615bf8e26f48e0bff3a2483f578b",
        "elf_bytes": 638132,
        "named_text": 232552,
        "instructions": 58189,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
}

MAX_PATCHES = 2048
SNAPSHOT_ENTRY_BYTES = 4 + 32 + 1024
SNAPSHOT_MAX_BYTES = 64 + MAX_PATCHES * SNAPSHOT_ENTRY_BYTES + 32
VERIFY_CHUNK_BYTES = 64 * 1024
BASELINE_MAX_PEAK_BYTES = SNAPSHOT_MAX_BYTES * 2
EXPERIMENT_MAX_PEAK_BYTES = SNAPSHOT_MAX_BYTES + VERIFY_CHUNK_BYTES
ORDER = ["BASE", "EXP", "EXP", "BASE", "EXP", "BASE", "BASE", "EXP"]


def info(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    return {"path": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def validate_frozen(label: str, elf: dict[str, object], irx: dict[str, object]) -> None:
    expected = FROZEN[label]
    if elf["sha256"] != expected["elf_sha256"] or elf["bytes"] != expected["elf_bytes"]:
        raise SystemExit(f"{label} baseline ELF is not the frozen Phase-0 binary")
    if irx["sha256"] != expected["irx_sha256"] or irx["bytes"] != expected["irx_bytes"]:
        raise SystemExit(f"{label} baseline IRX is not the frozen Phase-0 binary")


def sample_template(profile: str, baseline: dict, experiment: dict, irx: dict) -> dict:
    return {
        "experiment": "forensic-snapshot-bounded-readback",
        "profile": profile,
        "workload": "forensic-HDDMETA-save-existing-slot-and-new-slot-readback",
        "incremental_reference": FINGERPRINT_MALLOC[profile],
        "expected_source_change": {
            "snapshot_format_changed": False,
            "exact_byte_compare_preserved": True,
            "max_patch_count": MAX_PATCHES,
            "max_snapshot_bytes": SNAPSHOT_MAX_BYTES,
            "baseline_verify_allocation_bytes_at_max": SNAPSHOT_MAX_BYTES,
            "experiment_verify_allocation_bytes_at_max": VERIFY_CHUNK_BYTES,
            "baseline_image_plus_verify_peak_bytes_at_max": BASELINE_MAX_PEAK_BYTES,
            "experiment_image_plus_verify_peak_bytes_at_max": EXPERIMENT_MAX_PEAK_BYTES,
            "peak_reduction_bytes_at_max": BASELINE_MAX_PEAK_BYTES - EXPERIMENT_MAX_PEAK_BYTES,
            "iop_binary_change": False,
        },
        "baseline": baseline,
        "experiment_binary": experiment,
        "hdl_stream_irx": irx,
        "hardware": {
            "console_scp": "UNRECORDED",
            "hardware_revision": "UNRECORDED",
            "romver": "UNRECORDED",
            "storage_adapter": "UNRECORDED",
            "active_irx": "UNRECORDED",
        },
        "runs": [
            {
                "index": i + 1,
                "variant": variant,
                "patch_count": None,
                "snapshot_bytes": None,
                "existing_slot_match": None,
                "new_slot_write_readback_match": None,
                "elapsed_us": None,
                "correctness_hash": None,
                "result": None,
            }
            for i, variant in enumerate(ORDER)
        ],
        "report": {
            "elapsed_us": {"p50": None, "p95": None, "p99": None, "max": None},
            "correctness_failures": None,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-git-sha", required=True)
    parser.add_argument("--baseline-off", type=Path, default=Path("PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF"))
    parser.add_argument("--baseline-on", type=Path, default=Path("PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_ON.ELF"))
    parser.add_argument("--baseline-irx-off", type=Path, default=Path("HDL_STREAM_PROFILE_OFF.irx"))
    parser.add_argument("--baseline-irx-on", type=Path, default=Path("HDL_STREAM_PROFILE_ON.irx"))
    parser.add_argument("--experiment-off", type=Path, default=Path("PS2_HDD_BOOTSTRAP_MANAGER_TX_WORKSPACE_PROFILE_OFF.ELF"))
    parser.add_argument("--experiment-on", type=Path, default=Path("PS2_HDD_BOOTSTRAP_MANAGER_TX_WORKSPACE_PROFILE_ON.ELF"))
    parser.add_argument("--experiment-irx-off", type=Path, default=Path("HDL_STREAM_TX_WORKSPACE_PROFILE_OFF.irx"))
    parser.add_argument("--experiment-irx-on", type=Path, default=Path("HDL_STREAM_TX_WORKSPACE_PROFILE_ON.irx"))
    parser.add_argument("--identity-output", type=Path, default=Path("TRANSACTION_WORKSPACE_AB_IDENTITY.json"))
    parser.add_argument("--profile-off-template", type=Path, default=Path("TRANSACTION_WORKSPACE_AB_PROFILE_OFF_TEMPLATE.json"))
    parser.add_argument("--profile-on-template", type=Path, default=Path("TRANSACTION_WORKSPACE_AB_PROFILE_ON_TEMPLATE.json"))
    args = parser.parse_args()

    baseline_off = info(args.baseline_off)
    baseline_on = info(args.baseline_on)
    baseline_irx_off = info(args.baseline_irx_off)
    baseline_irx_on = info(args.baseline_irx_on)
    experiment_off = info(args.experiment_off)
    experiment_on = info(args.experiment_on)
    experiment_irx_off = info(args.experiment_irx_off)
    experiment_irx_on = info(args.experiment_irx_on)

    validate_frozen("OFF", baseline_off, baseline_irx_off)
    validate_frozen("ON", baseline_on, baseline_irx_on)
    if experiment_irx_off["sha256"] != baseline_irx_off["sha256"]:
        raise SystemExit("PROFILE OFF forensic experiment changed hdl_stream.irx")
    if experiment_irx_on["sha256"] != baseline_irx_on["sha256"]:
        raise SystemExit("PROFILE ON forensic experiment changed hdl_stream.irx")
    if experiment_off["sha256"] == FINGERPRINT_MALLOC["OFF"]["elf_sha256"]:
        raise SystemExit("PROFILE OFF forensic experiment did not change the EE ELF")
    if experiment_on["sha256"] == FINGERPRINT_MALLOC["ON"]["elf_sha256"]:
        raise SystemExit("PROFILE ON forensic experiment did not change the EE ELF")

    identity = {
        "experiment": "forensic-snapshot-bounded-readback",
        "project_git_sha": args.project_git_sha,
        "frozen_phase0_commit": "7875b14d837d6332f5edc37f1c12a55527d7dd87",
        "workspace_v1_frozen_ci": 724,
        "fingerprint_malloc_frozen_ci": 739,
        "storage_scratch_natural_rejected_ci": 743,
        "incremental_reference": FINGERPRINT_MALLOC,
        "ps2sdk_commit": "b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b",
        "toolchain": "mips64r5900el-ps2-elf GCC 15.2.0",
        "memory_model": {
            "max_patch_count": MAX_PATCHES,
            "max_snapshot_bytes": SNAPSHOT_MAX_BYTES,
            "verify_chunk_bytes": VERIFY_CHUNK_BYTES,
            "baseline_image_plus_verify_peak_bytes": BASELINE_MAX_PEAK_BYTES,
            "experiment_image_plus_verify_peak_bytes": EXPERIMENT_MAX_PEAK_BYTES,
            "peak_reduction_bytes": BASELINE_MAX_PEAK_BYTES - EXPERIMENT_MAX_PEAK_BYTES,
        },
        "correctness_contract": {
            "on_disk_format_changed": False,
            "slot_policy_changed": False,
            "overwrite_policy_changed": False,
            "exact_byte_compare_preserved": True,
        },
        "PROFILE_OFF": {"baseline_elf": baseline_off, "experiment_elf": experiment_off, "hdl_stream_irx": baseline_irx_off},
        "PROFILE_ON": {"baseline_elf": baseline_on, "experiment_elf": experiment_on, "hdl_stream_irx": baseline_irx_on},
    }
    args.identity_output.write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.profile_off_template.write_text(json.dumps(sample_template("OFF", baseline_off, experiment_off, baseline_irx_off), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.profile_on_template.write_text(json.dumps(sample_template("ON", baseline_on, experiment_on, baseline_irx_on), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
