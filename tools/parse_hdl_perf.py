#!/usr/bin/env python3
"""Parse corpus-v2 HDL performance records from HDDMAN.LOG.

The PS2 runtime deliberately emits compact counters and formats them only at
phase boundaries. This host-side parser turns those records into stable JSON so
hardware A/B results can be compared without teaching the IOP about JSON, which
would be a fairly creative misuse of 2 MiB of RAM.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

PREFIX = r"(?:\[\d+\]\s+)?"

RATE_RE = re.compile(
    PREFIX
    + r"HDL fast copy measured bytes=(\d+) usec=(\d+) rate=(\d+) KiB/s raw-usb11=(\d+)\.(\d+)%"
)
EE_LAT_RE = re.compile(
    PREFIX
    + r"HDL perf ([\w-]+) samples=(\d+) p50<=(\d+)us p95<=(\d+)us p99<=(\d+)us max=(\d+)us"
)
IOP_LAT_RE = re.compile(
    PREFIX
    + r"HDL IOP perf ([\w-]+) samples=(\d+) p50<=(\d+)us p95<=(\d+)us p99<=(\d+)us max=(\d+)us"
)
SNAPSHOT_RE = re.compile(
    PREFIX
    + r"HDL fast I/O snapshot phase=([\w-]+) flags=0x([0-9a-fA-F]+) fragments=(\d+) "
      r"direct=(\d+) fallback=(\d+) prefetch-hit=(\d+) miss=(\d+) pump=(\d+) "
      r"sectors=(\d+) src-dma=(\d+) target-dma=(\d+)"
)
TRAFFIC_RE = re.compile(
    PREFIX
    + r"HDL IOP traffic phase=([\w-]+) direct-src-sectors=(\d+) fallback-src-sectors=(\d+) "
      r"hdd-write-sectors=(\d+) hdd-read-sectors=(\d+) sif-dma-sectors=(\d+)"
)
EE_COPY_TRAFFIC_RE = re.compile(
    PREFIX
    + r"HDL perf copy traffic useful=(\d+) sif-dma=(\d+) ee-cache-maint=(\d+) fallback-source=(\d+)"
)
EE_VERIFY_TRAFFIC_RE = re.compile(
    PREFIX
    + r"HDL perf verify traffic target=(\d+) sif-dma-total=(\d+) ee-cache-maint-total=(\d+) "
      r"fallback-target=(\d+) consumer-samples=(\d+) final-chunk-excluded=(\d+)"
)
RESUME_PREFIX_RESTORE_RE = re.compile(
    PREFIX
    + r"HDL restored source SHA checkpoint bytes=(\d+); skipped prefix rehash"
)
RESUME_FULL_RESTORE_RE = re.compile(
    PREFIX
    + r"HDL restored complete source SHA checkpoint bytes=(\d+); skipped full USB hash pass"
)
RESUME_PREFIX_FALLBACK_RE = re.compile(
    PREFIX
    + r"HDL source SHA checkpoint unavailable result=(-?\d+); using safe prefix rehash"
)
RESUME_FULL_FALLBACK_RE = re.compile(
    PREFIX
    + r"HDL complete source SHA checkpoint unavailable result=(-?\d+); using safe full source hash"
)
CHECKPOINT_WRITE_FAILURE_RE = re.compile(
    PREFIX
    + r"HDL source SHA checkpoint (refresh|save(?: on cancel)?) failed(?: progress=(\d+))? result=(-?\d+)"
)
TRANSACTION_RE = re.compile(
    PREFIX
    + r"HDL transaction target=([^ ]+) stage=(\d+) progress=(\d+)/(\d+) result=(-?\d+)"
)


def _latency(match: re.Match[str]) -> dict[str, int]:
    return {
        "samples": int(match.group(2)),
        "p50_upper_us": int(match.group(3)),
        "p95_upper_us": int(match.group(4)),
        "p99_upper_us": int(match.group(5)),
        "max_us": int(match.group(6)),
    }


def parse_log(text: str) -> dict[str, object]:
    result: dict[str, object] = {
        "copy_rate": None,
        "ee_latency": {},
        "iop_latency": {},
        "snapshots": {},
        "iop_traffic": {},
        "ee_traffic": {},
        "resume_hash": {
            "prefix_restores": [],
            "full_restores": [],
            "prefix_fallback_results": [],
            "full_fallback_results": [],
            "checkpoint_write_failures": [],
            "transactions": [],
        },
    }

    for line in text.splitlines():
        match = RATE_RE.search(line)
        if match:
            result["copy_rate"] = {
                "bytes": int(match.group(1)),
                "usec": int(match.group(2)),
                "kib_per_second": int(match.group(3)),
                "raw_usb11_percent_tenths": int(match.group(4)) * 10
                + int(match.group(5)),
            }
            continue

        match = IOP_LAT_RE.search(line)
        if match:
            result["iop_latency"][match.group(1)] = _latency(match)  # type: ignore[index]
            continue

        match = EE_LAT_RE.search(line)
        if match:
            result["ee_latency"][match.group(1)] = _latency(match)  # type: ignore[index]
            continue

        match = SNAPSHOT_RE.search(line)
        if match:
            result["snapshots"][match.group(1)] = {  # type: ignore[index]
                "flags": int(match.group(2), 16),
                "fragments": int(match.group(3)),
                "direct_reads": int(match.group(4)),
                "fallback_reads": int(match.group(5)),
                "prefetch_hits": int(match.group(6)),
                "prefetch_misses": int(match.group(7)),
                "pumped_chunks": int(match.group(8)),
                "pumped_sectors": int(match.group(9)),
                "source_dma_chunks": int(match.group(10)),
                "target_dma_chunks": int(match.group(11)),
            }
            continue

        match = TRAFFIC_RE.search(line)
        if match:
            result["iop_traffic"][match.group(1)] = {  # type: ignore[index]
                "direct_source_sectors": int(match.group(2)),
                "fallback_source_sectors": int(match.group(3)),
                "hdd_write_sectors": int(match.group(4)),
                "hdd_read_sectors": int(match.group(5)),
                "sif_dma_sectors": int(match.group(6)),
            }
            continue

        match = EE_COPY_TRAFFIC_RE.search(line)
        if match:
            result["ee_traffic"]["copy"] = {  # type: ignore[index]
                "useful_bytes": int(match.group(1)),
                "sif_dma_bytes": int(match.group(2)),
                "ee_cache_maintenance_bytes": int(match.group(3)),
                "fallback_source_bytes": int(match.group(4)),
            }
            continue

        match = EE_VERIFY_TRAFFIC_RE.search(line)
        if match:
            result["ee_traffic"]["verify"] = {  # type: ignore[index]
                "target_bytes": int(match.group(1)),
                "sif_dma_total_bytes": int(match.group(2)),
                "ee_cache_maintenance_total_bytes": int(match.group(3)),
                "fallback_target_bytes": int(match.group(4)),
                "consumer_samples": int(match.group(5)),
                "final_chunk_excluded": int(match.group(6)),
            }
            continue

        match = RESUME_PREFIX_RESTORE_RE.search(line)
        if match:
            result["resume_hash"]["prefix_restores"].append(  # type: ignore[index]
                {"bytes": int(match.group(1))}
            )
            continue

        match = RESUME_FULL_RESTORE_RE.search(line)
        if match:
            result["resume_hash"]["full_restores"].append(  # type: ignore[index]
                {"bytes": int(match.group(1))}
            )
            continue

        match = RESUME_PREFIX_FALLBACK_RE.search(line)
        if match:
            result["resume_hash"]["prefix_fallback_results"].append(  # type: ignore[index]
                int(match.group(1))
            )
            continue

        match = RESUME_FULL_FALLBACK_RE.search(line)
        if match:
            result["resume_hash"]["full_fallback_results"].append(  # type: ignore[index]
                int(match.group(1))
            )
            continue

        match = CHECKPOINT_WRITE_FAILURE_RE.search(line)
        if match:
            result["resume_hash"]["checkpoint_write_failures"].append(  # type: ignore[index]
                {
                    "operation": match.group(1),
                    "progress": int(match.group(2)) if match.group(2) else None,
                    "result": int(match.group(3)),
                }
            )
            continue

        match = TRANSACTION_RE.search(line)
        if match:
            result["resume_hash"]["transactions"].append(  # type: ignore[index]
                {
                    "target": match.group(1),
                    "stage": int(match.group(2)),
                    "progress": int(match.group(3)),
                    "total": int(match.group(4)),
                    "result": int(match.group(5)),
                }
            )

    return result


def selftest() -> None:
    sample = """
