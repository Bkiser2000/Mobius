#!/bin/bash
# Unit tests for MobiusPL
set -e

TRAINER="${1:-./mobius_pl}"
SCORER="${2:-./mobius_score}"
TESTDIR="$(cd "$(dirname "$0")" && pwd)"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== MobiusPL Unit Tests ==="
echo "Temp dir: $TMPDIR"
echo "Trainer: $TRAINER"
echo "Scorer: $SCORER"
echo ""

# Test 1: Version info
echo "[TEST 1] Version information"
if ! output=$($TRAINER --version 2>&1); then
    echo "FAIL: Trainer --version failed"
    exit 1
fi
if [[ ! "$output" =~ "MobiusPL" ]]; then
    echo "FAIL: Version output missing 'MobiusPL'"
    exit 1
fi
echo "PASS: Version info available"

# Test 2: Help text
echo "[TEST 2] Help text"
if ! $TRAINER --help >/dev/null 2>&1; then
    echo "FAIL: Trainer --help failed"
    exit 1
fi
if ! $SCORER --help >/dev/null 2>&1; then
    echo "FAIL: Scorer --help failed"
    exit 1
fi
echo "PASS: Help text works"

# Test 3: Trainer accepts CSV with header
echo "[TEST 3] Training CSV validation"
cat > $TMPDIR/test_train.csv <<'EOF'
age,income,approved
25,35000,0
45,75000,1
35,55000,1
22,25000,0
50,65000,1
EOF

if ! $TRAINER $TMPDIR/test_train.csv 10 0.01 >/dev/null 2>&1; then
    echo "FAIL: Trainer failed on valid CSV"
    exit 1
fi

if [[ ! -f model_v1.bin ]]; then
    echo "FAIL: Model artifact not generated"
    exit 1
fi

if [[ ! -f feature_schema_v1.art ]]; then
    echo "FAIL: Schema artifact not generated"
    exit 1
fi
echo "PASS: Training generates artifacts"

# Test 4: Scorer loads model
echo "[TEST 4] Model artifact integrity"
if ! output=$(echo "35,55000" | $SCORER model_v1.bin req_test subj_test 2>&1 | head -1); then
    echo "FAIL: Scorer failed to load model"
    exit 1
fi
if [[ ! "$output" =~ "proba=" ]]; then
    echo "FAIL: Scorer output format invalid"
    exit 1
fi
echo "PASS: Model loads and scores"

# Test 5: Schema artifact exists and is readable
echo "[TEST 5] Schema artifact validation"
if [[ ! -f feature_schema_v1.art ]]; then
    echo "FAIL: Schema artifact missing"
    exit 1
fi
if ! head -1 feature_schema_v1.art | grep -q "VERSION"; then
    echo "FAIL: Schema header format invalid"
    exit 1
fi
echo "PASS: Schema artifact valid"

# Test 6: Audit trail created
echo "[TEST 6] Audit trail creation"
if [[ ! -f decision_audit.jsonl ]]; then
    echo "FAIL: Audit trail not created"
    exit 1
fi
if ! grep -q "curr_hash" decision_audit.jsonl; then
    echo "FAIL: Audit trail missing hash chain"
    exit 1
fi
echo "PASS: Audit trail created"

# Test 7: Self-check audit
echo "[TEST 7] Audit chain self-check"
if ! $SCORER --self-check decision_audit.jsonl >/dev/null 2>&1; then
    echo "FAIL: Audit self-check failed"
    exit 1
fi
echo "PASS: Audit chain valid"

# Test 8: Raw CSV mode
echo "[TEST 8] Raw CSV scoring mode"
cat > $TMPDIR/score_input.csv <<'EOF'
35,55000
EOF
if ! output=$(cat $TMPDIR/score_input.csv | $SCORER model_v1.bin req_raw_csv subj_raw --schema feature_schema_v1.art --raw-csv 2>&1); then
    echo "FAIL: Raw CSV scoring failed"
    echo "Output: $output"
    exit 1
fi
if [[ ! "$output" =~ "proba=" ]]; then
    echo "FAIL: Raw CSV output format invalid"
    exit 1
fi
echo "PASS: Raw CSV scoring works"

echo ""
echo "=== All Unit Tests Passed ==="