#!/bin/bash
# Integration tests for MobiusPL
set -e

TRAINER="${1:-./mobius_pl}"
SCORER="${2:-./mobius_score}"
TESTDIR="$(cd "$(dirname "$0")" && pwd)"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "=== MobiusPL Integration Tests ==="
echo ""

# Setup: Create larger training dataset
echo "[SETUP] Generating test dataset..."
python3 <<'PYTHON'
import csv
import random

random.seed(42)

# Generate synthetic adult-like data
with open('test_data.csv', 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['age', 'workclass', 'income', 'education', 'label'])
    
    for i in range(200):  # 200 samples
        age = random.randint(18, 75)
        workclass = random.choice(['Private', 'Government', 'Self-emp'])
        income = random.randint(15000, 150000)
        education = random.choice(['HS-grad', 'Bachelors', 'Masters'])
        label = 1 if (income > 50000 and age > 30) else 0
        
        writer.writerow([age, workclass, income, education, label])

print("Generated test_data.csv with 200 samples")
PYTHON

# Test 1: End-to-end workflow
echo "[TEST 1] End-to-end: train → score → audit verify"
if ! $TRAINER test_data.csv 100 0.01 >/dev/null 2>&1; then
    echo "FAIL: Training failed"
    exit 1
fi

# Score 10 records
for i in {1..10}; do
    age=$((20 + i * 5))
    income=$((20000 + i * 5000))
    echo "$age,Private,$income,Bachelors" | \
        $SCORER model_v1.bin "req_$i" "subj_$i" \
        --schema feature_schema_v1.art --raw-csv >/dev/null 2>&1
done

# Verify audit
if ! $SCORER --self-check decision_audit.jsonl >/dev/null 2>&1; then
    echo "FAIL: Audit chain corrupted after scoring"
    exit 1
fi
echo "PASS: Complete workflow successful"

# Test 2: JSON input mode
echo "[TEST 2] JSON input scoring"
JSON_RECORD='{"age":"45","workclass":"Private","income":"65000","education":"Bachelors"}'
if ! output=$(echo "$JSON_RECORD" | $SCORER model_v1.bin req_json subj_json \
    --schema feature_schema_v1.art --raw-json 2>&1); then
    echo "FAIL: JSON scoring failed"
    exit 1
fi
if [[ ! "$output" =~ "proba=" ]]; then
    echo "FAIL: JSON output invalid"
    exit 1
fi
echo "PASS: JSON mode works"

# Test 3: Engineered vector mode (backward compatibility)
echo "[TEST 3] Engineered vector (legacy) mode"
# Generate 2 engineered features (matching trained model)
FEATURES=$(python3 -c "import random; print(','.join(str(random.random()) for _ in range(8)))")
export MOBIUS_AUDIT_SIGN_KEY="test_sign_key"
if ! output=$(echo "$FEATURES" | $SCORER model_v1.bin req_legacy subj_legacy 2>&1); then
    echo "FAIL: Engineered vector mode failed"
    exit 1
fi
if [[ ! "$output" =~ "proba=" ]]; then
    echo "FAIL: Legacy mode output invalid"
    exit 1
fi
echo "PASS: Backward compatibility maintained"

# Test 4: Model corruption detection
echo "[TEST 4] Model corruption detection"
cp model_v1.bin model_v1.bin.backup
# Corrupt model (truncate it)
truncate -s 100 model_v1.bin
if $SCORER model_v1.bin req_corrupt subj_corrupt >/dev/null 2>&1; then
    echo "FAIL: Should reject corrupted model"
    mv model_v1.bin.backup model_v1.bin
    exit 1
fi
mv model_v1.bin.backup model_v1.bin
echo "PASS: Model corruption detected"

# Test 5: Concurrent scoring (basic lock test)
echo "[TEST 5] Concurrent scoring with audit lock"
for i in {1..5}; do
    (echo "35,Private,50000,Bachelors" | \
     $SCORER model_v1.bin "req_concurrent_$i" "subj_concurrent_$i" \
     --schema feature_schema_v1.art --raw-csv >/dev/null 2>&1) &
done
wait

if ! $SCORER --self-check decision_audit.jsonl >/dev/null 2>&1; then
    echo "FAIL: Audit chain corrupted after concurrent scoring"
    exit 1
fi
echo "PASS: Concurrent scoring handled safely"

echo ""
echo "=== All Integration Tests Passed ==="