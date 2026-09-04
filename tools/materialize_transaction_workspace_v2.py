#!/usr/bin/env python3
"""Materialize Phase-5 transaction workspace v2.

V1 centralizes the mutually-exclusive COPY/source-hash/target-verify buffers.
V2 additionally lets the execute_transaction source fingerprint borrow the same
workspace before any destructive HDD work. The pre-confirmation UI fingerprint
keeps its original helper-owned allocation, so no 64 KiB buffer is retained
while waiting for the user.

This remains an isolated build experiment. Default runtime sources are not
modified permanently.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from materialize_transaction_workspace import (
    MARKER as V1_MARKER,
    MaterializeError,
    function_span,
    materialize as materialize_v1,
    replace_once,
)

V2_MARKER = "HDL transaction workspace v2: source admission borrows transaction workspace"


def materialize_source(text: str) -> str:
    if V2_MARKER in text:
        raise MaterializeError("source already contains transaction workspace v2")

    start, end = function_span(text, "source_fingerprint")
    body = text[start:end]
    body = replace_once(
        body,
        "static int source_fingerprint(hdl_file_source_t *source,\n"
        "                              unsigned char digest[32])\n",
        "static int source_fingerprint_with_workspace(\n"
        "    hdl_file_source_t *source, unsigned char digest[32],\n"
        "    unsigned char *workspace)\n",
        "source fingerprint borrowed signature",
    )
    body = replace_once(
        body,
        "    unsigned char *buffer;\n",
        f"    /* {V2_MARKER}. */\n"
        "    unsigned char *buffer = workspace;\n",
        "source fingerprint borrowed buffer",
    )
    body = replace_once(
        body,
        "    buffer = memalign(64, HDL_INSTALL_IO_BYTES);\n"
        "    if (buffer == NULL)\n"
        "        return HDL_INSTALL_MEMORY_FAILED;\n",
        "    if (buffer == NULL)\n"
        "        return HDL_INSTALL_MEMORY_FAILED;\n",
        "remove source fingerprint allocation",
    )
    body = replace_once(
        body,
        "done:\n    free(buffer);\n    return result;\n",
        "done:\n    return result;\n",
        "remove source fingerprint free",
    )

    wrapper = '''
static int source_fingerprint(hdl_file_source_t *source,
                              unsigned char digest[32])
{
    unsigned char *buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    int result;

    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
    result = source_fingerprint_with_workspace(source, digest, buffer);
    free(buffer);
    return result;
}
'''
    text = text[:start] + body + wrapper + text[end:]
    return text


def materialize_transaction(text: str) -> str:
    text = materialize_v1(text)
    start, end = function_span(text, "execute_transaction")
    body = text[start:end]

    # V1 allocates only after target open. V2 moves that single allocation to
    # source admission so fingerprint, copy/hash and target verify share it.
    v1_allocation = '''    if (transaction->stage == HDL_TRANSACTION_STAGE_COPYING ||
        transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
        workspace = memalign(64, HDL_INSTALL_IO_BYTES);
        if (workspace == NULL) {
            result = HDL_INSTALL_MEMORY_FAILED;
            goto done;
        }
    }
'''
    body = replace_once(
        body,
        v1_allocation,
        "",
        "remove v1 late workspace allocation",
    )

    body = replace_once(
        body,
        "    if (transaction->stage < HDL_TRANSACTION_STAGE_METADATA_COMMITTED) {\n"
        "        result = open_source(transaction->source_path,\n",
        "    if (transaction->stage < HDL_TRANSACTION_STAGE_METADATA_COMMITTED) {\n"
        "        workspace = memalign(64, HDL_INSTALL_IO_BYTES);\n"
        "        if (workspace == NULL)\n"
        "            return HDL_INSTALL_MEMORY_FAILED;\n"
        "        result = open_source(transaction->source_path,\n",
        "allocate workspace at source admission",
    )
    body = replace_once(
        body,
        "        if (result < 0)\n"
        "            return result;\n"
        "        result = source_fingerprint(&source, fingerprint);\n",
        "        if (result < 0) {\n"
        "            free(workspace);\n"
        "            return result;\n"
        "        }\n"
        "        result = source_fingerprint_with_workspace(\n"
        "            &source, fingerprint, workspace);\n",
        "borrow workspace for transaction fingerprint",
    )
    body = replace_once(
        body,
        "        if (result < 0) {\n"
        "            fileXioClose(source.fd);\n"
        "            return result;\n"
        "        }\n"
        "    }\n",
        "        if (result < 0) {\n"
        "            fileXioClose(source.fd);\n"
        "            free(workspace);\n"
        "            return result;\n"
        "        }\n"
        "    }\n",
        "release workspace on source-validation failure",
    )

    text = text[:start] + body + text[end:]
    if text.count("memalign(64, HDL_INSTALL_IO_BYTES)") != 1:
        raise MaterializeError("v2 transaction must contain exactly one 64 KiB memalign")
    if "source_fingerprint_with_workspace(" not in text:
        raise MaterializeError("v2 transaction did not borrow source fingerprint workspace")
    return text


def selftest() -> None:
    source = r'''static int source_fingerprint(hdl_file_source_t *source,
                              unsigned char digest[32])
{
    unsigned char *buffer;
    int result = 0;
    buffer = memalign(64, HDL_INSTALL_IO_BYTES);
    if (buffer == NULL)
        return HDL_INSTALL_MEMORY_FAILED;
done:
    free(buffer);
    return result;
}

static int source_identity_matches(void)
{
    return 0;
}
'''
    transformed = materialize_source(source)
    assert V2_MARKER in transformed
    assert transformed.count("memalign(64, HDL_INSTALL_IO_BYTES)") == 1
    assert "source_fingerprint_with_workspace" in transformed
    assert "result = source_fingerprint_with_workspace(source, digest, buffer);" in transformed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("transaction", nargs="?", type=Path)
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return 0
    if args.transaction is None or args.source is None:
        parser.error("transaction and source are required unless --selftest is used")

    source_text = materialize_source(args.source.read_text(encoding="utf-8"))
    transaction_text = materialize_transaction(
        args.transaction.read_text(encoding="utf-8")
    )
    args.source.write_text(source_text, encoding="utf-8")
    args.transaction.write_text(transaction_text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
