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
JOURNAL_RE = re.compile(
    PREFIX
    + r"HDL journal save stage=(\d+) progress=(\d+)/(\d+) record=(\d+) us=(\d+) "
      r"count=(\d+) total_us=(\d+) max_us=(\d+) result=(-?\d+)"
)
COPY_CHECKPOINT_RE = re.compile(
    PREFIX + r"HDL COPY restored SHA checkpoint bytes=(\d+) sectors=(\d+)"
)
APA_SCAN_RE = re.compile(
    PREFIX + r"HDL APA catalogue scan result=(-?\d+) games=(\d+) seconds=(\d+) usec=(\d+)"
)


def _latency(match: re.Match[str]) -> dict[str, int]:
    return {
        "samples": int(match.group(2)),
        "p50_upper_us": int(match.group(3)),
        "p95_upper_us": int(match.group(4)),
        "p99_upper_us": int(match.group(5)),
        "max_us": int(match.group(6)),
    }


def _percentile_nearest_rank(values: list[int], percentile: int) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    rank = (percentile * len(ordered) + 99) // 100
    return ordered[max(0, rank - 1)]


def parse_log(text: str) -> dict[str, object]:
    result: dict[str, object] = {
        "copy_rate": None,
        "ee_latency": {},
        "iop_latency": {},
        "snapshots": {},
        "iop_traffic": {},
        "ee_traffic": {},
        "journal_saves": [],
        "journal_summary": None,
        "copy_resume_checkpoints": [],
        "apa_catalog_scans": [],
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

        match = JOURNAL_RE.search(line)
        if match:
            result["journal_saves"].append({  # type: ignore[union-attr]
                "stage": int(match.group(1)),
                "progress_sectors": int(match.group(2)),
                "total_sectors": int(match.group(3)),
                "record_bytes": int(match.group(4)),
                "usec": int(match.group(5)),
                "runtime_count": int(match.group(6)),
                "runtime_total_us": int(match.group(7)),
                "runtime_max_us": int(match.group(8)),
                "result": int(match.group(9)),
            })
            continue

        match = COPY_CHECKPOINT_RE.search(line)
        if match:
            result["copy_resume_checkpoints"].append({  # type: ignore[union-attr]
                "avoided_usb_prefix_bytes": int(match.group(1)),
                "completed_sectors": int(match.group(2)),
            })
            continue

        match = APA_SCAN_RE.search(line)
        if match:
            seconds = int(match.group(3))
            microseconds = int(match.group(4))
            result["apa_catalog_scans"].append({  # type: ignore[union-attr]
                "result": int(match.group(1)),
                "games": int(match.group(2)),
                "usec": seconds * 1_000_000 + microseconds,
            })

    journal_saves = result["journal_saves"]
    if isinstance(journal_saves, list) and journal_saves:
        successful = [
            int(sample["usec"])
            for sample in journal_saves
            if isinstance(sample, dict) and sample.get("result") == 0
        ]
        result["journal_summary"] = {
            "samples": len(journal_saves),
            "successful": len(successful),
            "failures": len(journal_saves) - len(successful),
            "p50_us": _percentile_nearest_rank(successful, 50),
            "p95_us": _percentile_nearest_rank(successful, 95),
            "p99_us": _percentile_nearest_rank(successful, 99),
            "max_us": max(successful) if successful else 0,
            "total_us": sum(successful),
        }

    return result


def selftest() -> None:
    sample = """
[0042] HDL fast copy measured bytes=1048576 usec=1000000 rate=1024 KiB/s raw-usb11=69.9%
[0043] HDL IOP perf usb-direct-read samples=16 p50<=65536us p95<=131072us p99<=131072us max=70000us
[0044] HDL perf pump-ioctl samples=16 p50<=65536us p95<=131072us p99<=131072us max=75000us
[0045] HDL fast I/O snapshot phase=copy-final flags=0x00000007 fragments=1 direct=16 fallback=0 prefetch-hit=15 miss=0 pump=16 sectors=2048 src-dma=16 target-dma=0
[0046] HDL IOP traffic phase=copy-final direct-src-sectors=2048 fallback-src-sectors=0 hdd-write-sectors=2048 hdd-read-sectors=0 sif-dma-sectors=2048
[0047] HDL perf copy traffic useful=1048576 sif-dma=1048576 ee-cache-maint=2097152 fallback-source=0
[0048] HDL journal save stage=3 progress=16384/100000 record=544 us=3100 count=1 total_us=3100 max_us=3100 result=0
[0049] HDL journal save stage=3 progress=32768/100000 record=544 us=7900 count=2 total_us=11000 max_us=7900 result=0
[0050] HDL journal save stage=3 progress=49152/100000 record=544 us=5200 count=3 total_us=16200 max_us=7900 result=0
[0051] HDL journal save stage=3 progress=65536/100000 record=544 us=9000 count=4 total_us=25200 max_us=9000 result=-5
[0052] HDL COPY restored SHA checkpoint bytes=2147483648 sectors=1048576
[0053] HDL APA catalogue scan result=0 games=87 seconds=1 usec=250000
"""
    parsed = parse_log(sample)
    assert parsed["copy_rate"]["kib_per_second"] == 1024  # type: ignore[index]
    assert parsed["iop_latency"]["usb-direct-read"]["samples"] == 16  # type: ignore[index]
    assert parsed["ee_latency"]["pump-ioctl"]["max_us"] == 75000  # type: ignore[index]
    assert parsed["snapshots"]["copy-final"]["flags"] == 7  # type: ignore[index]
    assert parsed["iop_traffic"]["copy-final"]["hdd_write_sectors"] == 2048  # type: ignore[index]
    assert parsed["ee_traffic"]["copy"]["ee_cache_maintenance_bytes"] == 2097152  # type: ignore[index]
    assert parsed["journal_saves"][0]["record_bytes"] == 544  # type: ignore[index]
    assert parsed["journal_summary"]["samples"] == 4  # type: ignore[index]
    assert parsed["journal_summary"]["successful"] == 3  # type: ignore[index]
    assert parsed["journal_summary"]["p50_us"] == 5200  # type: ignore[index]
    assert parsed["journal_summary"]["p95_us"] == 7900  # type: ignore[index]
    assert parsed["journal_summary"]["max_us"] == 7900  # type: ignore[index]
    assert parsed["copy_resume_checkpoints"][0]["avoided_usb_prefix_bytes"] == 2147483648  # type: ignore[index]
    assert parsed["apa_catalog_scans"][0]["games"] == 87  # type: ignore[index]
    assert parsed["apa_catalog_scans"][0]["usec"] == 1250000  # type: ignore[index]


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
