#!/usr/bin/env python3
"""
Development-only PS2 APA fault injector for Michishirube hardware validation.

The tool is intentionally conservative:
- probe is read-only;
- mutate is dry-run unless --apply is supplied;
- every mutation requires the exact current master-header SHA-256 printed by probe;
- physical drives additionally require --confirm-physical-write;
- every touched 1024-byte APA header is backed up before the first write;
- writes are flushed and read back exactly;
- restore refuses to overwrite a header that is neither the expected mutated
  state nor the original state recorded by the manifest.

This is not a general-purpose disk editor.
"""

from __future__ import annotations

import argparse
import ctypes
import datetime as _dt
import hashlib
import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from typing import BinaryIO, Dict, List, Optional, Tuple

SECTOR_SIZE = 512
APA_HEADER_SIZE = 1024

APA_CHECKSUM = 0x000
APA_MAGIC = 0x004
APA_NEXT = 0x008
APA_PREV = 0x00C
APA_ID = 0x010
APA_START = 0x040
APA_LENGTH = 0x044
APA_TYPE = 0x048
APA_FLAGS = 0x04A
APA_NSUB = 0x04C
APA_MBR_MAGIC = 0x100
APA_MBR_VERSION = 0x120

APA_MAGIC_BYTES = b"APA\x00"
APA_MBR_ID = b"__mbr"
APA_MBR_TYPE = 0x0001
APA_MBR_VERSION_VALUE = 2
SONY_MBR_MAGIC = b"Sony Computer Entertainment Inc."

MANIFEST_FORMAT = "PS2-HDD-FAULT-INJECTOR-1"
MAX_CHAIN_HEADERS = 4096

SCENARIOS = (
    "master-magic-1bit",
    "next-1bit",
    "next-2bit",
)


class FaultInjectorError(RuntimeError):
    pass


def _u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def _u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def _put_u32(data: bytearray, off: int, value: int) -> None:
    struct.pack_into("<I", data, off, value & 0xFFFFFFFF)


def _cstring(data: bytes) -> str:
    return data.split(b"\x00", 1)[0].decode("ascii", "replace")


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def apa_checksum(data: bytes) -> int:
    if len(data) != APA_HEADER_SIZE:
        raise FaultInjectorError("APA header must be exactly 1024 bytes")
    words = struct.unpack_from("<255I", data, 4)
    return sum(words) & 0xFFFFFFFF


def apa_checksum_ok(data: bytes) -> bool:
    return _u32(data, APA_CHECKSUM) == apa_checksum(data)


def hamming32(a: int, b: int) -> int:
    return ((a ^ b) & 0xFFFFFFFF).bit_count()


def validate_master(data: bytes) -> None:
    if len(data) != APA_HEADER_SIZE:
        raise FaultInjectorError("short master header")
    if data[APA_MAGIC:APA_MAGIC + 4] != APA_MAGIC_BYTES:
        raise FaultInjectorError("LBA 0 is not canonical APA magic")
    if data[APA_ID:APA_ID + len(APA_MBR_ID)] != APA_MBR_ID:
        raise FaultInjectorError("LBA 0 is not __mbr")
    if _u32(data, APA_START) != 0:
        raise FaultInjectorError("APA master start is not zero")
    if _u16(data, APA_TYPE) != APA_MBR_TYPE:
        raise FaultInjectorError("APA master type is not MBR")
    if data[APA_MBR_MAGIC:APA_MBR_MAGIC + len(SONY_MBR_MAGIC)] != SONY_MBR_MAGIC:
        raise FaultInjectorError("Sony MBR marker is not canonical")
    if _u32(data, APA_MBR_VERSION) != APA_MBR_VERSION_VALUE:
        raise FaultInjectorError("APA MBR version is not 2")
    if not apa_checksum_ok(data):
        raise FaultInjectorError("APA master checksum is already invalid")


def validate_chain_header(data: bytes, lba: int) -> None:
    if len(data) != APA_HEADER_SIZE:
        raise FaultInjectorError(f"short APA header at LBA 0x{lba:08x}")
    if data[APA_MAGIC:APA_MAGIC + 4] != APA_MAGIC_BYTES:
        raise FaultInjectorError(f"non-APA header in live chain at LBA 0x{lba:08x}")
    if _u32(data, APA_START) != lba:
        raise FaultInjectorError(
            f"header start mismatch at LBA 0x{lba:08x}: "
            f"claims 0x{_u32(data, APA_START):08x}"
        )
    if not apa_checksum_ok(data):
        raise FaultInjectorError(
            f"header checksum already invalid at LBA 0x{lba:08x}"
        )


