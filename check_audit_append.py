#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path


def count_lines(path):
    p = Path(path)
    if not p.exists():
        return 0
    with p.open("r", encoding="utf-8") as f:
        return sum(1 for _ in f)


def main():
    ap = argparse.ArgumentParser(description="Check one scoring request appends exactly one audit line.")
    ap.add_argument("--bin", default="./mobius_score")
    ap.add_argument("--model", default="model_v1.bin")
    ap.add_argument("--audit", default="decision_audit.jsonl")
    ap.add_argument("--features", type=int, default=104)
    ap.add_argument("--request", default="req-append-check")
    ap.add_argument("--subject", default="subject-999")
    args = ap.parse_args()

    if not Path(args.bin).exists():
        print(f"error: scorer binary not found: {args.bin}")
        return 2
    if not Path(args.model).exists():
        print(f"error: model not found: {args.model}")
        return 2

    before = count_lines(args.audit)

    payload = ",".join(["0"] * args.features) + "\n"
    p = subprocess.run(
        [args.bin, args.model, args.request, args.subject, args.audit],
        input=payload,
        text=True,
        capture_output=True,
    )

    after = count_lines(args.audit)
    delta = after - before

    if p.stdout.strip():
        print(p.stdout.strip())
    if p.stderr.strip():
        print(p.stderr.strip())

    print(f"before={before} after={after} delta={delta}")
    if p.returncode != 0:
        print("result=FAILED (scorer returned non-zero)")
        return 1
    if delta != 1:
        print("result=FAILED (expected delta=1)")
        return 1

    print("result=OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
