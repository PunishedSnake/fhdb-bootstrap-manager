#!/usr/bin/env python3
"""Materialize the isolated bounded HDDMETA read-back experiment.

The baseline forensic snapshot keeps two complete images live: the canonical
serialized HDDMETA image and an equally large read-back buffer used only for
byte-for-byte verification. This experiment keeps the canonical image but bounds
the read-back scratch to 64 KiB and compares every returned chunk exactly.

No on-disk format, hash, slot, overwrite, error, or repair policy changes.
"""

from __future__ import annotations

import argparse
from pathlib import Path

MARKER = "bounded forensic snapshot read-back experiment"
CHUNK_DEFINE = "#define SNAPSHOT_VERIFY_CHUNK_BYTES (64u * 1024u)"


class MaterializeError(RuntimeError):
    pass


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise MaterializeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def materialize(text: str) -> str:
    if MARKER in text:
        raise MaterializeError("forensic bounded verify already materialized")

    text = replace_once(
        text,
        "#define SNAPSHOT_TRAILER_BYTES 32u\n",
        "#define SNAPSHOT_TRAILER_BYTES 32u\n"
        f"{CHUNK_DEFINE}\n",
        "chunk constant",
    )

    anchor = "int forensic_snapshot_save(unsigned int storage,\n"
    helper = r'''/* bounded forensic snapshot read-back experiment.
 * Keep exact byte-for-byte verification but do not duplicate the complete
 * variable-size HDDMETA image merely to read it back. */
static int snapshot_file_matches_bounded(const char *path,
                                         const unsigned char *expected,
                                         unsigned int size,
                                         unsigned char *scratch,
                                         unsigned int scratch_size)
{
    unsigned int offset = 0;
    int fd;
    int result;

    if (path == NULL || expected == NULL || scratch == NULL ||
        scratch_size == 0)
        return -1;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    result = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (result < 0 || (unsigned int)result != size ||
        fileXioLseek(fd, 0, FIO_SEEK_SET) < 0) {
        fileXioClose(fd);
        return -1;
    }

    while (offset < size) {
        unsigned int bytes = size - offset;
        unsigned int complete = 0;

        if (bytes > scratch_size)
            bytes = scratch_size;
        while (complete < bytes) {
            result = fileXioRead(fd, scratch + complete,
                                 (int)(bytes - complete));
            if (result <= 0) {
                fileXioClose(fd);
                return result < 0 ? result : -1;
            }
            complete += (unsigned int)result;
        }
        if (memcmp(scratch, expected + offset, bytes) != 0) {
            fileXioClose(fd);
            return 0;
        }
        offset += bytes;
    }

    fileXioClose(fd);
    return 1;
}

'''
    text = replace_once(text, anchor, helper + anchor, "bounded compare helper")

    text = replace_once(
        text,
        "    unsigned int image_size = 0;\n"
        "    unsigned int slot;\n",
        "    unsigned int image_size = 0;\n"
        "    unsigned int verify_size = 0;\n"
        "    unsigned int slot;\n",
        "verify size state",
    )

    text = replace_once(
        text,
        "    verify = malloc(image_size);\n"
        "    if (verify == NULL) {\n",
        "    verify_size = image_size < SNAPSHOT_VERIFY_CHUNK_BYTES\n"
        "                      ? image_size : SNAPSHOT_VERIFY_CHUNK_BYTES;\n"
        "    verify = malloc(verify_size);\n"
        "    if (verify == NULL) {\n",
        "bounded verify allocation",
    )

    text = replace_once(
        text,
        "            if (stat.size == image_size &&\n"
        "                read_exact_file(path, verify, (int)image_size) == 0 &&\n"
        "                memcmp(verify, image, image_size) == 0) {\n",
        "            if (stat.size == image_size &&\n"
        "                snapshot_file_matches_bounded(path, image, image_size,\n"
        "                                              verify, verify_size) == 1) {\n",
        "existing snapshot compare",
    )

    text = replace_once(
        text,
        "        if (read_exact_file(path, verify, (int)image_size) < 0 ||\n"
        "            memcmp(verify, image, image_size) != 0) {\n",
        "        if (snapshot_file_matches_bounded(path, image, image_size,\n"
        "                                          verify, verify_size) != 1) {\n",
        "new snapshot compare",
    )

    if text.count("verify = malloc(image_size);") != 0:
        raise MaterializeError("full-size verify allocation survived")
    if text.count("snapshot_file_matches_bounded(") != 3:
        raise MaterializeError("unexpected bounded compare helper/call count")
    return text


def selftest() -> None:
    fixture = r'''#define SNAPSHOT_TRAILER_BYTES 32u

int forensic_snapshot_save(unsigned int storage,
                           const void *result,
                           const void *plan,
                           char path_out[64])
{
    unsigned char *image = NULL;
    unsigned char *verify = NULL;
    unsigned int image_size = 0;
    unsigned int slot;
    verify = malloc(image_size);
    if (verify == NULL) {
        return -1;
    }
    if (stat.size == image_size &&
                read_exact_file(path, verify, (int)image_size) == 0 &&
                memcmp(verify, image, image_size) == 0) {
        return 0;
    }
    if (read_exact_file(path, verify, (int)image_size) < 0 ||
            memcmp(verify, image, image_size) != 0) {
        return -2;
    }
    free(verify);
    free(image);
    return 0;
}
'''
    out = materialize(fixture)
    assert MARKER in out
    assert CHUNK_DEFINE in out
    assert "verify = malloc(verify_size);" in out
    assert "verify = malloc(image_size);" not in out
    assert out.count("snapshot_file_matches_bounded(") == 3


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if args.source is None:
        parser.error("source is required unless --selftest is used")
    args.source.write_text(
        materialize(args.source.read_text(encoding="utf-8")), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
