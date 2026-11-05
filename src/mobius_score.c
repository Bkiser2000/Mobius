#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "../include/feature_schema_artifact.h"

#ifndef MOBIUS_VERSION
#define MOBIUS_VERSION "0.0.1"
#endif

#ifndef MOBIUS_BUILD_DATE
#define MOBIUS_BUILD_DATE "0.0.1"
#endif

#ifndef MOBIUS_COMMIT
#define MOBIUS_COMMIT "Production"
#endif

#define AUDIT_MAX_PATH 4096
#define AUDIT_DEFAULT_MAX_BYTES (5ULL * 1024ULL * 1024ULL) /* 5 MiB */
#define AUDIT_LOCK_SUFFIX ".lock"
#define MODEL_MAGIC 0x4D4F4249u /* MOBI */
#define MODEL_VERSION 1u
#define MODEL_HASH_LEN 32u
#define MAX_LINE 65536u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t created_unix;
    uint32_t feature_count;
    uint32_t schema_hash32;
    float decision_threshold;
    float abstain_low;
    float abstain_high;
    uint8_t payload_sha256[MODEL_HASH_LEN];
} ModelHeader;

typedef struct {
    ModelHeader h;
    float *w;         /* feature_count */
    float b;
    float *scaler_mn; /* feature_count */
    float *scaler_mx; /* feature_count */
} ModelArtifact;

typedef enum {
    DECISION_AUTO_NEG = 0,
    DECISION_MANUAL_REVIEW = 1,
    DECISION_AUTO_POS = 2
} DecisionClass;

typedef struct {
    float proba;
    DecisionClass cls;
} DecisionResult;

typedef struct {
    uint8_t prev_hash[MODEL_HASH_LEN];
    bool initialized;
} AuditChainState;

static void print_version(void) {
    printf("MobiusPL Scorer v%s\n", MOBIUS_VERSION);
    printf("Build Date: %s\n", MOBIUS_BUILD_DATE);
    printf("Commit: %s\n", MOBIUS_COMMIT);
}

static bool parse_float_token(const char *s, float *out) {
    if (!s || !out) return false;
    errno = 0;
    char *end = NULL;
    float v = strtof(s, &end);
    if (errno != 0 || end == s) return false;
    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) return false;
        end++;
    }
    *out = v;
    return true;
}

static bool sha256_hash_chunks(const void *const *chunks, const size_t *sizes, size_t count, uint8_t out[MODEL_HASH_LEN]) {
    if (!chunks || !sizes || !out) return false;

    bool ok = false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) goto done;
    for (size_t i = 0; i < count; i++) {
        if (sizes[i] == 0) continue;
        if (!chunks[i]) goto done;
        if (EVP_DigestUpdate(ctx, chunks[i], sizes[i]) != 1) goto done;
    }

    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out, &out_len) != 1) goto done;
    if (out_len != MODEL_HASH_LEN) goto done;

    ok = true;
done:
    EVP_MD_CTX_free(ctx);
    return ok;
}

static bool artifact_payload_hash(const ModelHeader *h, const float *w, float b, const float *mn, const float *mx, uint8_t out[MODEL_HASH_LEN]) {
    if (!h || !w || !mn || !mx || !out) return false;

    size_t n = (size_t)h->feature_count;
    const void *chunks[] = {
        &h->magic, &h->version, &h->created_unix, &h->feature_count,
        &h->schema_hash32, &h->decision_threshold, &h->abstain_low, &h->abstain_high,
        w, &b, mn, mx
    };
    const size_t sizes[] = {
        sizeof(h->magic), sizeof(h->version), sizeof(h->created_unix), sizeof(h->feature_count),
        sizeof(h->schema_hash32), sizeof(h->decision_threshold), sizeof(h->abstain_low), sizeof(h->abstain_high),
        n * sizeof(float), sizeof(b), n * sizeof(float), n * sizeof(float)
    };

    return sha256_hash_chunks(chunks, sizes, sizeof(chunks) / sizeof(chunks[0]), out);
}