def _physical_path(number: int) -> str:
    return rf"\\.\PhysicalDrive{number}"


def _physical_drive_size(number: int) -> int:
    if os.name != "nt":
        raise FaultInjectorError("--physical is supported only on Windows")

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    create_file = kernel32.CreateFileW
    create_file.argtypes = [
        ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
    ]
    create_file.restype = ctypes.c_void_p

    device_io_control = kernel32.DeviceIoControl
    device_io_control.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
        ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32),
        ctypes.c_void_p,
    ]
    device_io_control.restype = ctypes.c_int

    close_handle = kernel32.CloseHandle
    close_handle.argtypes = [ctypes.c_void_p]

    generic_read = 0x80000000
    file_share_read = 0x00000001
    file_share_write = 0x00000002
    open_existing = 3
    ioctl_disk_get_length_info = 0x0007405C
    invalid_handle_value = ctypes.c_void_p(-1).value

    path = _physical_path(number)
    handle = create_file(
        path, generic_read, file_share_read | file_share_write,
        None, open_existing, 0, None,
    )
    if handle == invalid_handle_value:
        raise FaultInjectorError(
            f"CreateFileW({path}) failed: WinError {ctypes.get_last_error()}"
        )
    try:
        length = ctypes.c_longlong()
        returned = ctypes.c_uint32()
        ok = device_io_control(
            handle, ioctl_disk_get_length_info, None, 0,
            ctypes.byref(length), ctypes.sizeof(length),
            ctypes.byref(returned), None,
        )
        if not ok:
            raise FaultInjectorError(
                "IOCTL_DISK_GET_LENGTH_INFO failed: "
                f"WinError {ctypes.get_last_error()}"
            )
        return int(length.value)
    finally:
        close_handle(handle)


class Target:
    def __init__(self, image: Optional[str], physical: Optional[int]):
        if (image is None) == (physical is None):
            raise FaultInjectorError("select exactly one of --image or --physical")
        self.image = image
        self.physical = physical
        if physical is not None:
            self.path = _physical_path(physical)
            self.display = self.path
            self.size_bytes = _physical_drive_size(physical)
        else:
            self.path = str(Path(image).resolve())
            self.display = self.path
            self.size_bytes = Path(self.path).stat().st_size
        if self.size_bytes < APA_HEADER_SIZE:
            raise FaultInjectorError("target is too small")
        if self.size_bytes % SECTOR_SIZE != 0:
            raise FaultInjectorError("target size is not sector aligned")
        self.total_sectors = self.size_bytes // SECTOR_SIZE

    def open(self, write: bool = False) -> BinaryIO:
        mode = "r+b" if write else "rb"
        try:
            return open(self.path, mode, buffering=0)
        except PermissionError as exc:
            hint = (
                " Run the terminal as Administrator."
                if self.physical is not None else ""
            )
            raise FaultInjectorError(
                f"cannot open {self.display}: {exc}.{hint}"
            ) from exc


def read_header(handle: BinaryIO, lba: int) -> bytes:
    if lba < 0:
        raise FaultInjectorError("negative LBA")
    handle.seek(lba * SECTOR_SIZE)
    data = handle.read(APA_HEADER_SIZE)
    if len(data) != APA_HEADER_SIZE:
        raise FaultInjectorError(f"short read at LBA 0x{lba:08x}")
    return data


def write_header_verified(handle: BinaryIO, lba: int, data: bytes) -> None:
    if len(data) != APA_HEADER_SIZE:
        raise FaultInjectorError("refusing non-1024-byte header write")
    handle.seek(lba * SECTOR_SIZE)
    written = handle.write(data)
    if written != APA_HEADER_SIZE:
        raise FaultInjectorError(
            f"short write at LBA 0x{lba:08x}: {written}/{APA_HEADER_SIZE}"
        )
    handle.flush()
    os.fsync(handle.fileno())
    handle.seek(lba * SECTOR_SIZE)
    readback = handle.read(APA_HEADER_SIZE)
    if readback != data:
        raise FaultInjectorError(
            f"read-back mismatch at LBA 0x{lba:08x}; stop using this disk"
        )


