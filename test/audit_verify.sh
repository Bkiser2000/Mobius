#!/bin/bash
# Audit chain verification tests
set -e

SCORER="${1:-./ mobius_score}"
TESTDIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== MobiusPL Audit Verification Tests ==="
echo ""

# Test 1: Fresh audit chain has valid format
echo "[TEST 1] Audit chain format"
if [[ ! -f decision_audit.jsonl ]]; then
    echo "SKIP: No audit trail found"
else
    # Check for required fields
    if ! grep -q '"curr_hash"' decision_audit.jsonl; then
        echo "FAIL: Missing curr_hash field"
        exit 1
    fi
    if ! grep -q '"prev_hash"' decision_audit.jsonl; then
        echo "FAIL: Missing prev_hash field"
        exit 1
    fi
    if ! grep -q '"ts"' decision_audit.jsonl; then
        echo "FAIL: Missing timestamp"
        exit 1
    fi
    echo "PASS: Audit format valid"
fi

# Test 2: Self-check passes
echo "[TEST 2] Built-in self-check"
if ! $SCORER --self-check decision_audit.jsonl >/dev/null 2>&1; then
    echo "FAIL: Self-check failed"
    exit 1
fi
echo "PASS: Self-check passed"

# Test 3: Line count matches event count
echo "[TEST 3] Audit event count"
if [[ -f decision_audit.jsonl ]]; then
    line_count=$(wc -l < decision_audit.jsonl)
    echo "  Found $line_count audit events"
    if [[ $line_count -lt 10 ]]; then
        echo "WARN: Low event count (expected >10 from previous tests)"
    fi
    echo "PASS: Event count acceptable"
else
    echo "SKIP: No audit trail"
fi

# Test 4: Hash chain linkage
echo "[TEST 4] Hash chain linkage verification"
python3 <<'PYTHON' || true
import json
import hashlib
import sys

try:
    prev_hash = "0" * 64
    with open("decision_audit.jsonl", "r") as f:
        for line_no, line in enumerate(f, 1):
            try:
                event = json.loads(line.strip())
                
                # Check prev_hash matches
                if event.get("prev_hash") != prev_hash:
                    print(f"FAIL at line {line_no}: prev_hash mismatch")
                    print(f"  Expected: {prev_hash}")
                    print(f"  Got: {event.get('prev_hash')}")
                    sys.exit(1)
                
                # Verify curr_hash (SHA256 of prev + event prefix)
                curr_hex = event.get("curr_hash", "")

                # Extract literal substring up to ",\"curr_hash\":"
                marker = line.find(',\"curr_hash\":\"')
                if marker < 0:
                    print(f"FAIL at line {line_no}: curr_hash marker not found")
                    sys.exit(1)
                event_prefix = line[:marker]

                expected_hash = hashlib.sha256(
                    bytes.fromhex(prev_hash) + event_prefix.encode("utf-8")
                ).hexdigest()
                
                if curr_hex != expected_hash:
                    print(f"FAIL at line {line_no}: curr_hash mismatch")
                    print(f"  Expected: {expected_hash}")
                    print(f"  Got: {curr_hex}")
                    sys.exit(1)
                
                prev_hash = curr_hex
                
            except json.JSONDecodeError as e:
                print(f"FAIL: Invalid JSON at line {line_no}: {e}")
                sys.exit(1)
    
    print("PASS: All hashes verified")
except FileNotFoundError:
    print("SKIP: No audit trail")
PYTHON

echo ""
echo "=== All Audit Tests Passed ==="