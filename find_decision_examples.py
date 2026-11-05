#!/usr/bin/env python3
import argparse
import random
import subprocess
import sys
from pathlib import Path


def run_score(bin_path, model_path, audit_path, req_id, subject_id, vec):
    payload = ",".join(f"{x:.6f}" for x in vec) + "\n"
    p = subprocess.run(
        [bin_path, model_path, req_id, subject_id, audit_path],
        input=payload,
        text=True,
        capture_output=True,
    )
    out = (p.stdout + p.stderr).strip()
    return p.returncode, out


def main():
    ap = argparse.ArgumentParser(description="Find AUTO_NEG and AUTO_POS example vectors.")
    ap.add_argument("--bin", default="./mobius_score")
    ap.add_argument("--model", default="model_v1.bin")
    ap.add_argument("--audit", default="decision_audit.jsonl")
    ap.add_argument("--features", type=int, default=104)
    ap.add_argument("--tries", type=int, default=5000)
    ap.add_argument("--low", type=float, default=-2.0)
    ap.add_argument("--high", type=float, default=2.0)
    ap.add_argument("--subject", default="subject-999")
    ap.add_argument("--save-prefix", default="found_vector")
    args = ap.parse_args()

    if not Path(args.bin).exists():
        print(f"error: scorer binary not found: {args.bin}")
        return 2
    if not Path(args.model).exists():
        print(f"error: model not found: {args.model}")
        return 2

    found_neg = None
    found_pos = None

    for i in range(1, args.tries + 1):
        vec = [random.uniform(args.low, args.high) for _ in range(args.features)]
        rc, out = run_score(args.bin, args.model, args.audit, f"req-search-{i}", args.subject, vec)
        if rc != 0:
            continue
        if "decision=AUTO_NEG" in out and found_neg is None:
            found_neg = (i, out, vec)
        if "decision=AUTO_POS" in out and found_pos is None:
            found_pos = (i, out, vec)
        if found_neg and found_pos:
            break

    print("AUTO_NEG:", "FOUND" if found_neg else "NOT FOUND")
    if found_neg:
        i, out, vec = found_neg
        print("  trial:", i)
        print("  output:", out)
        s = ",".join(f"{x:.6f}" for x in vec)
        print("  vector:", s)
        Path(f"{args.save_prefix}_neg.csv").write_text(s + "\n", encoding="utf-8")
        print(f"  saved: {args.save_prefix}_neg.csv")

    print("AUTO_POS:", "FOUND" if found_pos else "NOT FOUND")
    if found_pos:
        i, out, vec = found_pos
        print("  trial:", i)
        print("  output:", out)
        s = ",".join(f"{x:.6f}" for x in vec)
        print("  vector:", s)
        Path(f"{args.save_prefix}_pos.csv").write_text(s + "\n", encoding="utf-8")
        print(f"  saved: {args.save_prefix}_pos.csv")

    return 0


if __name__ == "__main__":
    sys.exit(main())
