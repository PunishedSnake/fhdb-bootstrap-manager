#!/usr/bin/env python3
"""Validate and bind the active Phase-5 incremental A/B pair.

Frozen references:
- Phase-0 baseline: CI #666
- transaction workspace v1: CI #724
- workspace v1 + source-fingerprint malloc: CI #739

The active experiment adds natural alignment for two 1024-byte storage scratch
buffers that are consumed only by ordinary fileXio reads and EE CPU code.
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

WORKSPACE_V1 = {
    "OFF": {
        "elf_sha256": "23bbf6dfc28eb87bc7d484875a8940b9309eb5e3994d9c922388c9a0249415c6",
        "elf_bytes": 632756,
        "named_text": 229764,
        "instructions": 57491,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
    },
    "ON": {
        "elf_sha256": "09185cd6a21bbb9990d0b7f8cfe70fa80b4e9ba01a00e9648cb9fa70d9b3d693",
        "elf_bytes": 638260,
        "named_text": 232560,
        "instructions": 58190,
        "execute_transaction_bytes": 6008,
        "execute_transaction_instructions": 1502,
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
        "experiment": "hdl-workspace-v1-fingerprint-malloc-storage-scratch-natural",
        "profile": profile,
        "workload": "functional-storage-snapshot-plus-successful-hdl-transaction",
        "expected_source_change": {
            "transaction_workspace_version": 1,
            "source_fingerprint_allocator": "malloc(65536)",
            "storage_scratch_sites": [
                "header_backup.c::backup_scratch",
                "repair_snapshot.c::snapshot_verify",
            ],
            "storage_scratch_bytes_each": 1024,
            "storage_scratch_old_alignment": 64,
            "storage_scratch_new_alignment": "natural",
            "storage_scratch_consumer": "ordinary fileXio read + EE CPU parse/memcmp",
            "custom_hdl_sif_dma_alignment_changed": False,
            "pad_alignment_changed": False,
            "gif_dma_alignment_changed": False,
            "iop_binary_change": False,
        },
        "baseline": baseline,
        "workspace_v1_reference": WORKSPACE_V1[profile],
        "fingerprint_malloc_reference": FINGERPRINT_MALLOC[profile],
        "experiment_binary": experiment,
        "hdl_stream_irx": irx,
        "hardware": {
            "console_scp": "UNRECORDED",
            "hardware_revision": "UNRECORDED",
            "romver": "UNRECORDED",
            "storage_adapter": "UNRECORDED",
            "hdd_model": "UNRECORDED",
            "usb_device": "UNRECORDED",
            "active_irx": "UNRECORDED",
        },
        "runs": [
            {
                "index": i + 1,
                "variant": variant,
                "snapshot_save_result": None,
                "snapshot_readback_match": None,
                "header_backup_result": None,
                "source_fingerprint_elapsed_us": None,
                "transaction_elapsed_us": None,
                "correctness_hash": None,
            }
            for i, variant in enumerate(ORDER)
        ],
        "report": {
            "transaction_elapsed_us": {"p50": None, "p95": None, "p99": None, "max": None},
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
        raise SystemExit("PROFILE OFF alignment experiment changed hdl_stream.irx")
    if experiment_irx_on["sha256"] != baseline_irx_on["sha256"]:
        raise SystemExit("PROFILE ON alignment experiment changed hdl_stream.irx")
    if experiment_off["sha256"] == FINGERPRINT_MALLOC["OFF"]["elf_sha256"]:
        raise SystemExit("PROFILE OFF storage-scratch experiment did not change the EE ELF")
    if experiment_on["sha256"] == FINGERPRINT_MALLOC["ON"]["elf_sha256"]:
        raise SystemExit("PROFILE ON storage-scratch experiment did not change the EE ELF")

    identity = {
        "experiment": "hdl-workspace-v1-fingerprint-malloc-storage-scratch-natural",
        "project_git_sha": args.project_git_sha,
        "frozen_phase0_commit": "7875b14d837d6332f5edc37f1c12a55527d7dd87",
        "workspace_v1_frozen_ci": 724,
        "fingerprint_malloc_frozen_ci": 739,
        "workspace_v2_rejected_ci": 733,
        "workspace_v1_reference": WORKSPACE_V1,
        "fingerprint_malloc_reference": FINGERPRINT_MALLOC,
        "ps2sdk_commit": "b12f8af37bd42ec13b1bafb7ab6e7bdcfb4b683b",
        "toolchain": "mips64r5900el-ps2-elf GCC 15.2.0",
        "change": {
            "sites": ["header_backup.c::backup_scratch", "repair_snapshot.c::snapshot_verify"],
            "old_alignment": 64,
            "new_alignment": "natural",
            "consumer": "ordinary pinned fileXio read + EE CPU parse/memcmp",
            "direct_dma_consumer": False,
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
