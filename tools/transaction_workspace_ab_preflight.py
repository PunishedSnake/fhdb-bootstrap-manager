#!/usr/bin/env python3
"""Bind the active cold-isolated streaming APAMETA1 v3 experiment.

V3 changes only code placement relative to streaming v2 (CI #762): the exported
forensic snapshot save boundary is `cold,noinline` so LTO cannot inflate the
repair-plan UI controller. Streaming representation, fileXio sequencing,
workspace ownership and exact read-back semantics remain unchanged.
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

STREAM_V2 = {
    "OFF": {
        "elf_sha256": "161b68d578e4ac0cbe1a37b34259d630f22822f43973c8462beb169fd494e223",
        "elf_bytes": 634420,
        "section_text": 288077,
        "named_text": 231436,
        "instructions": 57911,
        "repair_plan_screen_bytes": 6916,
        "repair_plan_screen_instructions": 1730,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
    "ON": {
        "elf_sha256": "08e1694c9fafef6db0f09fc1e696baed3bb5518c675914ca48515e9a8ba10898",
        "elf_bytes": 639924,
        "section_text": 292037,
        "named_text": 234232,
        "instructions": 58610,
        "repair_plan_screen_bytes": 6916,
        "repair_plan_screen_instructions": 1730,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
}

MAX_PATCHES = 2048
SNAPSHOT_MAX_BYTES = 64 + MAX_PATCHES * (4 + 32 + 1024) + 32
STREAM_CHUNK_BYTES = 64 * 1024
DIGEST_CACHE_BYTES_MAX = MAX_PATCHES * 32
STREAM_WORKSPACE_BYTES_MAX = STREAM_CHUNK_BYTES * 2 + DIGEST_CACHE_BYTES_MAX
BOUNDED_V2_PEAK_BYTES = SNAPSHOT_MAX_BYTES + STREAM_CHUNK_BYTES
ORIGINAL_PAIR_PEAK_BYTES = SNAPSHOT_MAX_BYTES * 2
ORDER = ["BASE", "EXP", "EXP", "BASE", "EXP", "BASE", "BASE", "EXP"]


def file_info(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    return {"path": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def validate_frozen(label: str, elf: dict[str, object], irx: dict[str, object]) -> None:
    expected = FROZEN[label]
    if elf["sha256"] != expected["elf_sha256"] or elf["bytes"] != expected["elf_bytes"]:
        raise SystemExit(f"{label} baseline ELF is not the frozen Phase-0 binary")
    if irx["sha256"] != expected["irx_sha256"] or irx["bytes"] != expected["irx_bytes"]:
        raise SystemExit(f"{label} baseline IRX is not frozen Phase-0 identity")


def sample_template(profile: str, baseline: dict, experiment: dict, irx: dict) -> dict:
    return {
        "experiment": "forensic-snapshot-streaming-apameta1-v3-cold",
        "profile": profile,
        "workload": "forensic-HDDMETA-save-existing-slot-and-new-slot-readback",
        "stream_v2_reference": STREAM_V2[profile],
        "expected_source_change": {
            "dataflow_changed_from_stream_v2": False,
            "cold_noinline_boundary_added": True,
            "snapshot_format_changed": False,
            "exact_byte_compare_preserved": True,
            "stream_workspace_bytes_max": STREAM_WORKSPACE_BYTES_MAX,
            "peak_reduction_vs_bounded_v2": BOUNDED_V2_PEAK_BYTES - STREAM_WORKSPACE_BYTES_MAX,
            "peak_reduction_vs_original_full_pair": ORIGINAL_PAIR_PEAK_BYTES - STREAM_WORKSPACE_BYTES_MAX,
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

    baseline = {"OFF": file_info(args.baseline_off), "ON": file_info(args.baseline_on)}
    irx = {"OFF": file_info(args.baseline_irx_off), "ON": file_info(args.baseline_irx_on)}
    experiment = {"OFF": file_info(args.experiment_off), "ON": file_info(args.experiment_on)}
    experiment_irx = {"OFF": file_info(args.experiment_irx_off), "ON": file_info(args.experiment_irx_on)}

    for label in ("OFF", "ON"):
        validate_frozen(label, baseline[label], irx[label])
        if experiment_irx[label]["sha256"] != irx[label]["sha256"]:
            raise SystemExit(f"PROFILE {label} streaming v3 changed hdl_stream.irx")
        if experiment[label]["sha256"] == STREAM_V2[label]["elf_sha256"]:
            raise SystemExit(f"PROFILE {label} cold boundary did not change EE ELF from streaming v2")

    identity = {
        "experiment": "forensic-snapshot-streaming-apameta1-v3-cold",
        "project_git_sha": args.project_git_sha,
        "frozen_phase0_commit": "7875b14d837d6332f5edc37f1c12a55527d7dd87",
        "bounded_readback_v2_frozen_ci": 752,
        "streaming_v2_frozen_ci": 762,
        "stream_v2_reference": STREAM_V2,
        "reference_vector": {
            "format": "APAMETA1",
            "bytes": 2216,
            "image_sha256": "601ba74fc619738dac19baa2a6cb53054b67803e00b1fccb6bf89c69ef4bab6f",
        },
        "memory_model": {
            "max_snapshot_bytes": SNAPSHOT_MAX_BYTES,
            "stream_chunk_bytes": STREAM_CHUNK_BYTES,
            "digest_cache_bytes_max": DIGEST_CACHE_BYTES_MAX,
            "stream_workspace_bytes_max": STREAM_WORKSPACE_BYTES_MAX,
            "bounded_v2_peak_bytes": BOUNDED_V2_PEAK_BYTES,
            "original_full_pair_peak_bytes": ORIGINAL_PAIR_PEAK_BYTES,
            "peak_reduction_vs_bounded_v2": BOUNDED_V2_PEAK_BYTES - STREAM_WORKSPACE_BYTES_MAX,
            "peak_reduction_vs_original_full_pair": ORIGINAL_PAIR_PEAK_BYTES - STREAM_WORKSPACE_BYTES_MAX,
        },
        "code_placement": {
            "forensic_snapshot_save": "cold,noinline",
            "reason": "prevent streaming serializer growth from inflating repair_plan_screen through LTO",
        },
        "correctness_contract": {
            "on_disk_format_changed": False,
            "slot_policy_changed": False,
            "overwrite_policy_changed": False,
            "per_header_sha256_preserved": True,
            "trailer_sha256_preserved": True,
            "truncation_detection_preserved": True,
            "trailing_data_detection_preserved": True,
            "exact_byte_compare_preserved": True,
        },
        "PROFILE_OFF": {"baseline_elf": baseline["OFF"], "experiment_elf": experiment["OFF"], "hdl_stream_irx": irx["OFF"]},
        "PROFILE_ON": {"baseline_elf": baseline["ON"], "experiment_elf": experiment["ON"], "hdl_stream_irx": irx["ON"]},
    }
    args.identity_output.write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.profile_off_template.write_text(json.dumps(sample_template("OFF", baseline["OFF"], experiment["OFF"], irx["OFF"]), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.profile_on_template.write_text(json.dumps(sample_template("ON", baseline["ON"], experiment["ON"], irx["ON"]), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
