# MobiusPL: Auditable Binary Classification for Financial Risk Scoring

**Version:** 1.0.0  
**Status:** Production Ready  

## Table of Contents

1. [Overview](#overview)
2. [Key Features](#key-features)
3. [Architecture](#architecture)
4. [Use Cases](#use-cases)
5. [Quick Start](#quick-start)
6. [Installation](#installation)
7. [Training](#training)
8. [Scoring](#scoring)
9. [Input Modes](#input-modes)
10. [Feature Schema](#feature-schema)
11. [Audit Trail](#audit-trail)
12. [Configuration](#configuration)
13. [API Reference](#api-reference)
14. [Examples](#examples)
15. [Performance](#performance)
16. [Security](#security)
17. [Troubleshooting](#troubleshooting)

---

## Overview

**MobiusPL** is a production-grade binary classification system designed for compliance-heavy industries (lending, fraud detection, underwriting, identity verification). It combines logistic regression inference with cryptographic audit trails, three-class decision policies, and robust handling of missing/unknown data.

### Core Philosophy

- **Audit-first:** Every prediction is chained with SHA256 hashes, enabling regulatory proof
- **No surprises:** Out-of-distribution data is handled gracefully via `__UNK__` and `__MISSING__` tokens
- **Schema-portable:** Separates model artifacts (`model_v1.bin`) from feature definitions (`feature_schema_v1.art`)
- **Multi-mode:** Accept raw CSV, raw JSON, or pre-engineered float vectors

---

## Key Features

### 1. **Three-Class Decisions**
Instead of binary yes/no, MobiusPL scores with policy thresholds:
```
proba < abstain_low      → AUTO_NEG       (automatic rejection)
abstain_low ≤ proba ≤ abstain_high  → MANUAL_REVIEW  (escalate to humans)
proba > abstain_high     → AUTO_POS       (automatic approval)
```
This reduces false positives and keeps challenging cases for human review.

### 2. **SHA256-Chained Audit Trail**
Every decision appends to a tamper-proof JSONL log:
```jsonl
{"ts":1730610142,"request_id":"req1","subject_id":"subj1",...,"decision":"AUTO_POS","prev_hash":"0000...","curr_hash":"abcd..."}
{"ts":1730610143,"request_id":"req2",...,"decision":"MANUAL_REVIEW",...}
```
- Hash chain proves no events were deleted or reordered
- Checkpoint signatures on log rotation
- Self-check via `./mobius_score --self-check`

### 3. **Raw Data Featurization**
Train on raw CSV, score using:
- **Raw CSV:** `--raw-csv` with `--schema` artifact
- **Raw JSON:** `--raw-json` with `--schema` artifact
- **Engineered floats:** Legacy mode (no schema needed)

### 4. **Automatic Feature Discovery**
During training:
- Detects numeric vs categorical columns
- Learns category tokens and frequencies
- Computes numeric means for imputation
- Tracks min-max scaling bounds
- Generates portable schema artifact

### 5. **Robust Missing/Unknown Handling**
- Missing numeric values → column mean
- Unknown categories → `__UNK__` token
- Missing categories → `__MISSING__` token
- No crashes on OOD data

### 6. **Artifact Integrity**
- Model payload SHA256 hash (detect corruption)
- Schema CRC32 in model header (verify schema alignment)
- All floats serialized in binary (exact reproducibility)

---

## Architecture

### Data Flow: Training

```
Raw CSV (with header)
    ↓
[1] Skip header row (seen_header flag)
    ↓
[2] Parse each field, type-detect (numeric: parse float; categorical: collect token)
    ↓
[3] Build feature space (numeric features 1:1, categorical features 1:M via one-hot)
    ↓
[4] SGD training (2000 epochs, cross-entropy loss, 0.01 learning rate)
    ↓
[5] Evaluate on test set (80-20 split, compute accuracy/precision/recall/F1)
    ↓
[6] Scan training CSV again
    ↓
    ├─ Collect numeric column means (imputation values)
    ├─ Extract category tokens
    └─ Compute scaler bounds (min-max per feature)
    ↓
[7] Persist schema artifact (feature_schema_v1.art, percent-encoded text)
    ↓
[8] Persist model artifact (model_v1.bin, binary format with SHA256 payload hash)
```

### Data Flow: Scoring (Raw CSV Example)

```
Raw record: "39,State-gov,77516,Bachelors,13,Never-married,Adm-clerical,Not-in-family,White,Male,2174,0,40,United-States"
    ↓
[1] CLI parsing (collect --schema, --raw-csv flags)
    ↓
[2] Load model_v1.bin (validate magic, version, checksum)
    ↓
[3] Load feature_schema_v1.art (reconstruct column metadata, categories)
    ↓
[4] Parse raw CSV line into 14 fields
    ↓
[5] Featurize per schema:
    ├─ Numeric field → parse float, check bounds
    ├─ Numeric missing → use column mean
    ├─ Categorical field → one-hot encode
    ├─ Category unknown → encode __UNK__ token
    └─ Category missing → encode __MISSING__ token
    ↓
[6] Generate dense feature vector (104 floats)
    ↓
[7] Apply min-max scaling (per-feature normalization)
    ↓
[8] Predict: sigmoid(w·x + b)
    ↓
[9] Apply policy thresholds → decision class
    ↓
[10] Acquire audit lock (flock)
    ↓
[11] Verify audit chain integrity (SHA256 replay)
    ↓
[12] Rotate audit if needed (by size or date)
    ↓
[13] Append decision event with chained hash
    ↓
[14] Release lock, output: "proba=0.823456 decision=AUTO_POS"
```

### Machine Learning Model

- **Algorithm:** Logistic regression (sigmoid activation)
- **Loss:** Binary cross-entropy
- **Optimizer:** SGD (stochastic gradient descent)
- **Epochs:** 2000 (configurable)
- **Learning rate:** 0.01 (configurable)
- **Train/test split:** 80/20
- **Feature scaling:** Min-max normalization (stored per-feature)

### File Formats

#### Model Artifact (`model_v1.bin`)

```
[Header]
  uint32_t magic              (0x4D4F4249 = "MOBI")
  uint32_t version            (1)
  uint64_t created_unix       (timestamp)
  uint32_t feature_count      (number of features)
  uint32_t schema_hash32      (CRC32 of schema for validation)
  float    decision_threshold (unused, for future)
  float    abstain_low        (policy threshold)
  float    abstain_high       (policy threshold)
  uint8_t  payload_sha256[32] (SHA256 of w, b, scalers)

[Payload]
  float    w[feature_count]   (weights)
  float    b                  (bias)
  float    scaler_mn[feature_count]  (min per feature)
  float    scaler_mx[feature_count]  (max per feature)
```

#### Feature Schema Artifact (`feature_schema_v1.art`)

```
Line 1: VERSION|RAW_COL_COUNT|FEATURE_COUNT

Numeric columns:
  NUMERIC|col_name|feature_idx|mean_value

Categorical columns:
  CATEGORY|col_idx|token%20with%20spaces|feature_idx
  CATEGORY|col_idx|another_token|feature_idx
  ...
```

**Percent-encoding:** Handles commas/pipes in token values via `%XX` hex encoding.

#### Audit Trail (`decision_audit.jsonl`)

```jsonl
{"ts":1730610142,"request_id":"req1","subject_id":"subj1","model_version":1,"schema_hash32":2147483647,"threshold":0.5,"abstain_low":0.3,"abstain_high":0.7,"proba":0.823456,"decision":"AUTO_POS","prev_hash":"0000000000000000000000000000000000000000000000000000000000000000","curr_hash":"abcd1234..."}
{"ts":1730610143,"type":"CHECKPOINT","rotated_from":"decision_audit.jsonl.20261104-143025","prev_hash":"abcd1234...","checkpoint_sig":"efgh5678...","curr_hash":"ijkl9999..."}
```

---

## Use Cases

### 1. **Credit Lending**
- **Input:** Loan application (age, income, employment, credit history)
- **Output:** AUTO_NEG (deny), MANUAL_REVIEW (call underwriter), AUTO_POS (preapprove)
- **Audit:** Prove to regulators why each decision was made
- **Compliance:** FCRA-ready (Fair Credit Reporting Act)

### 2. **Fraud & Abuse Detection**
- **Input:** Transaction features (device, country, velocity, amount)
- **Output:** Block / Review / Allow
- **Audit:** Prove system wasn't biased against specific groups
- **Compliance:** GDPR transparency (Article 22)

### 3. **Insurance Underwriting**
- **Input:** Applicant demographics + risk factors
- **Output:** Risk tier + premium recommendation
- **Audit:** Document underwriting logic for regulators
- **Compliance:** Fair lending standards

### 4. **Identity Verification / KYC**
- **Input:** User profile data
- **Output:** Accept / Manual Review / Reject
- **Audit:** Prove consistent application of rules
- **Compliance:** AML/KYC regulations

---

## Quick Start

### 1. Build

```bash
cd /mnt/Basefiles/CLearn/bkclib/MobiusPL
gcc -O2 -Wall -Wextra -std=c11 -DPOSIX_C_SOURCE=200809L \
  mobius_pl.c feature_schema_artifact.c -lcrypto -lm -o mobius_pl

gcc -O2 -Wall -Wextra -std=c11 -DPOSIX_C_SOURCE=200809L \
  mobius_score.c feature_schema_artifact.c -lcrypto -lm -o mobius_score
```

### 2. Train

```bash
export MOBIUS_AUDIT_SIGN_KEY="my-secret-key-32-chars-min!!!"
./mobius_pl training_data.csv 2000 0.01
# Outputs: model_v1.bin, feature_schema_v1.art
```

### 3. Score (Raw CSV)

```bash
echo "39,State-gov,77516,Bachelors,13,Never-married,Adm-clerical,Not-in-family,White,Male,2174,0,40,United-States" | \
  ./mobius_score model_v1.bin req1 subj1 --schema feature_schema_v1.art --raw-csv

# Output:
# proba=0.823456 decision=AUTO_POS
```

### 4. Verify Audit

```bash
./mobius_score --self-check decision_audit.jsonl
# Output:
# self-check OK lines=42 last_hash=abcd1234...
```

---

## Installation

### Requirements

- **OS:** Linux/POSIX (requires `flock`, `gmtime_r`)
- **Compiler:** GCC or Clang with C11 support
- **Libraries:** OpenSSL 1.1+ (`libcrypto`)
- **Build:** Make or manual `gcc` commands

### Build from Source

```bash
# Clone/extract repo
cd bkclib/MobiusPL

# Compile both binaries
gcc -O2 -Wall -Wextra -std=c11 -DPOSIX_C_SOURCE=200809L \
  mobius_pl.c feature_schema_artifact.c -lcrypto -lm -o mobius_pl

gcc -O2 -Wall -Wextra -std=c11 -DPOSIX_C_SOURCE=200809L \
  mobius_score.c feature_schema_artifact.c -lcrypto -lm -o mobius_score

# Verify
./mobius_pl --help 2>&1 | head -1  # Will show usage if compiled
./mobius_score --help 2>&1 | head -1
```

### Optional: Install to System

```bash
sudo install -m 755 mobius_pl /usr/local/bin/
sudo install -m 755 mobius_score /usr/local/bin/
sudo install -m 644 include/feature_schema_artifact.h /usr/local/include/
```

---

## Training

### Training Command

```bash
./mobius_pl <training_csv> [epochs] [learning_rate]
```

**Arguments:**
- `training_csv`: Path to CSV file with header row (first line is column names)
- `epochs`: SGD epochs (default: 2000)
- `learning_rate`: SGD learning rate (default: 0.01)

### Example Training

```bash
./mobius_pl data/adult_income.csv 2000 0.01
```

**Expected Output:**
```
Training logistic regression model from data/adult_income.csv
Rows total: 32561, train: 26048, test: 6513
Features extracted: 104
Epochs: 2000, learning_rate: 0.01
...
[Epoch 500] train_loss=0.456, test_loss=0.468
[Epoch 1000] train_loss=0.389, test_loss=0.401
[Epoch 1500] train_loss=0.345, test_loss=0.358
[Epoch 2000] train_loss=0.312, test_loss=0.325

Evaluation:
  Accuracy:  0.849
  Precision: 0.867
  Recall:    0.792
  F1-Score:  0.828

Saving artifacts:
  model_v1.bin (checksum: abc123...)
  feature_schema_v1.art
Done!
```

### Training CSV Format

**Required:**
- First line: column headers (comma-separated)
- Subsequent lines: data rows (comma-separated)
- No special escaping needed (trainer uses simple CSV parsing)

**Example:**
```
age,workclass,fnlwgt,education,education_num,marital_status,occupation,relationship,race,sex,capital_gain,capital_loss,hours_per_week,native_country
39,State-gov,77516,Bachelors,13,Never-married,Adm-clerical,Not-in-family,White,Male,2174,0,40,United-States
50,Self-emp-inc,83311,Bachelors,13,Married-civ-spouse,Exec-managerial,Husband,White,Male,15024,0,13,United-States
38,Private,215646,HS-grad,9,Divorced,Handlers-cleaners,Not-in-family,White,Male,0,0,40,United-States
```

### What Trainer Does

1. **Reads CSV:** Parses header, detects column count
2. **Type detection:** Infers numeric vs categorical per column
3. **Feature extraction:**
   - Numeric: 1 feature per column
   - Categorical: 1 feature per unique value (one-hot encoding)
4. **Train/test split:** 80% train, 20% test (random shuffle)
5. **SGD training:** Learns weights via cross-entropy minimization
6. **Schema collection:** Extracts means, categories, scaler bounds
7. **Saves artifacts:**
   - `model_v1.bin`: Binary model (weights, bias, scalers)
   - `feature_schema_v1.art`: Text schema (column metadata, categories)

---

## Scoring

### Scoring Command

```bash
./mobius_score <model.bin> <request_id> <subject_id> [audit.jsonl] [options]
```

**Required Arguments:**
- `model.bin`: Path to trained model (from trainer)
- `request_id`: Unique request identifier (for audit trail)
- `subject_id`: Subject being scored (person, account, etc.)

**Optional Arguments:**
- `audit.jsonl`: Audit trail path (default: `decision_audit.jsonl`)
- `--schema <path>`: Path to feature schema (required for raw modes)
- `--raw-csv`: Parse stdin as raw CSV (requires `--schema`)
- `--raw-json`: Parse stdin as raw JSON (requires `--schema`)

**Input:** Assumes data on stdin (pipe from file or echo)

**Output:** `proba=X.XXXXXX decision=CLASS` on stdout, audit entry appended to file

### Scoring Modes

#### Mode 1: Engineered Floats (Legacy)

```bash
echo "0.5,0.3,0.2,0.1,..." | ./mobius_score model.bin req1 subj1
# proba=0.845 decision=AUTO_POS
```

- Input: 104 comma-separated floats (must match model's feature count)
- No schema needed
- Fast path (skip featurization)

#### Mode 2: Raw CSV

```bash
echo "39,State-gov,77516,Bachelors,...,United-States" | \
  ./mobius_score model.bin req1 subj1 --schema feature_schema_v1.art --raw-csv
# proba=0.823 decision=AUTO_POS
```

- Input: Raw CSV fields (14 in this example)
- Schema required (provides column metadata)
- Featurization via schema (handles missing/unknown)

#### Mode 3: Raw JSON

```bash
echo '{"age":"39","workclass":"State-gov","education":"Bachelors",...}' | \
  ./mobius_score model.bin req1 subj1 --schema feature_schema_v1.art --raw-json
# proba=0.823 decision=AUTO_POS
```

- Input: Flat JSON (`{"key":"value",...}`)
- Key order doesn't matter
- Handles missing keys gracefully

### Audit Trail Operation

When scoring:

1. **Lock:** Acquire exclusive lock on `audit.jsonl.lock` (prevents concurrent writes)
2. **Load:** Read last hash from audit chain
3. **Verify:** Replay all events, validate chain integrity
4. **Rotate:** Check if audit needs rotation (by size or date)
   - If yes: Rename current file, write checkpoint event with signature
5. **Append:** Create decision event, compute SHA256 of (prev_hash || event), write with new hash
6. **Unlock:** Release lock

**Environment variables:**
- `MOBIUS_AUDIT_SIGN_KEY`: Secret key for checkpoint signatures (required)
- `MOBIUS_AUDIT_MAX_BYTES`: Max audit file size before rotation (default: 5 MiB)

### Error Handling

```bash
# Missing schema with --raw-csv
echo "39,State-gov,..." | ./mobius_score model.bin req1 subj1 --raw-csv
# stderr: error: --raw-csv and --raw-json require --schema <path>
# exit code: 1

# Invalid model
./mobius_score corrupt.bin req1 subj1
# stderr: load_model_artifact failed
# exit code: 1

# Audit chain corrupted
./mobius_score model.bin req1 subj1
# stderr: audit self-check failed: chain invalid
# exit code: 1

# Missing audit signing key
./mobius_score model.bin req1 subj1  # MOBIUS_AUDIT_SIGN_KEY not set
# stderr: MOBIUS_AUDIT_SIGN_KEY is required for checkpoint signing
# exit code: 1
```

---

## Input Modes

### Raw CSV

**Example input:**
```
39,State-gov,77516,Bachelors,13,Never-married,Adm-clerical,Not-in-family,White,Male,2174,0,40,United-States
```

**Processing:**
- Split on commas
- Pass field-by-field to schema
- Numeric field: parse float, use value
- Missing numeric: use column mean
- Categorical field: lookup in category map
- Unknown category: use `__UNK__` token
- Missing category: use `__MISSING__` token

**Invocation:**
```bash
cat raw_data.csv | ./mobius_score model.bin req1 subj1 --schema schema.art --raw-csv
```

### Raw JSON

**Example input:**
```json
{"age":"39","workclass":"State-gov","education":"Bachelors","marital_status":"Never-married","occupation":"Adm-clerical"}
```

**Processing:**
- Parse flat JSON (no nesting)
- Per column in schema: lookup in JSON by key
- If key exists: use value; if missing: use default (mean for numeric, `__MISSING__` for categorical)
- Unknown categories still map to `__UNK__`

**Invocation:**
```bash
echo '{"age":"39","workclass":"State-gov",...}' | ./mobius_score model.bin req1 subj1 --schema schema.art --raw-json
```

**Handling edge cases:**
- Extra keys in JSON: ignored
- Missing keys in JSON: treated as missing (mean imputation or `__MISSING__`)
- Numeric field with "abc": treated as missing (mean imputation)
- Nested JSON: not supported (flattened keys only)

### Engineered Vector (Backward Compatibility)

**Example input:**
```
0.45,0.82,0.10,0.91,0.33,...,0.22
```
(104 floats)

**Processing:**
- Split on commas
- Parse all as floats
- Apply min-max scaling (bounds from model)
- Pass to sigmoid predictor
- No schema needed

**Invocation:**
```bash
echo "0.45,0.82,..." | ./mobius_score model.bin req1 subj1
```

---

## Feature Schema

### Schema Artifact Format

```
VERSION|RAW_COL_COUNT|FEATURE_COUNT
NUMERIC|age|0|47.5
NUMERIC|fnlwgt|1|189778.1
CATEGORY|1|Private|2
CATEGORY|1|Self-emp-not-inc|3
CATEGORY|1|Self-emp-inc|4
CATEGORY|1|Federal-gov|5
CATEGORY|1|Local-gov|6
CATEGORY|1|State-gov|7
CATEGORY|1|Without-pay|8
CATEGORY|1|Never-worked|9
CATEGORY|2|Bachelors|10
CATEGORY|2|Masters|11
CATEGORY|2|Doctorate|12
...
```

**Schema Structure:**

- **Header line:** `VERSION=1|RAW_COL_COUNT=14|FEATURE_COUNT=104`
  - `VERSION`: Format version (currently 1)
  - `RAW_COL_COUNT`: Number of original CSV columns
  - `FEATURE_COUNT`: Total features after one-hot encoding

- **NUMERIC lines:** `NUMERIC|col_name|feature_idx|mean_value`
  - `col_name`: Original column name (from CSV header)
  - `feature_idx`: Index in final feature vector (0-based)
  - `mean_value`: Mean value for imputation (used when missing during scoring)

- **CATEGORY lines:** `CATEGORY|col_idx|token|feature_idx`
  - `col_idx`: Original column index (0-based)
  - `token`: Category value (may be percent-encoded)
  - `feature_idx`: Index in final feature vector

**Percent-encoding:**
- Characters: `,`, `|`, `%` are encoded as `%2C`, `%7C`, `%25`
- Applied during schema save, decoded during schema load
- Enables tokens with special characters

### Schema Lifecycle

**Training Phase:**
1. Trainer reads CSV, detects column types
2. Numeric columns: extract means, assign feature indices
3. Categorical columns: collect unique tokens, assign feature indices
4. Save to `feature_schema_v1.art` (percent-encoded)

**Scoring Phase (Raw Mode):**
1. Load schema file, reconstruct metadata
2. Per input field: lookup in schema
3. Numeric: use value or mean; validate range if scaler bounds available
4. Categorical: lookup token in category map or use `__UNK__`

**Validation:**
- Schema hash (CRC32) stored in model header
- Optional: compute schema hash during scoring, compare with model
- Mismatch indicates train/score misalignment

---

## Audit Trail

### Purpose

Prove that every decision:
- Was made at a specific time
- Used a specific model version
- Was logged immediately (not altered post-hoc)
- Followed consistent logic

### Format

```jsonl
{"ts":1730610142,"request_id":"req1","subject_id":"subj1","model_version":1,"schema_hash32":2147483647,"threshold":0.5,"abstain_low":0.3,"abstain_high":0.7,"proba":0.823456,"decision":"AUTO_POS","prev_hash":"0000000000000000000000000000000000000000000000000000000000000000","curr_hash":"abcd1234..."}
{"ts":1730610143,"request_id":"req2",...}
{"ts":1730610200,"type":"CHECKPOINT","rotated_from":"decision_audit.jsonl.20261104-143025","prev_hash":"abcd1234...","checkpoint_sig":"efgh5678...","curr_hash":"ijkl9999..."}
```

**Decision Event Fields:**
- `ts`: Unix timestamp
- `request_id`: Request identifier (from CLI)
- `subject_id`: Subject ID (from CLI)
- `model_version`: Model version (from header)
- `schema_hash32`: Schema hash (from header)
- `threshold`: Decision threshold (unused)
- `abstain_low`, `abstain_high`: Policy thresholds
- `proba`: Predicted probability
- `decision`: Decision class (AUTO_NEG, MANUAL_REVIEW, AUTO_POS)
- `prev_hash`: SHA256 of previous event
- `curr_hash`: SHA256(prev_hash || event_json_prefix)

**Checkpoint Event Fields:**
- `ts`: Rotation timestamp
- `type`: "CHECKPOINT"
- `rotated_from`: Old filename
- `prev_hash`: Previous event's hash
- `checkpoint_sig`: HMAC signature of rotation (proves key holder initiated)
- `curr_hash`: Hash of this checkpoint event

### Verification

**Manual verification:**
```bash
./mobius_score --self-check decision_audit.jsonl
# Output: self-check OK lines=1234 last_hash=abcd1234...
```

**Programmatic verification:**
```c
uint8_t last_hash[32];
size_t lines = 0;
if (audit_verify_chain_file("decision_audit.jsonl", last_hash, &lines)) {
    printf("Valid: %zu events\n", lines);
} else {
    printf("Invalid: chain corrupted\n");
}
```

**What gets verified:**
1. Each event has valid `prev_hash` and `curr_hash`
2. Computed hash matches `curr_hash` (proves no tampering)
3. Chain links properly (each event's `prev_hash` == previous's `curr_hash`)
4. No missing events (can detect deleted lines)

### Rotation

**Triggers:**
- File size exceeds `MOBIUS_AUDIT_MAX_BYTES` (default: 5 MiB)
- Date boundary crossed (new UTC day)

**Process:**
1. Rename current file: `decision_audit.jsonl` → `decision_audit.jsonl.20261104-143025`
2. Create new file with CHECKPOINT event
3. CHECKPOINT event includes:
   - Signature (proves authorized rotation)
   - Reference to old filename
   - Link to previous chain

**Recovery:**
Even after rotation, full audit trail is preserved:
- `decision_audit.jsonl` (current, active)
- `decision_audit.jsonl.20261104-143025` (archived)
- `decision_audit.jsonl.20261103-235959` (archived)
- ... etc

---

## Configuration

### Environment Variables

#### MOBIUS_AUDIT_SIGN_KEY
**Required** for scoring (audit trail operation)

```bash
export MOBIUS_AUDIT_SIGN_KEY="my-secret-key-minimum-32-chars!!!"
./mobius_score model.bin req1 subj1
```

- Used to sign checkpoint events during audit rotation
- Must be at least 32 characters (recommendation: 64+ random)
- Same key across cluster required for verification
- Never ship in code; use secrets management (vault, env, etc.)

#### MOBIUS_AUDIT_MAX_BYTES
**Optional**, controls audit rotation

```bash
# Rotate at 10 MiB
export MOBIUS_AUDIT_MAX_BYTES=10485760
./mobius_score model.bin req1 subj1

# Rotate at 1 GiB
export MOBIUS_AUDIT_MAX_BYTES=1073741824
```

Default: 5 MiB (5242880 bytes)

### Model Configuration

**During training:** Pass epochs and learning rate

```bash
./mobius_pl data.csv 5000 0.001
```

- More epochs (5000 vs 2000): slower convergence, more training time
- Lower learning rate (0.001 vs 0.01): finer updates, more epochs needed

**During scoring:** Policy thresholds embedded in model

These are baked into `model_v1.bin` at training time. To change:
1. Retrain model with different thresholds (not configurable at scoring time)
2. Deploy new model artifact

---

## API Reference

### Trainer (mobius_pl)

**Key Functions:**

#### schema_init()
```c
void schema_init(FeatureSchema *s);
```
Initialize empty schema.

#### schema_add_column()
```c
bool schema_add_column(FeatureSchema *s, const char *name, SchemaColumnKind kind);
```
Add column definition (SCHEMA_KIND_NUMERIC or SCHEMA_KIND_CATEGORICAL).

#### schema_add_category()
```c
bool schema_add_category(FeatureSchema *s, size_t col_idx, const char *token);
```
Register category for categorical column.

#### schema_save_artifact()
```c
bool schema_save_artifact(const FeatureSchema *s, const char *path);
```
Write schema to text file (percent-encoded).

#### schema_find_category_feature()
```c
bool schema_find_category_feature(const FeatureSchema *s, size_t col_idx, 
                                   const char *token, size_t *out_feature_idx);
```
Lookup feature index for (column, category) pair.

### Scorer (mobius_score)

**Key Functions:**

#### schema_load_artifact()
```c
bool schema_load_artifact(const char *path, FeatureSchema *s);
```
Load schema from text file.

#### schema_parse_csv_fields()
```c
bool schema_parse_csv_fields(const char *line, char **out_fields, size_t max_fields, size_t *out_count);
```
Parse CSV line into field array (handles escaping).

#### schema_json_get_flat_value()
```c
bool schema_json_get_flat_value(const char *json, const char *key, char *out_val, size_t val_sz);
```
Extract value from flat JSON by key.

#### featurize_raw_csv_line()
```c
bool featurize_raw_csv_line(const FeatureSchema *s, const char *line, float *x_out);
```
Convert raw CSV line to feature vector using schema.

#### featurize_raw_json_line()
```c
bool featurize_raw_json_line(const FeatureSchema *s, const char *json, float *x_out);
```
Convert flat JSON to feature vector using schema.

#### load_model_artifact()
```c
bool load_model_artifact(const char *path, ModelArtifact *out);
```
Load and validate model (magic, version, checksum).

#### predict_proba_scaled()
```c
float predict_proba_scaled(const ModelArtifact *a, const float *x_scaled);
```
Compute P(class=1 | x) via sigmoid(w·x + b) [(0, 1) range].

#### policy_decide()
```c
DecisionResult policy_decide(float proba, float low, float high);
```
Apply policy thresholds to probability.

#### audit_append_event()
```c
bool audit_append_event(const char *audit_path, AuditChainState *st,
                        const char *request_id, const char *subject_id,
                        const ModelArtifact *a, DecisionResult dr);
```
Log decision with chained hash, rotate if needed.

#### audit_verify_chain_file()
```c
bool audit_verify_chain_file(const char *audit_path, uint8_t out_last_hash[32], size_t *out_lines);
```
Validate entire audit trail integrity.

---

## Examples

### Example 1: Complete Workflow (Adult Income Dataset)

```bash
# Download data
wget https://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.data -O adult.csv

# Add header
sed -i '1s/^/age,workclass,fnlwgt,education,education_num,marital_status,occupation,relationship,race,sex,capital_gain,capital_loss,hours_per_week,native_country\n/' adult.csv

# Train (2000 epochs, lr=0.01)
./mobius_pl adult.csv

# Score one record
echo "39,State-gov,77516,Bachelors,13,Never-married,Adm-clerical,Not-in-family,White,Male,2174,0,40,United-States" | \
  ./mobius_score model_v1.bin req001 person001 --schema feature_schema_v1.art --raw-csv

# Verify audit
./mobius_score --self-check decision_audit.jsonl
```

### Example 2: Batch Scoring (Scripts)

```bash
#!/bin/bash
# score_batch.sh - Score CSV file line-by-line

export MOBIUS_AUDIT_SIGN_KEY="my-secret-key"

while IFS= read -r line; do
    echo "$line" | ./mobius_score model_v1.bin batch_req_$RANDOM subj_$RANDOM \
        --schema feature_schema_v1.art --raw-csv >> results.jsonl
done < input.csv

# Audit
./mobius_score --self-check decision_audit.jsonl
```

### Example 3: Legacy Engineered Vector Mode

```bash
# Pre-compute 104 features elsewhere (old system)
FEATURES="0.45,0.82,0.10,0.91,0.33,...,0.22"

# Score with engineered vector (no schema needed)
echo "$FEATURES" | ./mobius_score model_v1.bin req_legacy subj_123

# Output: proba=0.734 decision=MANUAL_REVIEW
```

### Example 4: JSON Input

```bash
# Create JSON record
cat > record.json <<EOF
{
  "age": "45",
  "workclass": "Private",
  "education": "Bachelors",
  "marital_status": "Married-civ-spouse",
  "occupation": "Exec-managerial",
  "race": "White",
  "sex": "Male",
  "hours_per_week": "40",
  "native_country": "United-States"
}
EOF

# Score
cat record.json | ./mobius_score model_v1.bin req_json subj_456 \
  --schema feature_schema_v1.art --raw-json

# Output: proba=0.891 decision=AUTO_POS
```

### Example 5: Error Handling

```bash
#!/bin/bash

# Detect and handle errors gracefully
if ! echo "39,State-gov,..." | ./mobius_score model.bin req1 subj1 \
    --schema schema.art --raw-csv >/dev/null 2>&1; then
    echo "WARN: Scoring failed"
    exit 1
fi

# Verify audit chain
if ! ./mobius_score --self-check decision_audit.jsonl >/dev/null 2>&1; then
    echo "ERROR: Audit trail corrupted"
    exit 1
fi

echo "OK: All checks passed"
```

---

## Performance

### Computational Complexity

| Operation | Complexity | Time (typical) |
|-----------|-----------|---|
| CSV training (N samples, F features) | O(N·F·E) | 10–60s (N=30K, F=100, E=2000) |
| Model load | O(F) | <1ms |
| Raw CSV featurization | O(F + C) | 1–5ms (F=100, C=50 categories) |
| Prediction | O(F) | <1ms |
| Audit write + chain verify | O(L) | 5–50ms (L=audit lines) |
| **Total scoring** | O(F + L) | 10–100ms/request |

### Throughput

- **Batch mode:** ~1,000–10,000 decisions/sec (depends on audit chain size)
- **Streaming mode:** Bounded by audit lock contention; use connection pooling

### Memory

- **Model artifact:** ~1 MiB (104 features × 4 bytes × 2 = weights + scalers + bias)
- **Schema artifact:** ~20–100 KiB (categories, mappings)
- **Audit file:** Grows ~500 bytes/decision (after rotation)

### Optimization Tips

1. **Audit rotation:** Set `MOBIUS_AUDIT_MAX_BYTES` to keep active file <100 MiB
2. **Locking:** If high concurrency, consider sharding by subject_id (separate audit files)
3. **Model selection:** Use minimal features (fewer features = faster scoring)
4. **Batch processing:** Amortize model load, lock acquisition over N records

---

## Security

### Model Integrity

**Threat:** Attacker modifies model weights to always approve

**Mitigation:**
- Model payload SHA256 embedded in header
- On load, recompute hash, verify match
- Hash includes all weights, bias, scalers

### Audit Trail Non-Repudiation

**Threat:** System operator deletes audit events to hide decisions

**Mitigation:**
- SHA256 chain makes deletion obvious (next event's `prev_hash` won't match)
- Checkpoint signatures prove authorized rotations
- Archive rotated files (immutable storage recommended)

**Best practices:**
- Store audit on separate, immutable system (append-only filesystem, S3 with versioning, syslog)
- Rotate to archive daily
- Verify chain integrity on access (compliance audits)

### Key Management

**MOBIUS_AUDIT_SIGN_KEY:**
- Never hardcode in production
- Use secrets vault (HashiCorp Vault, AWS Secrets Manager, etc.)
- Rotate periodically (changes key, but doesn't invalidate old audit tapes)
- Different key per environment (dev/staging/prod)

### Access Control

**Recommendation:**
- Scorer binary: world-readable, only run by authorized services
- Model/schema artifacts: readable by scoring service, world-unwritable
- Audit trail: append-only to authorized service, read-only to compliance/audit teams
- MOBIUS_AUDIT_SIGN_KEY: only accessible to scoring service (not shell, not logs)

### Cryptography

- **Hash:** SHA256 (NIST-approved, resistant to collision attacks)
- **Encoding:** Hex (safe for text transmission/storage)
- **Randomness:** Linux `/dev/urandom` for any stochastic components (not currently used)

---

## Troubleshooting

### Build Errors

#### `undefined reference to 'EVP_MD_CTX_new'`
**Solution:** Add `-lcrypto` to linker:
```bash
gcc ... -lcrypto -lm
```

#### `error: unknown type name 'ssize_t'`
**Solution:** Add `-DPOSIX_C_SOURCE=200809L` to compiler:
```bash
gcc -std=c11 -DPOSIX_C_SOURCE=200809L ...
```

#### `fatal error: openssl/evp.h: No such file`
**Solution:** Install OpenSSL dev headers:
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# CentOS/RHEL
sudo yum install openssl-devel
```

### Training Errors

#### `Error: CSV parsing failed at line X`
**Cause:** Malformed CSV (extra commas, unmatched quotes)

**Solution:**
- Verify CSV with: `head -5 input.csv`
- Check for embedded commas in fields (trainer expects simple CSV)
- Ensure all rows have same column count

#### `Error: feature count mismatch`
**Cause:** Training data has inconsistent columns

**Solution:**
- Count columns in header: `head -1 data.csv | tr ',' '\n' | wc -l`
- Count in first data line: `head -2 data.csv | tail -1 | tr ',' '\n' | wc -l`
- Should match

### Scoring Errors

#### `error: --schema is only valid with --raw-csv or --raw-json`
**Cause:** Used `--schema` flag without raw mode

**Solution:**
- Remove `--schema` for engineered vector mode, OR
- Add `--raw-csv` or `--raw-json` flag

#### `error: --raw-csv and --raw-json require --schema <path>`
**Cause:** Used raw mode without schema

**Solution:**
- Ensure schema artifact exists: `ls -la feature_schema_v1.art`
- Update path if schema moved: `./mobius_score ... --schema /path/to/schema.art --raw-csv`

#### `schema/model feature_count mismatch`
**Cause:** Schema doesn't match model (trained on different data)

**Solution:**
- Retrain model and schema together (same training data)
- Check that `feature_schema_v1.art` is from same training run

#### `proba NaN or Inf` in output
**Cause:** Feature vector contains invalid values after featurization

**Solution:**
- Check input: `echo "$input" | head -c 200`
- Verify schema: `head -20 feature_schema_v1.art`
- If numeric field has non-numeric value: ensure imputation mean is valid

#### `audit self-check failed: chain invalid`
**Cause:** Audit trail corrupted (deleted/modified event)

**Solution:**
- Archive current audit: `mv decision_audit.jsonl decision_audit.jsonl.backup`
- Start fresh: `rm decision_audit.jsonl decision_audit.jsonl.lock`
- Resume scoring (new chain will initialize)
- Investigate backup for forensics

#### `MOBIUS_AUDIT_SIGN_KEY is required`
**Cause:** Environment variable not set before running scorer

**Solution:**
```bash
export MOBIUS_AUDIT_SIGN_KEY="your-secret-key"
./mobius_score ...
```

### Performance Issues

#### Scoring takes >1 second per request
**Cause:** Audit chain verification overhead (large audit file)

**Solution:**
- Check audit file size: `ls -lh decision_audit.jsonl`
- If >100 MiB: force rotation
  ```bash
  mv decision_audit.jsonl decision_audit.jsonl.archive
  ```
- Lower `MOBIUS_AUDIT_MAX_BYTES` for more frequent rotation:
  ```bash
  export MOBIUS_AUDIT_MAX_BYTES=1000000  # 1 MiB
  ```

#### Memory errors during training (large dataset)
**Cause:** In-memory feature space too large

**Solution:**
- Subsample training data: `head -100000 data.csv > data_small.csv`
- Compile with more stack: `ulimit -s unlimited`
- Use machine with more RAM

### Data Quality Issues

#### Many predictions are NaN
**Cause:** Input containing invalid numeric values

**Solution:**
- Clean data: `sed '/[^0-9,.-]/d' input.csv`
- Check for special characters: `file input.csv`
- Verify against training data: `head data.csv`

#### All predictions are identical probability
**Cause:** All features are zero or constant

**Solution:**
- Verify featurization: Add debug output to `featurize_raw_csv_line`
- Check schema: `cat feature_schema_v1.art | grep "^NUMERIC" | head -5`
- Test with known-good training data

---

## Compliance & Regulatory

### Fair Lending (FCRA, ECOA)

**MobiusPL supports:**
- ✅ Transparent decision logic (logistic regression, explainable)
- ✅ Audit trail (every decision logged with timestamp)
- ✅ Consistent application (same model across subjects)
- ✅ Record of consideration (all features logged)

**You must still:**
- ❌ Validate model for disparate impact (not built-in)
- ❌ Maintain adverse action notices (handle in application layer)
- ❌ Implement right-to-explain (output decision + top features separately)
- ❌ Periodic model recalibration (outside MobiusPL)

### GDPR Article 22 (Automated Decisions)

**MobiusPL supports:**
- ✅ Audit trail for decision traceability
- ✅ Human review path (MANUAL_REVIEW class)
- ✅ Reproducibility (SHA256-validated model)

**You must still:**
- ❌ Implement right to explanation (user-friendly output)
- ❌ Human oversight (business process)
- ❌ Data deletion (outside scoring, in your DB)

### AML/KYC Regulations

**MobiusPL supports:**
- ✅ Immutable audit log (for regulatory review)
- ✅ Timestamp + decision recording
- ✅ Risk score preservation

---

## Support & Contact

- **Documentation:** This README
- **Repository:** [MobiusPL on GitHub]
- **Issues:** Please report bugs with minimal reproducible examples

---

## Version History

### v1.0.0 (Nov 2024)
- Initial release
- Three-class decision support (AUTO_NEG, MANUAL_REVIEW, AUTO_POS)
- SHA256 audit chain with checkpoint signatures
- Raw CSV/JSON featurization with schema artifacts
- Automatic feature discovery during training
- Robust missing/unknown value handling
- Production-ready audit trail with rotation

---

**Copyright © 2024. All rights reserved.**
