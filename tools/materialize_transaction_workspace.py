#!/usr/bin/env python3
"""Materialize the isolated HDL transaction-workspace experiment.

The default source tree keeps the historical per-phase memalign/free policy.
This tool rewrites a temporary copy of transaction.inc so one transaction owns
one 64 KiB / 64-byte-aligned EE workspace across source/copy/HDD-verify phases.

The rewrite is deliberately exact and fails closed if the audited source shape
moves. It is used only by the experiment build; it does not promote the policy
to the default runtime.
"""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

MARKER = "HDL transaction workspace experiment: one owner, borrowed by phase helpers"


class MaterializeError(RuntimeError):
    pass


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise MaterializeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def function_span(text: str, name: str) -> tuple[int, int]:
    start_token = f"static int {name}("
    start = text.find(start_token)
    if start < 0:
        raise MaterializeError(f"missing function {name}")
    next_start = text.find("\nstatic int ", start + len(start_token))
    if next_start < 0:
        raise MaterializeError(f"cannot bound function {name}")
    return start, next_start


def edit_function(text: str, name: str, edits: list[tuple[str, str, str]]) -> str:
    start, end = function_span(text, name)
    body = text[start:end]
    for old, new, label in edits:
        body = replace_once(body, old, new, f"{name}: {label}")
    return text[:start] + body + text[end:]


def materialize(text: str) -> str:
    if MARKER in text:
        raise MaterializeError("source already contains transaction workspace experiment")

    text = edit_function(
        text,
        "hash_source_payload",
        [
            (
                "                               unsigned char digest[32])\n",
                "                               unsigned char digest[32],\n"
                "                               unsigned char *workspace)\n",
                "workspace parameter",
            ),
            (
                "    unsigned char *buffer;\n",
                "    unsigned char *buffer = workspace;\n",
                "borrow workspace",
            ),
            (
                "    buffer = memalign(64, HDL_INSTALL_IO_BYTES);\n"
                "    if (buffer == NULL)\n"
                "        return HDL_INSTALL_MEMORY_FAILED;\n",
                "    if (buffer == NULL)\n"
                "        return HDL_INSTALL_MEMORY_FAILED;\n",
                "remove phase allocation",
            ),
            (
                "done:\n    free(buffer);\n    return result;\n",
                "done:\n    return result;\n",
                "remove phase free",
            ),
        ],
    )

    text = edit_function(
        text,
        "copy_payload",
        [
            (
                "                        unsigned char source_digest[32])\n",
                "                        unsigned char source_digest[32],\n"
                "                        unsigned char *workspace)\n",
                "workspace parameter",
            ),
            (
                "    unsigned char *buffer;\n",
                "    unsigned char *buffer = workspace;\n",
                "borrow workspace",
            ),
            (
                "    buffer = memalign(64, HDL_INSTALL_IO_BYTES);\n"
                "    if (buffer == NULL)\n"
                "        return HDL_INSTALL_MEMORY_FAILED;\n",
                "    if (buffer == NULL)\n"
                "        return HDL_INSTALL_MEMORY_FAILED;\n",
                "remove phase allocation",
            ),
            (
                "done:\n    free(buffer);\n    return result;\n",
                "done:\n    return result;\n",
                "remove phase free",
            ),
        ],
    )

    text = edit_function(
        text,
        "verify_target_digest",
        [
            (
                "                                const unsigned char expected_digest[32])\n",
                "                                const unsigned char expected_digest[32],\n"
                "                                unsigned char *workspace)\n",
                "workspace parameter",
            ),
            (
                "    unsigned char *target_buffer;\n",
                "    unsigned char *target_buffer = workspace;\n",
                "borrow workspace",
            ),
            (
                "    target_buffer = memalign(64, HDL_INSTALL_IO_BYTES);\n"
                "    if (target_buffer == NULL)\n"
                "        return HDL_INSTALL_MEMORY_FAILED;\n",
                "    if (target_buffer == NULL)\n"
                "        return HDL_INSTALL_MEMORY_FAILED;\n",
                "remove phase allocation",
            ),
            (
                "done:\n    free(target_buffer);\n    return result;\n",
                "done:\n    return result;\n",
                "remove phase free",
            ),
        ],
    )

    start, end = function_span(text, "execute_transaction")
    body = text[start:end]
    body = replace_once(
        body,
        "    unsigned char source_payload_digest[32];\n",
        "    unsigned char source_payload_digest[32];\n"
        f"    /* {MARKER}. */\n"
        "    unsigned char *workspace = NULL;\n",
        "execute_transaction: workspace owner",
    )
    body = replace_once(
        body,
        "    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING) {\n"
        "        result = copy_payload(transaction, &plan, &layout,\n"
        "                              source.fd, target_fd, source_payload_digest);\n",
        "    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING ||\n"
        "        transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {\n"
        "        workspace = memalign(64, HDL_INSTALL_IO_BYTES);\n"
        "        if (workspace == NULL) {\n"
        "            result = HDL_INSTALL_MEMORY_FAILED;\n"
        "            goto done;\n"
        "        }\n"
        "    }\n"
        "    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING) {\n"
        "        result = copy_payload(transaction, &plan, &layout,\n"
        "                              source.fd, target_fd, source_payload_digest,\n"
        "                              workspace);\n",
        "execute_transaction: allocate once before phase work",
    )
    body = replace_once(
        body,
        "        result = verify_target_digest(transaction, &plan, &layout,\n"
        "                                      target_fd, source_payload_digest);\n",
        "        result = verify_target_digest(transaction, &plan, &layout,\n"
        "                                      target_fd, source_payload_digest,\n"
        "                                      workspace);\n",
        "execute_transaction: copy-path verify workspace",
    )
    body = replace_once(
        body,
        "            result = hash_source_payload(transaction, source.fd,\n"
        "                                         source_payload_digest);\n",
        "            result = hash_source_payload(transaction, source.fd,\n"
        "                                         source_payload_digest, workspace);\n",
        "execute_transaction: resumed source hash workspace",
    )
    body = replace_once(
        body,
        "            result = verify_target_digest(transaction, &plan, &layout,\n"
        "                                          target_fd, source_payload_digest);\n",
        "            result = verify_target_digest(transaction, &plan, &layout,\n"
        "                                          target_fd, source_payload_digest,\n"
        "                                          workspace);\n",
        "execute_transaction: resumed verify workspace",
    )
    body = replace_once(
        body,
        "done:\n    if (target_fd >= 0)\n",
        "done:\n    free(workspace);\n    if (target_fd >= 0)\n",
        "execute_transaction: release owner workspace",
    )
    text = text[:start] + body + text[end:]

    if text.count("memalign(64, HDL_INSTALL_IO_BYTES)") != 1:
        raise MaterializeError("materialized transaction must contain exactly one 64 KiB memalign")
    if text.count("free(workspace);") != 1:
        raise MaterializeError("materialized transaction must contain exactly one workspace free")
    for token in ("free(buffer);", "free(target_buffer);"):
        if token in text:
            raise MaterializeError(f"phase-local free survived: {token}")
    return text