static void artifact_free(ModelArtifact *a) {
    if (!a) return;
    free(a->w);
    free(a->scaler_mn);
    free(a->scaler_mx);
    a->w = NULL;
    a->scaler_mn = NULL;
    a->scaler_mx = NULL;
    memset(&a->h, 0, sizeof(a->h));
    a->b = 0.0f;
}

static bool load_model_artifact(const char *path, ModelArtifact *out) {
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    if (fread(&out->h, sizeof(out->h), 1, f) != 1) {
        fclose(f);
        return false;
    }
    if (out->h.magic != MODEL_MAGIC || out->h.version != MODEL_VERSION || out->h.feature_count == 0) {
        fclose(f);
        return false;
    }

    size_t n = (size_t)out->h.feature_count;
    out->w = (float *)malloc(n * sizeof(float));
    out->scaler_mn = (float *)malloc(n * sizeof(float));
    out->scaler_mx = (float *)malloc(n * sizeof(float));
    if (!out->w || !out->scaler_mn || !out->scaler_mx) {
        fclose(f);
        artifact_free(out);
        return false;
    }

    bool ok = true;
    if (fread(out->w, sizeof(float), n, f) != n) ok = false;
    if (ok && fread(&out->b, sizeof(float), 1, f) != 1) ok = false;
    if (ok && fread(out->scaler_mn, sizeof(float), n, f) != n) ok = false;
    if (ok && fread(out->scaler_mx, sizeof(float), n, f) != n) ok = false;
    fclose(f);

    if (!ok) {
        artifact_free(out);
        return false;
    }

    uint8_t calc[MODEL_HASH_LEN];
    if (!artifact_payload_hash(&out->h, out->w, out->b, out->scaler_mn, out->scaler_mx, calc)) {
        artifact_free(out);
        return false;
    }
    if (memcmp(calc, out->h.payload_sha256, MODEL_HASH_LEN) != 0) {
        artifact_free(out);
        return false;
    }

    return true;
}

static float sigmoidf_safe(float z) {
    if (z > 20.0f) return 1.0f;
    if (z < -20.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-z));
}

static float predict_proba_scaled(const ModelArtifact *a, const float *x_scaled) {
    float z = a->b;
    for (size_t j = 0; j < a->h.feature_count; j++) z += a->w[j] * x_scaled[j];
    return sigmoidf_safe(z);
}

static DecisionResult policy_decide(float proba, float low, float high) {
    DecisionResult r;
    r.proba = proba;
    if (proba < low) r.cls = DECISION_AUTO_NEG;
    else if (proba > high) r.cls = DECISION_AUTO_POS;
    else r.cls = DECISION_MANUAL_REVIEW;
    return r;
}

static const char *decision_class_str(DecisionClass c) {
    switch (c) {
        case DECISION_AUTO_NEG: return "AUTO_NEG";
        case DECISION_AUTO_POS: return "AUTO_POS";
        default: return "MANUAL_REVIEW";
    }
}

