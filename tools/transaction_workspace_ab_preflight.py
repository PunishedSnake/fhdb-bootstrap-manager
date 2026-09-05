#!/usr/bin/env python3
"""Validate and bind the active Phase-5 streaming APAMETA1 A/B.

Frozen references:
- Phase-0 baseline: CI #666
- transaction workspace v1: CI #724
- workspace v1 + source-fingerprint malloc: CI #739
- bounded HDDMETA read-back v1: CI #749
- bounded HDDMETA read-back v2: CI #752 (runtime identity re-proved by CI #757)

The active experiment removes the complete canonical snapshot allocation and
uses one bounded workspace for serialized write chunks, exact read-back chunks
and cached per-entry SHA-256 digests.
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

BOUNDED_V2 = {
    "OFF": {
        "elf_sha256": "ecd99a7aee199039146cfa8275d2ecbe360b9b486bb290adb3bd30d86ae10a54",
        "elf_bytes": 633012,
        "section_text": 286757,
        "named_text": 230076,
        "instructions": 57571,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
    "ON": {
        "elf_sha256": "3ace7ea8730dc7dd56fe6bea078b2aeedc1e7735c5bcc831e7d7b883f65bdd2f",
        "elf_bytes": 638516,
        "section_text": 290717,
        "named_text": 232872,
        "instructions": 58270,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
}

MAX_PATCHES = 2048
SNAPSHOT_ENTRY_BYTES = 4 + 32 + 1024
SNAPSHOT_MAX_BYTES = 64 + MAX_PATCHES * SNAPSHOT_ENTRY_BYTES + 32
STREAM_CHUNK_BYTES = 64 * 1024
DIGEST_CACHE_BYTES_MAX = MAX_PATCHES * 32
STREAM_WORKSPACE_BYTES_MAX = STREAM_CHUNK_BYTES * 2 + DIGEST_CACHE_BYTES_MAX
ORIGINAL_PAIR_PEAK_BYTES = SNAPSHOT_MAX_BYTES * 2
BOUNDED_V2_PEAK_BYTES = SNAPSHOT_MAX_BYTES + STREAM_CHUNK_BYTES
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
        "experiment": "forensic-snapshot-streaming-apameta1-v2",
        "profile": profile,
        "workload": "forensic-HDDMETA-save-existing-slot-and-new-slot-readback",
        "bounded_v2_reference": BOUNDED_V2[profile],
        "expected_source_change": {
            "snapshot_format_changed": False,
            "exact_byte_compare_preserved": True,
            "per_header_sha256_preserved": True,
            "trailer_sha256_preserved": True,
            "max_patch_count": MAX_PATCHES,
            "max_snapshot_bytes": SNAPSHOT_MAX_BYTES,
            "stream_chunk_bytes": STREAM_CHUNK_BYTES,
            "digest_cache_bytes_max": DIGEST_CACHE_BYTES_MAX,
            "stream_workspace_bytes_max": STREAM_WORKSPACE_BYTES_MAX,
            "bounded_v2_peak_bytes_at_max": BOUNDED_V2_PEAK_BYTES,
            "stream_peak_bytes_at_max": STREAM_WORKSPACE_BYTES_MAX,
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
        raise SystemExit("PROFILE OFF streaming experiment changed hdl_stream.irx")
    if experiment_irx_on["sha256"] != baseline_irx_on["sha256"]:
        raise SystemExit("PROFILE ON streaming experiment changed hdl_stream.irx")
    if experiment_off["sha256"] == BOUNDED_V2["OFF"]["elf_sha256"]:
        raise SystemExit("PROFILE OFF streaming experiment did not change the EE ELF from bounded v2")
    if experiment_on["sha256"] == BOUNDED_V2["ON"]["elf_sha256"]:
        raise SystemExit("PROFILE ON streaming experiment did not change the EE ELF from bounded v2")

    identity = {
        "experiment": "forensic-snapshot-streaming-apameta1-v2",
        "project_git_sha": args.project_git_sha,
        "frozen_phase0_commit": "7875b14d837d6332f5edc37f1c12a55527d7dd87",
        "workspace_v1_frozen_ci": 724,
        "fingerprint_malloc_frozen_ci": 739,
        "storage_scratch_natural_rejected_ci": 743,
        "bounded_readback_v1_frozen_ci": 749,
        "bounded_readback_v2_frozen_ci": 752,
        "bounded_readback_v2_identity_reproved_ci": 757,
        "bounded_v2_reference": BOUNDED_V2,
        "reference_vector": {
            "format": "APAMETA1",
            "bytes": 2216,
            "image_sha256": "601ba74fc619738dac19baa2a6cb53054b67803e00b1fccb6bf89c69ef4bab6f",
        },
        "ps2sdk_commit": "b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b",
        "toolchain": "mips64r5900el-ps2-elf GCC 15.2.0",
        "memory_model": {
            "max_patch_count": MAX_PATCHES,
            "max_snapshot_bytes": SNAPSHOT_MAX_BYTES,
            "stream_chunk_bytes": STREAM_CHUNK_BYTES,
            "digest_cache_bytes_max": DIGEST_CACHE_BYTES_MAX,
            "stream_workspace_bytes_max": STREAM_WORKSPACE_BYTES_MAX,
            "original_full_pair_peak_bytes": ORIGINAL_PAIR_PEAK_BYTES,
            "bounded_v2_peak_bytes": BOUNDED_V2_PEAK_BYTES,
            "peak_reduction_vs_bounded_v2": BOUNDED_V2_PEAK_BYTES - STREAM_WORKSPACE_BYTES_MAX,
            "peak_reduction_vs_original_full_pair": ORIGINAL_PAIR_PEAK_BYTES - STREAM_WORKSPACE_BYTES_MAX,
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
        "PROFILE_OFF": {"baseline_elf": baseline_off, "experiment_elf": experiment_off, "hdl_stream_irx": baseline_irx_off},
        "PROFILE_ON": {"baseline_elf": baseline_on, "experiment_elf": experiment_on, "hdl_stream_irx": baseline_irx_on},
    }
    args.identity_output.write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.profile_off_template.write_text(json.dumps(sample_template("OFF", baseline_off, experiment_off, baseline_irx_off), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.profile_on_template.write_text(json.dumps(sample_template("ON", baseline_on, experiment_on, baseline_irx_on), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