def selftest() -> None:
    fixture = r'''static int hash_source_payload(const hdl_transaction_t *transaction,
                               int source_fd,
                               unsigned char digest[32])
{
    unsigned char *buffer;
    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
done:
    free(buffer);
    return result;
}

static int copy_payload(hdl_transaction_t *transaction,
                        const hdl_partition_plan_t *plan,
                        const hdl_stream_layout_t *layout,
                        int source_fd, int target_fd,
                        unsigned char source_digest[32])
{
    unsigned char *buffer;
    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
done:
    free(buffer);
    return result;
}

static int verify_target_digest(const hdl_transaction_t *transaction,
                                const hdl_partition_plan_t *plan,
                                const hdl_stream_layout_t *layout,
                                int target_fd,
                                const unsigned char expected_digest[32])
{
    unsigned char *target_buffer;
    target_buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (target_buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
done:
    free(target_buffer);
    return result;
}

static int execute_transaction(hdl_transaction_t *transaction)
{
    unsigned char source_payload_digest[32];
    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING) {
        result = copy_payload(transaction, &plan, &layout,
                              source.fd, target_fd, source_payload_digest);
        result = verify_target_digest(transaction, &plan, &layout,
                                      target_fd, source_payload_digest);
    }
    if (transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
            result = hash_source_payload(transaction, source.fd,
                                         source_payload_digest);
            result = verify_target_digest(transaction, &plan, &layout,
                                          target_fd, source_payload_digest);
    }
done:
    if (target_fd >= 0)
        fileXioClose(target_fd);
    return result;
}

static int sentinel(void) { return 0; }
'''
    out = materialize(fixture)
    assert MARKER in out
    assert out.count("memalign(64, HDL_INSTALL_IO_BYTES)") == 1
    assert out.count("free(workspace);") == 1
    assert "source_payload_digest, workspace" in out
    assert "source_payload_digest,\n                                      workspace" in out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs="?", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return 0
    if args.input is None or args.output is None:
        parser.error("input and output are required unless --selftest is used")

    source = args.input.read_text(encoding="utf-8")
    result = materialize(source)
    args.output.write_text(result, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