static void hex_encode(const uint8_t *in, size_t n, char *out_hex) {
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out_hex[i * 2] = hex[(in[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex[in[i] & 0xF];
    }
    out_hex[n * 2] = '\0';
}

static bool hex_nibble(char c, uint8_t *out) {
    if (!out) return false;
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { *out = (uint8_t)(10 + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'F') { *out = (uint8_t)(10 + (c - 'A')); return true; }
    return false;
}

static bool hex_decode_32(const char *hex, uint8_t out[MODEL_HASH_LEN]) {
    if (!hex || !out) return false;
    for (size_t i = 0; i < MODEL_HASH_LEN; i++) {
        uint8_t hi = 0, lo = 0;
        if (!hex_nibble(hex[i * 2], &hi)) return false;
        if (!hex_nibble(hex[i * 2 + 1], &lo)) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool extract_hash_field(const char *line, const char *marker, char out_hex[MODEL_HASH_LEN * 2 + 1]) {
    if (!line || !marker || !out_hex) return false;
    const char *p = strstr(line, marker);
    if (!p) return false;
    p += strlen(marker);

    for (size_t i = 0; i < MODEL_HASH_LEN * 2; i++) {
        uint8_t nib = 0;
        if (!hex_nibble(p[i], &nib)) return false;
        out_hex[i] = p[i];
    }
    if (p[MODEL_HASH_LEN * 2] != '"') return false;
    out_hex[MODEL_HASH_LEN * 2] = '\0';
    return true;
}

static bool audit_verify_chain_file(
    const char *audit_path,
    uint8_t out_last_hash[MODEL_HASH_LEN],
    size_t *out_lines
) {
    if (!audit_path || !out_last_hash) return false;

    memset(out_last_hash, 0, MODEL_HASH_LEN);
    if (out_lines) *out_lines = 0;

    FILE *f = fopen(audit_path, "r");
    if (!f) {
        if (errno == ENOENT) return true; /* no file yet is valid */
        return false;
    }

    uint8_t prev[MODEL_HASH_LEN];
    memset(prev, 0, sizeof(prev));

    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len == 0) continue;
        if (line[len - 1] == '\n') line[len - 1] = '\0';

        char prev_hex[MODEL_HASH_LEN * 2 + 1];
        char curr_hex[MODEL_HASH_LEN * 2 + 1];
        if (!extract_hash_field(line, "\"prev_hash\":\"", prev_hex) ||
            !extract_hash_field(line, "\"curr_hash\":\"", curr_hex)) {
            fclose(f);
            return false;
        }

        char expected_prev_hex[MODEL_HASH_LEN * 2 + 1];
        hex_encode(prev, MODEL_HASH_LEN, expected_prev_hex);
        if (strcmp(prev_hex, expected_prev_hex) != 0) {
            fclose(f);
            return false;
        }

        char *curr_marker = strstr(line, ",\"curr_hash\":\"");
        if (!curr_marker) {
            fclose(f);
            return false;
        }

        size_t prefix_len = (size_t)(curr_marker - line);
        uint8_t calc[MODEL_HASH_LEN];
        const void *chunks[] = { prev, line };
        const size_t sizes[] = { MODEL_HASH_LEN, prefix_len };
        if (!sha256_hash_chunks(chunks, sizes, 2, calc)) {
            fclose(f);
            return false;
        }

        char calc_hex[MODEL_HASH_LEN * 2 + 1];
        hex_encode(calc, MODEL_HASH_LEN, calc_hex);
        if (strcmp(calc_hex, curr_hex) != 0) {
            fclose(f);
            return false;
        }

        memcpy(prev, calc, MODEL_HASH_LEN);
        if (out_lines) (*out_lines)++;
    }

    fclose(f);
    memcpy(out_last_hash, prev, MODEL_HASH_LEN);
    return true;
}

static bool make_lock_path(const char *audit_path, char out[AUDIT_MAX_PATH]) {
    if (!audit_path || !out) return false;
    int n = snprintf(out, AUDIT_MAX_PATH, "%s%s", audit_path, AUDIT_LOCK_SUFFIX);
    return n > 0 && (size_t)n < AUDIT_MAX_PATH;
}

static int audit_lock_exclusive(const char *audit_path) {
    char lock_path[AUDIT_MAX_PATH];
    if (!make_lock_path(audit_path, lock_path)) return -1;

    int fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;

    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void audit_unlock_close(int fd) {
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

static int yyyymmdd_utc(time_t t) {
    struct tm tmv;
    if (!gmtime_r(&t, &tmv)) return -1;
    return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

static bool checkpoint_signature_hex(
    const char *sign_key,
    const char *payload,
    char sig_hex[MODEL_HASH_LEN * 2 + 1]
) {
    if (!sign_key || !payload || !sig_hex) return false;
    uint8_t sig[MODEL_HASH_LEN];
    const void *chunks[] = { sign_key, payload };
    const size_t sizes[] = { strlen(sign_key), strlen(payload) };
    if (!sha256_hash_chunks(chunks, sizes, 2, sig)) return false;
    hex_encode(sig, MODEL_HASH_LEN, sig_hex);
    return true;
}

static bool rotate_audit_if_needed(
    const char *audit_path,
    uint64_t max_bytes,
    const char *sign_key,
    AuditChainState *chain
) {
    if (!audit_path || !sign_key || !chain) return false;

    struct stat st;
    if (stat(audit_path, &st) != 0) {
        if (errno == ENOENT) return true;
        return false;
    }

    time_t now = time(NULL);
    int today = yyyymmdd_utc(now);
    int file_day = yyyymmdd_utc(st.st_mtime);

    bool need_rotate = ((uint64_t)st.st_size >= max_bytes) || (file_day != today);
    if (!need_rotate) return true;

    struct tm tmv;
    if (!gmtime_r(&now, &tmv)) return false;

    char rotated[AUDIT_MAX_PATH];
    int rn = snprintf(
        rotated, sizeof(rotated),
        "%s.%04d%02d%02d-%02d%02d%02d",
        audit_path,
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec
    );
    if (rn <= 0 || (size_t)rn >= sizeof(rotated)) return false;

    if (rename(audit_path, rotated) != 0) return false;

    FILE *nf = fopen(audit_path, "a");
    if (!nf) return false;

    uint8_t prev[MODEL_HASH_LEN];
    if (chain->initialized) memcpy(prev, chain->prev_hash, MODEL_HASH_LEN);
    else memset(prev, 0, MODEL_HASH_LEN);

    char prev_hex[MODEL_HASH_LEN * 2 + 1];
    hex_encode(prev, MODEL_HASH_LEN, prev_hex);

    char cp_payload[4096];
    int pn = snprintf(
        cp_payload, sizeof(cp_payload),
        "{\"ts\":%" PRIu64 ",\"type\":\"CHECKPOINT\",\"rotated_from\":\"%s\",\"prev_hash\":\"%s\"}",
        (uint64_t)now, rotated, prev_hex
    );
    if (pn <= 0 || (size_t)pn >= sizeof(cp_payload)) {
        fclose(nf);
        return false;
    }

    char sig_hex[MODEL_HASH_LEN * 2 + 1];
    if (!checkpoint_signature_hex(sign_key, cp_payload, sig_hex)) {
        fclose(nf);
        return false;
    }

    char event_line[6144];
    int en = snprintf(
        event_line, sizeof(event_line),
        "{\"ts\":%" PRIu64 ",\"type\":\"CHECKPOINT\",\"rotated_from\":\"%s\",\"prev_hash\":\"%s\",\"checkpoint_sig\":\"%s\"",
        (uint64_t)now, rotated, prev_hex, sig_hex
    );
    if (en <= 0 || (size_t)en >= sizeof(event_line)) {
        fclose(nf);
        return false;
    }

    uint8_t curr[MODEL_HASH_LEN];
    const void *chunks[] = { prev, event_line };
    const size_t sizes[] = { MODEL_HASH_LEN, (size_t)en };
    if (!sha256_hash_chunks(chunks, sizes, 2, curr)) {
        fclose(nf);
        return false;
    }

    char curr_hex[MODEL_HASH_LEN * 2 + 1];
    hex_encode(curr, MODEL_HASH_LEN, curr_hex);

    fprintf(nf, "%s,\"curr_hash\":\"%s\"}\n", event_line, curr_hex);
    fclose(nf);

    memcpy(chain->prev_hash, curr, MODEL_HASH_LEN);
    chain->initialized = true;
    return true;
}

static bool audit_load_last_curr_hash(const char *audit_path, AuditChainState *st) {
    if (!audit_path || !st) return false;

    memset(st->prev_hash, 0, MODEL_HASH_LEN);
    st->initialized = false;

    FILE *f = fopen(audit_path, "r");
    if (!f) {
        if (errno == ENOENT) return true;
        return false;
    }

    const char *marker = "\"curr_hash\":\"";
    char line[4096];
    char last_hex[MODEL_HASH_LEN * 2 + 1];
    bool found = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, marker);
        if (!p) continue;

        p += strlen(marker);

        bool ok = true;
        for (size_t i = 0; i < MODEL_HASH_LEN * 2; i++) {
            uint8_t nib = 0;
            if (!hex_nibble(p[i], &nib)) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        if (p[MODEL_HASH_LEN * 2] != '"') continue;

        memcpy(last_hex, p, MODEL_HASH_LEN * 2);
        last_hex[MODEL_HASH_LEN * 2] = '\0';
        found = true;
    }

    fclose(f);

    if (!found) return true;

    if (!hex_decode_32(last_hex, st->prev_hash)) return false;
    st->initialized = true;
    return true;
}

static bool audit_append_event(
    const char *audit_path,
    AuditChainState *st,
    const char *request_id,
    const char *subject_id,
    const ModelArtifact *a,
    DecisionResult dr
) {
    if (!audit_path || !st || !request_id || !subject_id || !a) return false;

    uint8_t prev[MODEL_HASH_LEN];
    if (!st->initialized) memset(prev, 0, sizeof(prev));
    else memcpy(prev, st->prev_hash, sizeof(prev));

    char prev_hex[MODEL_HASH_LEN * 2 + 1];
    hex_encode(prev, MODEL_HASH_LEN, prev_hex);

    char event_line[2048];
    int n = snprintf(
        event_line, sizeof(event_line),
        "{\"ts\":%" PRIu64 ",\"request_id\":\"%s\",\"subject_id\":\"%s\","
        "\"model_version\":%u,\"schema_hash32\":%u,\"threshold\":%.6f,"
        "\"abstain_low\":%.6f,\"abstain_high\":%.6f,\"proba\":%.6f,"
        "\"decision\":\"%s\",\"prev_hash\":\"%s\"",
        (uint64_t)time(NULL),
        request_id,
        subject_id,
        a->h.version,
        a->h.schema_hash32,
        a->h.decision_threshold,
        a->h.abstain_low,
        a->h.abstain_high,
        dr.proba,
        decision_class_str(dr.cls),
        prev_hex
    );
    if (n <= 0 || (size_t)n >= sizeof(event_line)) return false;

    uint8_t curr[MODEL_HASH_LEN];
    const void *chunks[] = { prev, event_line };
    const size_t sizes[] = { MODEL_HASH_LEN, (size_t)n };
    if (!sha256_hash_chunks(chunks, sizes, 2, curr)) return false;

    char curr_hex[MODEL_HASH_LEN * 2 + 1];
    hex_encode(curr, MODEL_HASH_LEN, curr_hex);

    FILE *f = fopen(audit_path, "a");
    if (!f) return false;
    fprintf(f, "%s,\"curr_hash\":\"%s\"}\n", event_line, curr_hex);
    fclose(f);

    memcpy(st->prev_hash, curr, MODEL_HASH_LEN);
    st->initialized = true;
    return true;
}

static bool parse_feature_line(const char *line_in, size_t expected, float *out_raw) {
    if (!line_in || !out_raw || expected == 0) return false;

    char *line = strdup(line_in);
    if (!line) return false;

    size_t count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(line, ",", &saveptr);
    while (tok) {
        while (*tok && isspace((unsigned char)*tok)) tok++;
        if (count >= expected) {
            free(line);
            return false;
        }

        float v = 0.0f;
        if (!parse_float_token(tok, &v)) {
            free(line);
            return false;
        }
        out_raw[count++] = v;
        tok = strtok_r(NULL, ",", &saveptr);
    }

    free(line);
    return count == expected;
}

static void apply_scaler_inplace(float *x, const ModelArtifact *a) {
    for (size_t j = 0; j < a->h.feature_count; j++) {
        float range = a->scaler_mx[j] - a->scaler_mn[j];
        if (range < 1e-12f) x[j] = 0.0f;
        else x[j] = (x[j] - a->scaler_mn[j]) / range;
    }
}

static bool featurize_raw_csv_line(const FeatureSchema *s, const char *line_in, float *x_out) {
    if (!s || !line_in || !x_out) return false;
    for (size_t i = 0; i < s->feature_count; i++) x_out[i] = 0.0f;

    char *line = strdup(line_in);
    if (!line) return false;

    size_t ln = strlen(line);
    while (ln > 0 && (line[ln - 1] == '\n' || line[ln - 1] == '\r')) line[--ln] = '\0';

    char *vals[2048];
    size_t nv = 0;
    if (!schema_parse_csv_fields(line, vals, 2048, &nv) || nv != s->raw_col_count) {
        free(line);
        return false;
    }

    for (size_t j = 0; j < s->raw_col_count; j++) {
        const SchemaColumn *c = &s->cols[j];
        const char *tok = vals[j];

        if (c->kind == SCHEMA_KIND_NUMERIC) {
            float v = 0.0f;
            if (!parse_float_token(tok, &v)) v = c->numeric_impute_value;
            if (c->numeric_feature_idx >= s->feature_count) { free(line); return false; }
            x_out[c->numeric_feature_idx] = v;
        } else {
            const char *cat = (tok && tok[0]) ? tok : "__MISSING__";
            size_t fidx = 0;
            if (!schema_find_category_feature(s, j, cat, &fidx)) {
                if (!schema_find_category_feature(s, j, "__UNK__", &fidx)) {
                    continue;
                }
            }
            if (fidx < s->feature_count) x_out[fidx] = 1.0f;
        }
    }

    free(line);
    return true;
}

static bool featurize_raw_json_line(const FeatureSchema *s, const char *json, float *x_out) {
    if (!s || !json || !x_out) return false;
    for (size_t i = 0; i < s->feature_count; i++) 
        x_out[i] = 0.0f;

    char val[4096];
    for (size_t j = 0; j < s->raw_col_count; j++) {
        const SchemaColumn *c = &s->cols[j];
        bool has = schema_json_get_flat_value(json, c->name, val, sizeof(val));

        if (c->kind == SCHEMA_KIND_NUMERIC) {
            float v = c->numeric_impute_value;
            if (has) {
                float p = 0.0f;
                if (parse_float_token(val, &p)) v = p;
            }
            if (c->numeric_feature_idx >= s->feature_count) return false;
            x_out[c->numeric_feature_idx] = v;
        } else {
            const char *cat = "__MISSING__";
            if (has && val[0] != '\0') cat = val;
            size_t fidx = 0;
            if (!schema_find_category_feature(s, j, cat, &fidx)) {
                if (!schema_find_category_feature(s, j, "__UNK__", &fidx)) continue;
            }
            if (fidx < s->feature_count) x_out[fidx] = 1.0f;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    bool use_raw_csv = false;
    bool use_raw_json = false;
    const char *schema_path = NULL;

    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("MobiusPL Scorer v%s\n", MOBIUS_VERSION);
            printf("Usage: mobius_score <model.bin> <request_id> <subject_id> [audit.jsonl] [options]\n");
            printf("  --version          Display version\n");
            printf("  --help             Display this message\n");
            printf("  --self-check FILE  Verify audit chain integrity\n");
            printf("  --schema PATH      Path to feature schema artifact\n");
            printf("  --raw-csv          Parse stdin as raw CSV\n");
            printf("  --raw-json         Parse stdin as raw JSON\n");
            return 0;
        }
    }

    if (argc >= 2 && strcmp(argv[1], "--self-check") == 0) {
        const char *audit_path = (argc >= 3) ? argv[2] : "decision_audit.jsonl";

        int lock_fd = audit_lock_exclusive(audit_path);
        if (lock_fd < 0) {
            fprintf(stderr, "failed to acquire audit lock\n");
            return 2;
        }

        uint8_t last_hash[MODEL_HASH_LEN];
        size_t lines = 0;
        bool ok = audit_verify_chain_file(audit_path, last_hash, &lines);
        audit_unlock_close(lock_fd);

        if (!ok) {
            fprintf(stderr, "self-check failed: invalid audit chain\n");
            return 1;
        }

        char hex[MODEL_HASH_LEN * 2 + 1];
        hex_encode(last_hash, MODEL_HASH_LEN, hex);
        printf("self-check OK lines=%zu last_hash=%s\n", lines, hex);
        return 0;
    }

    // Parse positional arguments and options
    const char *positionals[4] = {NULL, NULL, NULL, NULL};
    size_t positional_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--raw-csv") == 0) {
            use_raw_csv = true;
            continue;
        }
        if (strcmp(argv[i], "--raw-json") == 0) {
            use_raw_json = true;
            continue;
        }
        if (strcmp(argv[i], "--schema") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --schema requires a path argument\n");
                return 1;
            }
            schema_path = argv[++i];
            continue;
        }
        if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            return 1;
        }
        if (positional_count >= 4) {
            fprintf(stderr, "error: too many positional arguments\n");
            return 1;
        }
        positionals[positional_count++] = argv[i];
    }

    // Validate argument count
    if (positional_count < 3) {
        fprintf(stderr, "Usage: %s <model.bin> <request_id> <subject_id> [audit.jsonl]\n", argv[0]);
        fprintf(stderr, "       %s <model.bin> <request_id> <subject_id> [audit.jsonl] --schema <schema.art> --raw-csv\n", argv[0]);
        fprintf(stderr, "       %s <model.bin> <request_id> <subject_id> [audit.jsonl] --schema <schema.art> --raw-json\n", argv[0]);
        fprintf(stderr, "       %s --self-check [audit.jsonl]\n", argv[0]);
        fprintf(stderr, "Feature line (raw or engineered) is read from stdin.\n");
        return 1;
    }

    // Validate mutually exclusive flags
    if (use_raw_csv && use_raw_json) {
        fprintf(stderr, "error: cannot use both --raw-csv and --raw-json\n");
        return 1;
    }

    // Validate that raw modes have schema
    if ((use_raw_csv || use_raw_json) && !schema_path) {
        fprintf(stderr, "error: --raw-csv and --raw-json require --schema <path>\n");
        return 1;
    }

    // Validate that engineered mode doesn't have schema
    if (!use_raw_csv && !use_raw_json && schema_path) {
        fprintf(stderr, "error: --schema is only valid with --raw-csv or --raw-json\n");
        return 1;
    }

    const char *model_path = positionals[0];
    const char *request_id = positionals[1];
    const char *subject_id = positionals[2];
    const char *audit_path = (positional_count >= 4) ? positionals[3] : "decision_audit.jsonl";

    ModelArtifact a;
    if (!load_model_artifact(model_path, &a)) {
        fprintf(stderr, "load_model_artifact failed\n");
        return 1;
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), stdin)) {
        fprintf(stderr, "failed to read feature line from stdin\n");
        artifact_free(&a);
        return 1;
    }

    size_t n = (size_t)a.h.feature_count;
    float *x = (float *)malloc(n * sizeof(float));
    if (!x) {
        artifact_free(&a);
        return 1;
    }

    FeatureSchema schema;
    schema_init(&schema);

    if (use_raw_csv || use_raw_json) {
        if (!schema_path) {
            fprintf(stderr, "raw mode requires --schema <path>\n");
            free(x); 
            artifact_free(&a); 
            return 1;
        }
        if (!schema_load_artifact(schema_path, &schema)) {
            fprintf(stderr, "schema_load_artifact failed\n");
            free(x); 
            artifact_free(&a); 
            return 1;
        }
        if (schema.feature_count != a.h.feature_count) {
            fprintf(stderr, "schema/model feature_count mismatch\n");
            schema_free(&schema);
            free(x); 
            artifact_free(&a); 
            return 1;
        }

        bool ok = use_raw_csv
        ? featurize_raw_csv_line(&schema, line, x)
        : featurize_raw_json_line(&schema, line, x);
        schema_free(&schema);

        if (!ok) {
            fprintf(stderr, "raw featurization failed\n");
            free(x); 
            artifact_free(&a); 
            return 1;
        }

    } else {
        if (!parse_feature_line(line, n, x)) {
            fprintf(stderr, "feature parse failed: expected exactly %u floats\n", a.h.feature_count);
            free(x); 
            artifact_free(&a); 
            return 1;
        }
    }

    apply_scaler_inplace(x, &a);
    float p = predict_proba_scaled(&a, x);
    DecisionResult dr = policy_decide(p, a.h.abstain_low, a.h.abstain_high);
    printf("proba=%.6f decision=%s\n", dr.proba, decision_class_str(dr.cls));

    int lock_fd = audit_lock_exclusive(audit_path);
    if (lock_fd < 0) {
        fprintf(stderr, "failed to acquire audit lock\n");
        free(x);
        artifact_free(&a);
        return 1;
    }

    AuditChainState chain = {0};
    if (!audit_load_last_curr_hash(audit_path, &chain)) {
        fprintf(stderr, "audit chain bootstrap failed\n");
        audit_unlock_close(lock_fd);
        free(x);
        artifact_free(&a);
        return 1;
    }

    uint8_t verified_last[MODEL_HASH_LEN];
    size_t verified_lines = 0;
    if (!audit_verify_chain_file(audit_path, verified_last, &verified_lines)) {
        fprintf(stderr, "audit self-check failed: chain invalid\n");
        audit_unlock_close(lock_fd);
        free(x);
        artifact_free(&a);
        return 1;
    }
    memcpy(chain.prev_hash, verified_last, MODEL_HASH_LEN);
    chain.initialized = (verified_lines > 0);

    const char *sign_key = getenv("MOBIUS_AUDIT_SIGN_KEY");
    if (!sign_key || sign_key[0] == '\0') {
        fprintf(stderr, "MOBIUS_AUDIT_SIGN_KEY is required for checkpoint signing\n");
        audit_unlock_close(lock_fd);
        free(x);
        artifact_free(&a);
        return 1;
    }

    uint64_t max_bytes = AUDIT_DEFAULT_MAX_BYTES;
    const char *max_env = getenv("MOBIUS_AUDIT_MAX_BYTES");
    if (max_env && max_env[0] != '\0') {
        char *end = NULL;
        unsigned long long v = strtoull(max_env, &end, 10);
        if (end != max_env && *end == '\0' && v > 0ULL) max_bytes = (uint64_t)v;
    }

    if (!rotate_audit_if_needed(audit_path, max_bytes, sign_key, &chain)) {
        fprintf(stderr, "audit rotate failed\n");
        audit_unlock_close(lock_fd);
        free(x);
        artifact_free(&a);
        return 1;
    }

    if (!audit_append_event(audit_path, &chain, request_id, subject_id, &a, dr)) {
        fprintf(stderr, "audit append failed\n");
        audit_unlock_close(lock_fd);
        free(x);
        artifact_free(&a);
        return 1;
    }

    audit_unlock_close(lock_fd);
    free(x);
    artifact_free(&a);
    return 0;
}