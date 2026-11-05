#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description="Score one engineered feature vector from a CSV line file.")
    ap.add_argument("--bin", default="./mobius_score")
    ap.add_argument("--model", default="model_v1.bin")
    ap.add_argument("--audit", default="decision_audit.jsonl")
    ap.add_argument("--request", required=True)
    ap.add_argument("--subject", required=True)
    ap.add_argument("--vector-file", required=True, help="File containing one CSV line of features")
    args = ap.parse_args()

    for path in [args.bin, args.model, args.vector_file]:
        if not Path(path).exists():
            print(f"error: missing file: {path}")
            return 2

    payload = Path(args.vector_file).read_text(encoding="utf-8")
    if not payload.endswith("\n"):
        payload += "\n"

    p = subprocess.run(
        [args.bin, args.model, args.request, args.subject, args.audit],
        input=payload,
        text=True,
        capture_output=True,
    )

    if p.stdout.strip():
        print(p.stdout.strip())
    if p.stderr.strip():
        print(p.stderr.strip())

    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
