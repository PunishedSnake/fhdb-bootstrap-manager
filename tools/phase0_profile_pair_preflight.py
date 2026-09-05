#!/usr/bin/env python3
"""Verify the frozen Phase-0 PROFILE ON/OFF ELF pair before hardware testing.

This is deliberately a host-side guard. It does not instrument or modify the
benchmark binaries. Frozen identity is the exact ON/OFF ELF hash pair from CI
#666; later documentation/host-tool commits may rebuild byte-identical ELFs from
a newer repository head, so the run record keeps that artifact's project SHA
separate from the binary identity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

FROZEN_SOURCE_GIT_SHA = "7875b14d837d6332f5edc37f1c12a55527d7dd87"
PAIR = {
    "ON": {
        "bytes": 638388,
        "sha256": "964d5c30613b16e5a160b51d4473000ce6da5740596a785d100d2c68a09686d7",
        "irx_sha256": "8d3dbeabadbb860888b2c3d2072e8344953bea443faefccefce006b234cdb3db",
    },
    "OFF": {
        "bytes": 632884,
        "sha256": "4d1458ebf158c21759d1acdd3a44ecca094a5f9948c9e4461ef4a4beb8f23916",
        "irx_sha256": "f0b29957560ce2ef35a53e77fa8250f477d7aa6490037f00cdfe2edc04a39751",
    },
}

# Four samples per mode, distributed so neither build owns one contiguous block.
DEFAULT_ORDER = ("OFF", "ON", "ON", "OFF", "ON", "OFF", "OFF", "ON")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_blob(path: Path, expected_bytes: int, expected_sha256: str) -> tuple[int, str]:
    size = path.stat().st_size
    digest = _sha256(path)
    if size != expected_bytes:
        raise ValueError(f"{path}: size {size}, expected {expected_bytes}")
    if digest != expected_sha256:
        raise ValueError(f"{path}: sha256 {digest}, expected {expected_sha256}")
    return size, digest


def sample_template(project_git_sha: str = FROZEN_SOURCE_GIT_SHA) -> dict[str, object]:
    if not project_git_sha:
        raise ValueError("project_git_sha must be non-empty")

    samples: list[dict[str, object]] = []
    for index, mode in enumerate(DEFAULT_ORDER, start=1):
        samples.append({
            "run": index,
            "mode": mode,
            "project_git_sha": project_git_sha,
            "benchmark_elf_sha256": PAIR[mode]["sha256"],
            "hdl_stream_irx_sha256": PAIR[mode]["irx_sha256"],
            "workload_id": "FILL_ME",
            "correctness_hash": "FILL_ME",
            "source_bytes": 0,
            "total_us": 0,
            "copy_us": 0,
            "verify_us": 0,
        })
    return {
        "frozen_binary_source_git_sha": FROZEN_SOURCE_GIT_SHA,
        "note": (
            "Replace every FILL_ME/zero measurement before comparison. "
            "Keep source_bytes and correctness_hash identical across comparable runs. "
            "Do not edit the mode-specific ELF/IRX hashes."
        ),
        "samples": samples,
    }


def selftest() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        payload = b"phase0-preflight-fixture\n"
        fixture = root / "fixture.bin"
        fixture.write_bytes(payload)
        digest = hashlib.sha256(payload).hexdigest()
        size, observed = verify_blob(fixture, len(payload), digest)
        assert size == len(payload)
        assert observed == digest

        try:
            verify_blob(fixture, len(payload) + 1, digest)
        except ValueError as error:
            assert "size" in str(error)
        else:
            raise AssertionError("size mismatch must fail")

        try:
            verify_blob(fixture, len(payload), "0" * 64)
        except ValueError as error:
            assert "sha256" in str(error)
        else:
            raise AssertionError("hash mismatch must fail")

    artifact_sha = "artifact-head-fixture"
    template = sample_template(artifact_sha)
    samples = template["samples"]
    assert isinstance(samples, list)
    assert len(samples) == 8
    assert sum(sample["mode"] == "ON" for sample in samples) == 4
    assert sum(sample["mode"] == "OFF" for sample in samples) == 4
    assert all(sample["project_git_sha"] == artifact_sha for sample in samples)
    assert all(sample["benchmark_elf_sha256"] == PAIR[str(sample["mode"])]["sha256"]
               for sample in samples)
    assert all(sample["hdl_stream_irx_sha256"] == PAIR[str(sample["mode"])]["irx_sha256"]
               for sample in samples)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--profile-on",
        type=Path,
        default=Path("PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_ON.ELF"),
    )
    parser.add_argument(
        "--profile-off",
        type=Path,
        default=Path("PS2_HDD_BOOTSTRAP_MANAGER_PROFILE_OFF.ELF"),
    )
    parser.add_argument(
        "--project-git-sha",
        default=FROZEN_SOURCE_GIT_SHA,
        help="project SHA recorded by the CI artifact being tested",
    )
    parser.add_argument("--output-template", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("phase0_profile_pair_preflight selftest: PASS")
        return 0

    if not args.project_git_sha:
        parser.error("--project-git-sha must be non-empty")

    try:
        on_size, on_hash = verify_blob(
            args.profile_on, int(PAIR["ON"]["bytes"]), str(PAIR["ON"]["sha256"])
        )
        off_size, off_hash = verify_blob(
            args.profile_off, int(PAIR["OFF"]["bytes"]), str(PAIR["OFF"]["sha256"])
        )
    except (OSError, ValueError) as error:
        print(f"phase0_profile_pair_preflight: {error}", file=sys.stderr)
        return 2

    print(f"PROFILE ON  PASS  {on_size} B  {on_hash}")
    print(f"PROFILE OFF PASS  {off_size} B  {off_hash}")
    print(f"artifact git SHA   {args.project_git_sha}")
    print(f"frozen source SHA  {FROZEN_SOURCE_GIT_SHA}")

    if args.output_template:
        rendered = json.dumps(sample_template(args.project_git_sha), indent=2) + "\n"
        args.output_template.write_text(rendered, encoding="utf-8")
        print(f"sample template    {args.output_template}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
