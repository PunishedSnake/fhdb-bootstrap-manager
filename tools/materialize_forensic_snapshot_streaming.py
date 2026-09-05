#!/usr/bin/env python3
"""Materialize the isolated streaming APAMETA1 snapshot experiment.

This supersedes the full canonical-image + bounded-readback representation only
inside the experiment build. It preserves APAMETA1 bytes, per-header SHA-256,
trailer SHA-256, slot/non-overwrite policy and exact read-back comparison.

Workspace layout at maximum patch count:
  expected/write chunk     64 KiB
  actual/read-back chunk   64 KiB
  cached entry digests     patch_count * 32 B (<= 64 KiB)

One allocation therefore replaces the full 2.17 MiB canonical image plus verify
scratch. Default runtime source is restored after the experiment build.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

MARKER = "streaming APAMETA1 snapshot experiment"


class MaterializeError(RuntimeError):
    pass


def function_span(text: str, name: str) -> tuple[int, int]:
    match = re.search(rf"(?:^|\n)(?:static\s+)?(?:[\w\s\*]+?)\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", text)
    if match is None:
        raise MaterializeError(f"function {name} not found")
    start = match.start()
    if start < len(text) and text[start] == "\n":
        start += 1
    brace = text.find("{", match.start())
    depth = 0
    i = brace
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                if end < len(text) and text[end] == "\n":
                    end += 1
                return start, end
        i += 1
    raise MaterializeError(f"function {name} has unmatched braces")


def replace_function(text: str, name: str, replacement: str) -> str:
    start, end = function_span(text, name)
    return text[:start] + replacement.rstrip() + "\n" + text[end:]


STREAM_HELPERS = r'''/* streaming APAMETA1 snapshot experiment.
 * Keep the on-disk representation and exact read-back contract while bounding
 * the producer/consumer workspace instead of owning a complete serialized image. */
#define SNAPSHOT_STREAM_CHUNK_BYTES (64u * 1024u)
#define SNAPSHOT_DIGEST_BYTES 32u

static int snapshot_prepare_stream(const apa_forensic_result_t *result,
                                   const apa_forensic_repair_plan_t *plan,
                                   unsigned char *digests,
                                   unsigned int *image_size_out,
                                   unsigned int *one_or_two_out)
{
    unsigned int one_or_two = 0;
    unsigned int i;

    if (plan->patch_count == 0 || plan->patch_count > APA_FORENSIC_MAX_PATCHES)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate forensic patch count");
    if (plan->patch_count >
        (0xffffffffu - SNAPSHOT_HEADER_BYTES - SNAPSHOT_TRAILER_BYTES) /
            SNAPSHOT_ENTRY_BYTES)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate HDDMETA image size");

    for (i = 0; i < plan->patch_count; i++) {
        const apa_forensic_patch_t *patch = &plan->patches[i];
        const apa_forensic_node_t *node;
        unsigned int distance;

        if (patch->node_index >= result->node_count)
            return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                                 "resolve patch node for HDDMETA");
        node = &result->nodes[patch->node_index];
        if (node->lba != patch->lba)
            return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                                 "match patch LBA to scanned node");
        sha256_buffer(node->header, APA_HEADER_SIZE,
                      digests + i * SNAPSHOT_DIGEST_BYTES);
        distance = apa_forensic_patch_bit_distance(patch);
        if (distance == 1u || distance == 2u)
            one_or_two++;
    }

    *image_size_out = SNAPSHOT_HEADER_BYTES +
                      plan->patch_count * SNAPSHOT_ENTRY_BYTES +
                      SNAPSHOT_TRAILER_BYTES;
    *one_or_two_out = one_or_two;
    return 0;
}

static void snapshot_build_header(const apa_forensic_result_t *result,
                                  const apa_forensic_repair_plan_t *plan,
                                  unsigned int one_or_two,
                                  unsigned char header[SNAPSHOT_HEADER_BYTES])
{
    memset(header, 0, SNAPSHOT_HEADER_BYTES);
    memcpy(header, snapshot_magic, sizeof(snapshot_magic));
    write_le32_snapshot(header + 8, FORENSIC_SNAPSHOT_VERSION);
    write_le32_snapshot(header + 12, result->total_sectors);
    write_le32_snapshot(header + 16, plan->map_index);
    write_le32_snapshot(header + 20, plan->confidence);
    write_le32_snapshot(header + 24, plan->patch_count);
    write_le32_snapshot(header + 28, plan->corroborated_count);
    write_le32_snapshot(header + 32, plan->speculative_count);
    write_le32_snapshot(header + 36, one_or_two);
}

static void snapshot_build_entry(const apa_forensic_result_t *result,
                                 const apa_forensic_repair_plan_t *plan,
                                 const unsigned char *digests,
                                 unsigned int index,
                                 unsigned char entry[SNAPSHOT_ENTRY_BYTES])
{
    const apa_forensic_patch_t *patch = &plan->patches[index];
    const apa_forensic_node_t *node = &result->nodes[patch->node_index];

    write_le32_snapshot(entry, patch->lba);
    memcpy(entry + 4, digests + index * SNAPSHOT_DIGEST_BYTES,
           SNAPSHOT_DIGEST_BYTES);
    memcpy(entry + 36, node->header, APA_HEADER_SIZE);
}

static void snapshot_compute_trailer(const apa_forensic_result_t *result,
                                     const apa_forensic_repair_plan_t *plan,
                                     const unsigned char *digests,
                                     unsigned int one_or_two,
                                     unsigned char trailer[SNAPSHOT_TRAILER_BYTES])
{
    sha256_context_t context;
    unsigned char header[SNAPSHOT_HEADER_BYTES];
    unsigned char lba[4];
    unsigned int i;

    snapshot_build_header(result, plan, one_or_two, header);
    sha256_init(&context);
    sha256_update(&context, header, sizeof(header));
    for (i = 0; i < plan->patch_count; i++) {
        const apa_forensic_patch_t *patch = &plan->patches[i];
        const apa_forensic_node_t *node = &result->nodes[patch->node_index];

        write_le32_snapshot(lba, patch->lba);
        sha256_update(&context, lba, sizeof(lba));
        sha256_update(&context, digests + i * SNAPSHOT_DIGEST_BYTES,
                      SNAPSHOT_DIGEST_BYTES);
        sha256_update(&context, node->header, APA_HEADER_SIZE);
    }
    sha256_final(&context, trailer);
}

static void snapshot_fill_range(const apa_forensic_result_t *result,
                                const apa_forensic_repair_plan_t *plan,
                                const unsigned char *digests,
                                unsigned int one_or_two,
                                const unsigned char trailer[SNAPSHOT_TRAILER_BYTES],
                                unsigned int offset,
                                unsigned char *destination,
                                unsigned int bytes)
{
    unsigned char header[SNAPSHOT_HEADER_BYTES];
    unsigned char entry[SNAPSHOT_ENTRY_BYTES];
    unsigned int trailer_offset = SNAPSHOT_HEADER_BYTES +
                                  plan->patch_count * SNAPSHOT_ENTRY_BYTES;
    unsigned int done = 0;

    snapshot_build_header(result, plan, one_or_two, header);
    while (done < bytes) {
        unsigned int position = offset + done;
        unsigned int take;

        if (position < SNAPSHOT_HEADER_BYTES) {
            unsigned int inner = position;
            take = SNAPSHOT_HEADER_BYTES - inner;
            if (take > bytes - done)
                take = bytes - done;
            memcpy(destination + done, header + inner, take);
        } else if (position < trailer_offset) {
            unsigned int relative = position - SNAPSHOT_HEADER_BYTES;
            unsigned int index = relative / SNAPSHOT_ENTRY_BYTES;
            unsigned int inner = relative - index * SNAPSHOT_ENTRY_BYTES;

            snapshot_build_entry(result, plan, digests, index, entry);
            take = SNAPSHOT_ENTRY_BYTES - inner;
            if (take > bytes - done)
                take = bytes - done;
            memcpy(destination + done, entry + inner, take);
        } else {
            unsigned int inner = position - trailer_offset;
            take = SNAPSHOT_TRAILER_BYTES - inner;
            if (take > bytes - done)
                take = bytes - done;
            memcpy(destination + done, trailer + inner, take);
        }
        done += take;
    }
}

static int snapshot_write_exact(int fd, const unsigned char *data,
                                unsigned int size)
{
    unsigned int done = 0;

    while (done < size) {
        int result = fileXioWrite(fd, data + done, (int)(size - done));
        if (result <= 0)
            return result < 0 ? result : -1;
        done += (unsigned int)result;
    }
    return 0;
}

static int snapshot_read_exact(int fd, unsigned char *data, unsigned int size)
{
    unsigned int done = 0;

    while (done < size) {
        int result = fileXioRead(fd, data + done, (int)(size - done));
        if (result <= 0)
            return result < 0 ? result : -1;
        done += (unsigned int)result;
    }
    return 0;
}

static int snapshot_write_streamed(const char *path,
                                   const apa_forensic_result_t *result,
                                   const apa_forensic_repair_plan_t *plan,
                                   const unsigned char *digests,
                                   unsigned int one_or_two,
                                   const unsigned char trailer[SNAPSHOT_TRAILER_BYTES],
                                   unsigned char *chunk,
                                   unsigned int image_size)
{
    unsigned int offset = 0;
    int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);

    if (fd < 0)
        return fd;
    while (offset < image_size) {
        unsigned int bytes = image_size - offset;
        int result;

        if (bytes > SNAPSHOT_STREAM_CHUNK_BYTES)
            bytes = SNAPSHOT_STREAM_CHUNK_BYTES;
        snapshot_fill_range(result, plan, digests, one_or_two, trailer,
                            offset, chunk, bytes);
        result = snapshot_write_exact(fd, chunk, bytes);
        if (result < 0) {
            fileXioClose(fd);
            return result;
        }
        offset += bytes;
    }
    return fileXioClose(fd) < 0 ? -1 : 0;
}

static int snapshot_matches_streamed(const char *path,
                                     const apa_forensic_result_t *result,
                                     const apa_forensic_repair_plan_t *plan,
                                     const unsigned char *digests,
                                     unsigned int one_or_two,
                                     const unsigned char trailer[SNAPSHOT_TRAILER_BYTES],
                                     unsigned char *expected,
                                     unsigned char *actual,
                                     unsigned int image_size)
{
    unsigned int offset = 0;
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0);

    if (fd < 0)
        return fd;
    while (offset < image_size) {
        unsigned int bytes = image_size - offset;
        int result_code;

        if (bytes > SNAPSHOT_STREAM_CHUNK_BYTES)
            bytes = SNAPSHOT_STREAM_CHUNK_BYTES;
        snapshot_fill_range(result, plan, digests, one_or_two, trailer,
                            offset, expected, bytes);
        result_code = snapshot_read_exact(fd, actual, bytes);
        if (result_code < 0) {
            fileXioClose(fd);
            return result_code;
        }
        if (memcmp(expected, actual, bytes) != 0) {
            fileXioClose(fd);
            return 0;
        }
        offset += bytes;
    }
    {
        int extra = fileXioRead(fd, actual, 1);
        fileXioClose(fd);
        if (extra < 0)
            return extra;
        return extra == 0 ? 1 : 0;
    }
}
'''

STREAM_SAVE = r'''int forensic_snapshot_save(unsigned int storage,
                           const apa_forensic_result_t *result,
                           const apa_forensic_repair_plan_t *plan,
                           char path_out[FORENSIC_SNAPSHOT_PATH_SIZE])
{
    unsigned char trailer[SNAPSHOT_TRAILER_BYTES];
    unsigned char *workspace = NULL;
    unsigned char *expected;
    unsigned char *actual;
    unsigned char *digests;
    unsigned int image_size = 0;
    unsigned int one_or_two = 0;
    unsigned int digest_bytes;
    unsigned int workspace_bytes;
    unsigned int slot;
    int result_code;

    if (storage >= STORAGE_TARGET_COUNT || result == NULL || plan == NULL ||
        path_out == NULL)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate HDDMETA snapshot arguments");
    if (plan->patch_count == 0 || plan->patch_count > APA_FORENSIC_MAX_PATCHES)
        return fail_snapshot(FORENSIC_SNAPSHOT_INVALID_ARGUMENT,
                             "validate forensic patch count");

    digest_bytes = plan->patch_count * SNAPSHOT_DIGEST_BYTES;
    workspace_bytes = SNAPSHOT_STREAM_CHUNK_BYTES * 2u + digest_bytes;
    workspace = malloc(workspace_bytes);
    if (workspace == NULL)
        return fail_snapshot(FORENSIC_SNAPSHOT_ALLOC_FAILED,
                             "allocate streaming HDDMETA workspace");
    expected = workspace;
    actual = expected + SNAPSHOT_STREAM_CHUNK_BYTES;
    digests = actual + SNAPSHOT_STREAM_CHUNK_BYTES;

    disk_status_begin_at("Forensic repair safety snapshot",
                         "Streaming HDDMETA from every header the plan may touch",
                         "Forensic evidence / bounded APAMETA1 workspace");
    disk_status_io(DISK_STATUS_SCAN, 0, 0, 1, 3);
    result_code = snapshot_prepare_stream(result, plan, digests, &image_size,
                                          &one_or_two);
    if (result_code < 0) {
        free(workspace);
        disk_status_end();
        return result_code;
    }
    snapshot_compute_trailer(result, plan, digests, one_or_two, trailer);

    for (slot = 0; slot < FORENSIC_SNAPSHOT_SLOT_COUNT; slot++) {
        char path[FORENSIC_SNAPSHOT_PATH_SIZE];
        iox_stat_t stat;
        int stat_result;

        storage_path(path, sizeof(path), storage, snapshot_names[slot]);
        disk_status_phase_at("Selecting non-overwriting HDDMETA slot", path);
        disk_status_io(DISK_STATUS_SCAN, 0, 0, 1, 3);
        memset(&stat, 0, sizeof(stat));
        stat_result = fileXioGetStat(path, &stat);
        if (stat_result >= 0) {
            if (stat.size == image_size &&
                snapshot_matches_streamed(path, result, plan, digests,
                                          one_or_two, trailer, expected, actual,
                                          image_size) == 1) {
                strncpy(path_out, path, FORENSIC_SNAPSHOT_PATH_SIZE - 1u);
                path_out[FORENSIC_SNAPSHOT_PATH_SIZE - 1u] = '\0';
                free(workspace);
                disk_status_end();
                return 0;
            }
            continue;
        }

        disk_status_phase_at("Writing complete pre-repair HDDMETA snapshot", path);
        disk_status_io(DISK_STATUS_SCAN, 0, 0, 2, 3);
        result_code = snapshot_write_streamed(path, result, plan, digests,
                                              one_or_two, trailer, expected,
                                              image_size);
        if (result_code < 0) {
            free(workspace);
            disk_status_end();
            return fail_snapshot(FORENSIC_SNAPSHOT_WRITE_FAILED,
                                 "write HDDMETA snapshot");
        }
        disk_status_phase_at("Reading HDDMETA back and comparing every byte", path);
        disk_status_io(DISK_STATUS_VERIFY, 0, 0, 3, 3);
        if (snapshot_matches_streamed(path, result, plan, digests, one_or_two,
                                      trailer, expected, actual, image_size) != 1) {
            free(workspace);
            disk_status_end();
            return fail_snapshot(FORENSIC_SNAPSHOT_VERIFY_FAILED,
                                 "read back/compare HDDMETA snapshot");
        }

        strncpy(path_out, path, FORENSIC_SNAPSHOT_PATH_SIZE - 1u);
        path_out[FORENSIC_SNAPSHOT_PATH_SIZE - 1u] = '\0';
        free(workspace);
        disk_status_end();
        return 0;
    }

    free(workspace);
    disk_status_end();
    return fail_snapshot(FORENSIC_SNAPSHOT_NO_SLOT,
                         "select non-overwriting HDDMETA slot");
}
'''


def materialize(text: str) -> str:
    if MARKER in text:
        raise MaterializeError("streaming snapshot already materialized")
    start, end = function_span(text, "build_snapshot_image")
    text = text[:start] + f"/* {MARKER}. */\n" + STREAM_HELPERS + "\n" + text[end:]
    text = replace_function(text, "forensic_snapshot_save", STREAM_SAVE)

    if "build_snapshot_image(" in text:
        raise MaterializeError("full-image builder survived streaming transform")
    if "malloc(image_size)" in text:
        raise MaterializeError("full-size snapshot allocation survived")
    if text.count("workspace = malloc(workspace_bytes);") != 1:
        raise MaterializeError("streaming workspace allocation missing")
    if text.count("snapshot_matches_streamed(") != 3:
        raise MaterializeError("unexpected streamed compare helper/call count")
    return text


def selftest() -> None:
    fixture = r'''#define SNAPSHOT_HEADER_BYTES 64u
#define SNAPSHOT_ENTRY_BYTES (4u + 32u + APA_HEADER_SIZE)
#define SNAPSHOT_TRAILER_BYTES 32u
static int build_snapshot_image(const void *result, const void *plan,
                                unsigned char **image_out,
                                unsigned int *size_out)
{
    unsigned char *image = malloc(1024);
    free(image);
    return 0;
}
int forensic_snapshot_save(unsigned int storage, const void *result,
                           const void *plan, char path_out[64])
{
    unsigned char *image = malloc(1024);
    free(image);
    return 0;
}
'''
    out = materialize(fixture)
    assert MARKER in out
    assert "build_snapshot_image(" not in out
    assert "workspace = malloc(workspace_bytes);" in out
    assert "snapshot_compute_trailer" in out
    assert "snapshot_write_streamed" in out
    assert "snapshot_matches_streamed" in out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("forensic snapshot streaming materializer selftest: PASS")
        return 0
    if args.source is None:
        parser.error("source is required unless --selftest is used")
    args.source.write_text(materialize(args.source.read_text(encoding="utf-8")),
                           encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
