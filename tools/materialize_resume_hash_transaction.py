#!/usr/bin/env python3
"""Materialize the isolated resume-hash transaction experiment.

The frozen default transaction fragment must remain byte-for-byte untouched for
Phase-0 A/B.  This build-time transform applies the next recovery-only cut to
its already-isolated resume-hash copy: a PAYLOAD_VERIFIED transaction with a
matching complete SHA checkpoint no longer re-opens the USB ISO merely to prove
identity before using that same checkpoint.

If either expected source block drifts, generation fails instead of silently
building a partially transformed experiment.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

PRE_SOURCE_OLD = '''    /* Once METADATA_COMMITTED is durable, payload and source identity have
     * already passed full verification. Completion recovery only needs the
     * target layout plus metadata read-back, so do not require the USB ISO to
     * remain connected merely to move stage 5 -> COMPLETE. */
    if (transaction->stage < HDL_TRANSACTION_STAGE_METADATA_COMMITTED) {
        result = open_source(transaction->source_path,
                             transaction->source_bytes, &source);
        if (result < 0)
            return result;
        result = source_fingerprint(&source, fingerprint);
        if (result == 0 && memcmp(fingerprint, transaction->source_fingerprint,
                                  sizeof(fingerprint)) != 0)
            result = HDL_INSTALL_SOURCE_CHANGED;
        if (result == 0)
            result = source_identity_matches(&source, transaction);
        if (result < 0) {
            fileXioClose(source.fd);
            return result;
        }
    }
'''

PRE_SOURCE_NEW = '''    /* Once METADATA_COMMITTED is durable, payload and source identity have
     * already passed full verification. Completion recovery only needs the
     * target layout plus metadata read-back, so do not require the USB ISO to
     * remain connected merely to move stage 5 -> COMPLETE.
     *
     * PAYLOAD_VERIFIED has the same opportunity when its authenticated
     * checkpoint represents the complete source. The checkpoint is bound to
     * this journal's source size, completed byte count, fingerprint and target
     * ID; target SHA read-back below still proves the HDD payload matches the
     * digest accumulated while the original source bytes were copied. */
#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
    if (transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
        sha256_context_t restored_source_hash;
        int checkpoint_result =
            hash_checkpoint_load(transaction, &restored_source_hash);

        if (checkpoint_result == 0) {
            sha256_final(&restored_source_hash, source_payload_digest);
            source_digest_valid = 1;
            session_log_line(
                "HDL restored complete source SHA checkpoint bytes=%llu; skipped full USB hash pass and source reopen",
                (unsigned long long)transaction->source_bytes);
        } else {
            session_log_line(
                "HDL complete source SHA checkpoint unavailable result=%d; using safe full source hash",
                checkpoint_result);
        }
    }
#endif
    if (transaction->stage < HDL_TRANSACTION_STAGE_METADATA_COMMITTED &&
        !(transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED &&
          source_digest_valid)) {
        result = open_source(transaction->source_path,
                             transaction->source_bytes, &source);
        if (result < 0)
            return result;
        result = source_fingerprint(&source, fingerprint);
        if (result == 0 && memcmp(fingerprint, transaction->source_fingerprint,
                                  sizeof(fingerprint)) != 0)
            result = HDL_INSTALL_SOURCE_CHANGED;
        if (result == 0)
            result = source_identity_matches(&source, transaction);
        if (result < 0) {
            fileXioClose(source.fd);
            return result;
        }
    }
'''

STAGE4_OLD = '''    if (transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
        if (!verified_this_run) {
#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
            sha256_context_t restored_source_hash;
            int checkpoint_result =
                hash_checkpoint_load(transaction, &restored_source_hash);

            if (checkpoint_result == 0) {
                sha256_final(&restored_source_hash, source_payload_digest);
                source_digest_valid = 1;
                session_log_line(
                    "HDL restored complete source SHA checkpoint bytes=%llu; skipped full USB hash pass",
                    (unsigned long long)transaction->source_bytes);
            } else {
                session_log_line(
                    "HDL complete source SHA checkpoint unavailable result=%d; using safe full source hash",
                    checkpoint_result);
#endif
                disk_status_phase_at("Hashing source for resumed verification",
                                     "One source pass required without a matching hash checkpoint");
                result = hash_source_payload(transaction, source.fd,
                                             source_payload_digest);
                if (result < 0)
                    goto done;
                source_digest_valid = 1;
#if defined(HDL_RESUME_HASH_CHECKPOINT_ENABLED) && HDL_RESUME_HASH_CHECKPOINT_ENABLED
            }
#endif
            disk_status_phase_at("Re-verifying resumed payload on HDD",
                                 "HDD-only SHA-256 read-back against source digest");
            result = verify_target_digest(transaction, &plan, &layout,
                                          target_fd, source_payload_digest);
            if (result < 0)
                goto done;
        }
'''

STAGE4_NEW = '''    if (transaction->stage == HDL_TRANSACTION_STAGE_PAYLOAD_VERIFIED) {
        if (!verified_this_run) {
            if (!source_digest_valid) {
                disk_status_phase_at("Hashing source for resumed verification",
                                     "One source pass required without a matching hash checkpoint");
                result = hash_source_payload(transaction, source.fd,
                                             source_payload_digest);
                if (result < 0)
                    goto done;
                source_digest_valid = 1;
            }
            disk_status_phase_at("Re-verifying resumed payload on HDD",
                                 "HDD-only SHA-256 read-back against source digest");
            result = verify_target_digest(transaction, &plan, &layout,
                                          target_fd, source_payload_digest);
            if (result < 0)
                goto done;
        }
'''


def materialize(text: str) -> str:
    if text.count(PRE_SOURCE_OLD) != 1:
        raise ValueError("expected exactly one pre-source validation block")
    text = text.replace(PRE_SOURCE_OLD, PRE_SOURCE_NEW, 1)
    if text.count(STAGE4_OLD) != 1:
        raise ValueError("expected exactly one PAYLOAD_VERIFIED restore block")
    text = text.replace(STAGE4_OLD, STAGE4_NEW, 1)
    return text


def selftest() -> None:
    fixture = "before\n" + PRE_SOURCE_OLD + "middle\n" + STAGE4_OLD + "after\n"
    transformed = materialize(fixture)
    assert PRE_SOURCE_OLD not in transformed
    assert STAGE4_OLD not in transformed
    assert transformed.count("skipped full USB hash pass and source reopen") == 1
    assert transformed.count("if (!source_digest_valid)") == 1
    try:
        materialize("drifted input")
    except ValueError:
        pass
    else:
        raise AssertionError("source drift must fail materialization")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        print("materialize_resume_hash_transaction selftest: PASS")
        return 0
    if args.source is None or args.output is None:
        parser.error("source and output are required unless --selftest is used")

    try:
        text = args.source.read_text(encoding="utf-8")
        generated = materialize(text)
        args.output.write_text(generated, encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"materialize_resume_hash_transaction: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