[0042] HDL fast copy measured bytes=1048576 usec=1000000 rate=1024 KiB/s raw-usb11=69.9%
[0043] HDL IOP perf usb-direct-read samples=16 p50<=65536us p95<=131072us p99<=131072us max=70000us
[0044] HDL perf pump-ioctl samples=16 p50<=65536us p95<=131072us p99<=131072us max=75000us
[0045] HDL fast I/O snapshot phase=copy-final flags=0x00000007 fragments=1 direct=16 fallback=0 prefetch-hit=15 miss=0 pump=16 sectors=2048 src-dma=16 target-dma=0
[0046] HDL IOP traffic phase=copy-final direct-src-sectors=2048 fallback-src-sectors=0 hdd-write-sectors=2048 hdd-read-sectors=0 sif-dma-sectors=2048
[0047] HDL perf copy traffic useful=1048576 sif-dma=1048576 ee-cache-maint=2097152 fallback-source=0
[0048] HDL restored source SHA checkpoint bytes=33554432; skipped prefix rehash
[0049] HDL source SHA checkpoint unavailable result=-5; using safe prefix rehash
[0050] HDL restored complete source SHA checkpoint bytes=1073741824; skipped full USB hash pass
[0051] HDL complete source SHA checkpoint unavailable result=-7; using safe full source hash
[0052] HDL source SHA checkpoint save failed progress=32768 result=-12; journal remains authoritative
[0053] HDL transaction target=PP.TEST stage=6 progress=524288/524288 result=0
"""
    parsed = parse_log(sample)
    assert parsed["copy_rate"]["kib_per_second"] == 1024  # type: ignore[index]
    assert parsed["iop_latency"]["usb-direct-read"]["samples"] == 16  # type: ignore[index]
    assert parsed["ee_latency"]["pump-ioctl"]["max_us"] == 75000  # type: ignore[index]
    assert parsed["snapshots"]["copy-final"]["flags"] == 7  # type: ignore[index]
    assert parsed["iop_traffic"]["copy-final"]["hdd_write_sectors"] == 2048  # type: ignore[index]
    assert parsed["ee_traffic"]["copy"]["ee_cache_maintenance_bytes"] == 2097152  # type: ignore[index]
    assert parsed["resume_hash"]["prefix_restores"][0]["bytes"] == 33554432  # type: ignore[index]
    assert parsed["resume_hash"]["prefix_fallback_results"] == [-5]  # type: ignore[index]
    assert parsed["resume_hash"]["full_restores"][0]["bytes"] == 1073741824  # type: ignore[index]
    assert parsed["resume_hash"]["full_fallback_results"] == [-7]  # type: ignore[index]
    assert parsed["resume_hash"]["checkpoint_write_failures"][0]["progress"] == 32768  # type: ignore[index]
    assert parsed["resume_hash"]["transactions"][0]["result"] == 0  # type: ignore[index]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("parse_hdl_perf selftest: PASS")
        return 0
    if args.log is None:
        parser.error("log is required unless --selftest is used")

    parsed = parse_log(args.log.read_text(encoding="utf-8", errors="replace"))
    rendered = json.dumps(parsed, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
