#!/usr/bin/env python3
import argparse
import hashlib
import re
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description="Verify hash chain in decision audit JSONL file.")
    ap.add_argument("--audit", default="decision_audit.jsonl")
    args = ap.parse_args()

    p = Path(args.audit)
    if not p.exists():
        print(f"error: audit file not found: {args.audit}")
        return 2

    pat_curr = re.compile(r',"curr_hash":"([0-9a-f]{64})"}\s*$')
    pat_prev = re.compile(r'"prev_hash":"([0-9a-f]{64})"')

    ok = True
    count = 0
    prev_bytes = bytes(32)

    with p.open("r", encoding="utf-8") as f:
        for ln, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            m = pat_curr.search(line)
            if not m:
                print(f"line {ln}: missing curr_hash")
                ok = False
                continue

            curr_hex = m.group(1)
            prefix = line[:m.start()]

            pm = pat_prev.search(prefix)
            if not pm:
                print(f"line {ln}: missing prev_hash field")
                ok = False
                continue

            prev_in_line = pm.group(1)
            expected_prev = prev_bytes.hex()
            if prev_in_line != expected_prev:
                print(f"line {ln}: prev_hash mismatch expected={expected_prev} got={prev_in_line}")
                ok = False

            calc = hashlib.sha256(prev_bytes + prefix.encode("utf-8")).hexdigest()
            if calc != curr_hex:
                print(f"line {ln}: curr_hash mismatch expected={calc} got={curr_hex}")
                ok = False

            prev_bytes = bytes.fromhex(curr_hex)
            count += 1

    print(f"checked_lines={count}")
    print("hash_chain_status=OK" if ok else "hash_chain_status=FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