def scan_live_chain(target: Target) -> Tuple[bytes, List[Tuple[int, bytes]]]:
    with target.open(False) as handle:
        master = read_header(handle, 0)
        validate_master(master)

        chain: List[Tuple[int, bytes]] = [(0, master)]
        visited = {0}
        next_lba = _u32(master, APA_NEXT)

        while next_lba != 0:
            if len(chain) >= MAX_CHAIN_HEADERS:
                raise FaultInjectorError("APA chain exceeds safety limit")
            if next_lba in visited:
                raise FaultInjectorError(
                    f"cycle before master detected at LBA 0x{next_lba:08x}"
                )
            if next_lba + 2 > target.total_sectors:
                raise FaultInjectorError(
                    f"live-chain LBA 0x{next_lba:08x} is outside target"
                )
            data = read_header(handle, next_lba)
            validate_chain_header(data, next_lba)
            visited.add(next_lba)
            chain.append((next_lba, data))
            next_lba = _u32(data, APA_NEXT)

    return master, chain


def _choose_internal_next(
    chain: List[Tuple[int, bytes]], requested_lba: Optional[int]
) -> Tuple[int, bytes]:
    candidates = [
        item for item in chain[1:]
        if _u32(item[1], APA_NEXT) != 0
    ]
    if requested_lba is not None:
        for item in candidates:
            if item[0] == requested_lba:
                return item
        raise FaultInjectorError(
            f"LBA 0x{requested_lba:08x} is not a non-last live-chain header"
        )
    if not candidates:
        raise FaultInjectorError(
            "no non-master header with a live next link is available"
        )
    return candidates[len(candidates) // 2]


def build_mutation(
    scenario: str,
    chain: List[Tuple[int, bytes]],
    requested_lba: Optional[int],
) -> Tuple[int, bytes, bytes, Dict[str, object]]:
    if scenario == "master-magic-1bit":
        lba, before = chain[0]
        after = bytearray(before)
        old_byte = after[APA_MAGIC]
        if old_byte != ord("A"):
            raise FaultInjectorError(
                "master APA magic is not canonical before mutation"
            )
        after[APA_MAGIC] ^= 0x01
        details = {
            "field": "APA magic byte 0",
            "offset": APA_MAGIC,
            "before": f"0x{old_byte:02x}",
            "after": f"0x{after[APA_MAGIC]:02x}",
            "bit_distance": 1,
            "checksum_policy": "stale checksum retained",
        }
        return lba, before, bytes(after), details

    if scenario in ("next-1bit", "next-2bit"):
        lba, before = _choose_internal_next(chain, requested_lba)
        old_next = _u32(before, APA_NEXT)
        mask = 0x1 if scenario == "next-1bit" else 0x3
        new_next = old_next ^ mask
        after = bytearray(before)
        _put_u32(after, APA_NEXT, new_next)
        distance = hamming32(old_next, new_next)
        expected_distance = 1 if scenario == "next-1bit" else 2
        if distance != expected_distance:
            raise FaultInjectorError("internal bit-distance assertion failed")
        details = {
            "field": "next",
            "offset": APA_NEXT,
            "before": f"0x{old_next:08x}",
            "after": f"0x{new_next:08x}",
            "xor_mask": f"0x{mask:08x}",
            "bit_distance": distance,
            "checksum_policy": "stale checksum retained",
        }
        return lba, before, bytes(after), details

    raise FaultInjectorError(f"unknown scenario: {scenario}")


def changed_byte_offsets(before: bytes, after: bytes) -> List[int]:
    return [
        i for i, (old, new) in enumerate(zip(before, after))
        if old != new
    ]


def default_backup_dir(scenario: str) -> Path:
    stamp = _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return Path(f"ps2-hdd-fault-{scenario}-{stamp}")


def prepare_manifest(
    target: Target,
    scenario: str,
    master_before: bytes,
    lba: int,
    before: bytes,
    after: bytes,
    details: Dict[str, object],
    backup_dir: Path,
) -> Dict[str, object]:
    backup_dir.mkdir(parents=True, exist_ok=False)
    backup_file = f"LBA_{lba:08X}.bin"
    (backup_dir / backup_file).write_bytes(before)

    manifest: Dict[str, object] = {
        "format": MANIFEST_FORMAT,
        "created_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(),
        "status": "prepared",
        "target_display": target.display,
        "target_size_bytes": target.size_bytes,
        "scenario": scenario,
        "master_sha256_before": sha256_hex(master_before),
        "mutations": [{
            "lba": lba,
            "backup_file": backup_file,
            "before_sha256": sha256_hex(before),
            "after_sha256": sha256_hex(after),
            "changed_byte_offsets": changed_byte_offsets(before, after),
            "details": details,
        }],
    }
    (backup_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def update_manifest_status(
    backup_dir: Path, manifest: Dict[str, object], status: str
) -> None:
    manifest["status"] = status
    manifest["updated_utc"] = _dt.datetime.now(_dt.timezone.utc).isoformat()
    (backup_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def require_expected_master(actual: bytes, expected: str) -> None:
    actual_sha = sha256_hex(actual)
    if actual_sha.lower() != expected.lower():
        raise FaultInjectorError(
            "master SHA-256 does not match --expect-master-sha\n"
            f" expected: {expected}\n actual:   {actual_sha}\n"
            "Run probe again and verify that you selected the intended test disk."
        )


def print_probe(
    target: Target, master: bytes, chain: List[Tuple[int, bytes]]
) -> None:
    print(f"Target: {target.display}")
    print(f"Size:   {target.size_bytes} bytes ({target.total_sectors} sectors)")
    print(f"Master SHA-256: {sha256_hex(master)}")
    print(f"APA live-chain headers: {len(chain)}")
    print(
        "Master: "
        f"next=0x{_u32(master, APA_NEXT):08x} "
        f"prev=0x{_u32(master, APA_PREV):08x} "
        f"length=0x{_u32(master, APA_LENGTH):08x} "
        "checksum=OK"
    )
    candidates = [
        item for item in chain[1:]
        if _u32(item[1], APA_NEXT) != 0
    ]
    if candidates:
        lba, data = candidates[len(candidates) // 2]
        print(
            "Suggested topology-test header: "
            f"LBA 0x{lba:08x} "
            f"id='{_cstring(data[APA_ID:APA_ID + 32])}' "
            f"prev=0x{_u32(data, APA_PREV):08x} "
            f"next=0x{_u32(data, APA_NEXT):08x}"
        )
    print("\nNo data was modified.")


def command_probe(args: argparse.Namespace) -> int:
    target = Target(args.image, args.physical)
    master, chain = scan_live_chain(target)
    print_probe(target, master, chain)
    return 0


def command_mutate(args: argparse.Namespace) -> int:
    target = Target(args.image, args.physical)
    if target.physical is not None and not args.confirm_physical_write:
        raise FaultInjectorError(
            "physical-drive mutation requires --confirm-physical-write"
        )

    master, chain = scan_live_chain(target)
    require_expected_master(master, args.expect_master_sha)
    lba, before, after, details = build_mutation(
        args.scenario, chain, args.lba
    )

    if before == after:
        raise FaultInjectorError("mutation would not change any bytes")
    if apa_checksum_ok(after):
        raise FaultInjectorError(
            "planned stale-checksum mutation unexpectedly remains checksum-valid"
        )

    print(f"Scenario: {args.scenario}")
    print(f"Target:   {target.display}")
    print(f"Header:   LBA 0x{lba:08x}")
    print(f"Before:   {sha256_hex(before)}")
    print(f"After:    {sha256_hex(after)}")
    print(f"Details:  {json.dumps(details, sort_keys=True)}")
    print(f"Changed byte offsets: {changed_byte_offsets(before, after)}")

    if not args.apply:
        print("\nDRY RUN ONLY. Re-run with --apply after verifying the target.")
        return 0

    backup_dir = (
        Path(args.backup_dir)
        if args.backup_dir else default_backup_dir(args.scenario)
    )
    manifest = prepare_manifest(
        target, args.scenario, master, lba, before, after,
        details, backup_dir,
    )
    print(f"Backup manifest: {backup_dir / 'manifest.json'}")
    print("Original header was saved before the first raw write.")

    with target.open(True) as handle:
        current = read_header(handle, lba)
        if current != before:
            update_manifest_status(
                backup_dir, manifest, "source-changed-before-write"
            )
            raise FaultInjectorError(
                "target header changed after planning; no write was performed"
            )
        write_header_verified(handle, lba, after)

    update_manifest_status(backup_dir, manifest, "applied")
    print("Mutation written, flushed, and read back exactly.")
    print("Disconnect the disk cleanly and run the Michishirube test.")
    print("Keep the backup directory until the disk is restored and re-probed.")
    return 0


def load_manifest(path: Path) -> Tuple[Path, Dict[str, object]]:
    manifest_path = path / "manifest.json" if path.is_dir() else path
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != MANIFEST_FORMAT:
        raise FaultInjectorError("unsupported fault-injector manifest")
    return manifest_path.parent, manifest


def command_restore(args: argparse.Namespace) -> int:
    target = Target(args.image, args.physical)
    if target.physical is not None and not args.confirm_physical_write:
        raise FaultInjectorError(
            "physical-drive restore requires --confirm-physical-write"
        )
    backup_dir, manifest = load_manifest(Path(args.manifest))

    expected_size = int(manifest["target_size_bytes"])
    if target.size_bytes != expected_size:
        raise FaultInjectorError(
            f"target size differs from manifest: "
            f"{target.size_bytes} != {expected_size}"
        )

    mutations = list(manifest.get("mutations", []))
    if not mutations:
        raise FaultInjectorError("manifest has no mutations")

    states = []
    with target.open(False) as handle:
        for entry in mutations:
            lba = int(entry["lba"])
            original = (backup_dir / str(entry["backup_file"])).read_bytes()
            if len(original) != APA_HEADER_SIZE:
                raise FaultInjectorError(
                    f"backup for LBA 0x{lba:08x} is not 1024 bytes"
                )
            if sha256_hex(original) != str(entry["before_sha256"]):
                raise FaultInjectorError(
                    f"backup SHA mismatch for LBA 0x{lba:08x}"
                )
            current = read_header(handle, lba)
            current_sha = sha256_hex(current)
            before_sha = str(entry["before_sha256"])
            after_sha = str(entry["after_sha256"])
            if current_sha not in (before_sha, after_sha):
                raise FaultInjectorError(
                    f"LBA 0x{lba:08x} is neither original nor expected "
                    "mutated state; refusing to overwrite it"
                )
            states.append((entry, original, current))

    pending = [
        item for item in states
        if sha256_hex(item[2]) == str(item[0]["after_sha256"])
    ]
    if not pending:
        print("All manifest headers already match their original bytes. Nothing to do.")
        return 0

    print(f"Restore target: {target.display}")
    for entry, original, current in pending:
        print(
            f"  LBA 0x{int(entry['lba']):08x}: "
            f"{sha256_hex(current)} -> {sha256_hex(original)}"
        )
    if not args.apply:
        print("\nDRY RUN ONLY. Re-run with --apply to restore.")
        return 0

    # Restore non-master metadata first and LBA 0 last, mirroring the recovery
    # engine's conservative commit direction.
    pending.sort(key=lambda item: int(item[0]["lba"]) == 0)
    with target.open(True) as handle:
        for entry, original, current in pending:
            lba = int(entry["lba"])
            fresh = read_header(handle, lba)
            if fresh != current:
                raise FaultInjectorError(
                    f"LBA 0x{lba:08x} changed before restore; stopping"
                )
            write_header_verified(handle, lba, original)

    update_manifest_status(backup_dir, manifest, "restored")
    print("Original header bytes restored, flushed, and read back exactly.")
    return 0


def _make_header(
    lba: int,
    prev_lba: int,
    next_lba: int,
    length: int,
    ident: bytes,
    ptype: int,
    master: bool = False,
) -> bytes:
    data = bytearray(APA_HEADER_SIZE)
    data[APA_MAGIC:APA_MAGIC + 4] = APA_MAGIC_BYTES
    _put_u32(data, APA_NEXT, next_lba)
    _put_u32(data, APA_PREV, prev_lba)
    data[APA_ID:APA_ID + min(len(ident), 32)] = ident[:32]
    _put_u32(data, APA_START, lba)
    _put_u32(data, APA_LENGTH, length)
    struct.pack_into("<H", data, APA_TYPE, ptype)
    if master:
        data[APA_MBR_MAGIC:APA_MBR_MAGIC + len(SONY_MBR_MAGIC)] = SONY_MBR_MAGIC
        _put_u32(data, APA_MBR_VERSION, APA_MBR_VERSION_VALUE)
    _put_u32(data, APA_CHECKSUM, apa_checksum(bytes(data)))
    return bytes(data)


def command_selftest(_: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="ps2-fault-injector-") as td:
        image = Path(td) / "test.img"
        total_sectors = 0x5000
        image.write_bytes(b"\x00" * (total_sectors * SECTOR_SIZE))

        headers = [
            (0x0000, _make_header(
                0x0000, 0x3000, 0x1000, 0x1000,
                b"__mbr", 1, True,
            )),
            (0x1000, _make_header(
                0x1000, 0x0000, 0x2000, 0x1000,
                b"__system", 0x0100,
            )),
            (0x2000, _make_header(
                0x2000, 0x1000, 0x3000, 0x1000,
                b"+OPL", 0x0100,
            )),
            (0x3000, _make_header(
                0x3000, 0x2000, 0x0000, 0x1000,
                b"PP.TEST", 0x0100,
            )),
        ]
        with image.open("r+b", buffering=0) as handle:
            for lba, data in headers:
                handle.seek(lba * SECTOR_SIZE)
                handle.write(data)

        target = Target(str(image), None)
        master, chain = scan_live_chain(target)
        if len(chain) != 4:
            raise FaultInjectorError("selftest probe chain length")

        for scenario, expected_bits in (
            ("master-magic-1bit", 1),
            ("next-1bit", 1),
            ("next-2bit", 2),
        ):
            master, chain = scan_live_chain(target)
            lba, before, after, details = build_mutation(
                scenario, chain, None
            )
            if int(details["bit_distance"]) != expected_bits:
                raise FaultInjectorError("selftest bit distance")
            if apa_checksum_ok(after):
                raise FaultInjectorError(
                    "selftest expected stale checksum mismatch"
                )
            with target.open(True) as handle:
                write_header_verified(handle, lba, after)
                write_header_verified(handle, lba, before)
            restored_master, restored_chain = scan_live_chain(target)
            if sha256_hex(restored_master) != sha256_hex(master):
                raise FaultInjectorError("selftest master restore mismatch")
            if len(restored_chain) != 4:
                raise FaultInjectorError("selftest chain restore mismatch")

    print("PS2 HDD hardware fault injector self-test passed.")
    return 0


def add_target_args(parser: argparse.ArgumentParser) -> None:
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--image", help="raw HDD image file")
    group.add_argument(
        "--physical", type=int, metavar="N",
        help="Windows PhysicalDrive number",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Guarded PS2 APA fault injector for "
            "Michishirube hardware validation"
        )
    )
    sub = parser.add_subparsers(dest="command", required=True)

    probe = sub.add_parser(
        "probe", help="read-only target validation and chain summary"
    )
    add_target_args(probe)
    probe.set_defaults(func=command_probe)

    mutate = sub.add_parser(
        "mutate", help="preview or apply one controlled corruption"
    )
    add_target_args(mutate)
    mutate.add_argument("--scenario", choices=SCENARIOS, required=True)
    mutate.add_argument(
        "--lba", type=lambda value: int(value, 0),
        help="specific non-master header LBA for next-link scenarios",
    )
    mutate.add_argument(
        "--expect-master-sha", required=True,
        help="exact SHA-256 printed by a fresh probe",
    )
    mutate.add_argument(
        "--backup-dir",
        help="new directory for pre-mutation evidence and manifest",
    )
    mutate.add_argument(
        "--apply", action="store_true",
        help="actually perform the raw header write",
    )
    mutate.add_argument(
        "--confirm-physical-write", action="store_true",
        help="additional required gate for PhysicalDrive writes",
    )
    mutate.set_defaults(func=command_mutate)

    restore = sub.add_parser(
        "restore", help="restore exact original bytes from a manifest"
    )
    add_target_args(restore)
    restore.add_argument(
        "--manifest", required=True,
        help="manifest.json or its containing backup directory",
    )
    restore.add_argument(
        "--apply", action="store_true",
        help="actually perform the restore",
    )
    restore.add_argument(
        "--confirm-physical-write", action="store_true",
        help="additional required gate for PhysicalDrive writes",
    )
    restore.set_defaults(func=command_restore)

    selftest = sub.add_parser(
        "selftest", help="exercise mutations on a temporary image"
    )
    selftest.set_defaults(func=command_selftest)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    try:
        args = build_parser().parse_args(argv)
        return int(args.func(args))
    except (
        FaultInjectorError, OSError, ValueError, KeyError,
        json.JSONDecodeError,
    ) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
