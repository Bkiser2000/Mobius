#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string.h>
#include <float.h>
#include <time.h>
#include "../include/queue.h"
#include "../include/hash_map.h"
#include "../include/matrix.h"
#include "../include/tensor.h"
#include "../include/vector.h"
#include "../include/sort.h"
#include "../include/feature_schema_artifact.h"

#ifndef MOBIUS_VERSION
#define MOBIUS_VERSION "0.0.1"
#endif

#ifndef MOBIUS_BUILD_DATE
#define MOBIUS_BUILD_DATE "11.05.2026"
#endif

#ifndef MOBIUS_COMMIT
#define MOBIUS_COMMIT "Production"
#endif

#undef queue_init
#undef queue_push
#undef queue_pop
#undef queue_free
#undef queue_peek
#undef queue_size
#undef queue_empty
#undef queue_clear

// Core Pipeline State
typedef struct {
    char *line;
} RawRecord;

typedef struct {
    Matrix x;
    Matrix y;
} Dataset;

typedef enum {
    COL_UNKNOWN = 0,
    COL_NUMERIC = 1,
    COL_CATEGORICAL = 2
} ColumnKind;

typedef struct {
    HashMap category_maps;
    size_t feature_count;
    size_t raw_col_count;
    ColumnKind *col_kinds;
    size_t *num_feat_idx;
} FeatureState;

typedef struct {
    Queue ingest_q;
    FeatureState fs;
    Dataset ds;
    float *x_buf;
    float *y_buf;
    size_t rows;
    size_t cap_rows;
} Pipeline;

typedef struct {
    float *w;
    size_t n;
    float b;
} LogisticModel;

static char *dup_cstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

static void print_version(void) {
    printf("MobiusPL Trainer v%s\n", MOBIUS_VERSION);
    printf("Build Date: %s\n", MOBIUS_BUILD_DATE);
    printf("Commit: %s\n", MOBIUS_COMMIT);
}

static void print_usage(void) {
    printf("MobiusPL Trainer v%s\n", MOBIUS_VERSION);
    printf("Usage: mobius_pl <training_csv> [epochs] [learning_rate]\n");
    printf("  training_csv    - Path to CSV file with header row\n");
    printf("  epochs          - SGD epochs (default: 2000)\n");
    printf("  learning_rate   - SGD learning rate (default: 0.01)\n");
    printf("\nOptions:\n");
    printf("  --version       - Display version information\n");
    printf("  --help          - Display this message\n");
}

static void trim_inplace(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) start++;
    size_t end = len;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;
    if (start > 0) memmove(s, s + start, end - start);
    s[end - start] = '\0';
}

static bool binarize_labels_inplace(Matrix *y) {
    if (!y || !y->data || y->elem_size != sizeof(float)) return false;
    if (y->cols != 1) return false;

    float *yd = (float *)y->data;
    for (size_t i = 0; i < y->rows; i++) {
    	yd[i] = (yd[i] >= 0.5f) ? 1.0f : 0.0f;
    }
    return true;
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

static bool raw_record_copy(void *dst_elem, const void *src_elem, void *ctx) {
    (void)ctx;
    const RawRecord *src = (const RawRecord *)src_elem;
    RawRecord *dst = (RawRecord *)dst_elem;
    dst->line = dup_cstr(src->line ? src->line :"");
    return dst->line != NULL;
}

static void raw_record_dtor(void *elem, void *ctx) {
    (void)ctx;
    RawRecord *r = (RawRecord *)elem;
    free(r->line);
    r->line = NULL;
}

static bool fs_init_schema(FeatureState *fs, size_t raw_cols) {
    if (!fs || raw_cols == 0) return false;
    if (fs->raw_col_count != 0) return fs->raw_col_count == raw_cols;

    fs->col_kinds = (ColumnKind *)calloc(raw_cols, sizeof(ColumnKind));
    fs->num_feat_idx = (size_t *)malloc(raw_cols * sizeof(size_t));
    if (!fs->col_kinds || !fs->num_feat_idx) {
        free(fs->col_kinds);
        free(fs->num_feat_idx);
        fs->col_kinds = NULL;
        fs->num_feat_idx = NULL;
        return false;
    }

    for (size_t i = 0; i < raw_cols; i++) fs->num_feat_idx[i] = SIZE_MAX;
    fs->raw_col_count = raw_cols;
    return true;
}

static bool grow_feature_space(Pipeline *p, size_t new_feature_count) {
    if (!p) return false;
    if (new_feature_count <= p->fs.feature_count) return true;

    size_t oldf = p->fs.feature_count;
    size_t newf = new_feature_count;

    if (p->cap_rows == 0) {
        p->fs.feature_count = newf;
        return true;
    }

    if (p->cap_rows > SIZE_MAX / newf) return false;
    float *new_x = (float *)calloc(p->cap_rows * newf, sizeof(float));
    if (!new_x) return false;

    if (p->x_buf && oldf > 0) {
        for (size_t r = 0; r < p->rows; r++) {
            memcpy(new_x + r * newf, p->x_buf + r * oldf, oldf * sizeof(float));
        }
        free(p->x_buf);
    }

    p->x_buf = new_x;
    p->fs.feature_count = newf;
    return true;
}

static bool reserve_rows(Pipeline *p, size_t wanted_rows) {
    if (!p) return false;
    if (wanted_rows <= p->cap_rows) return true;

    size_t new_cap = p->cap_rows ? p->cap_rows : 64;
    while (new_cap < wanted_rows) {
        if (new_cap > (SIZE_MAX / 2)) return false;
        new_cap *= 2;
    }

    if (p->fs.feature_count == 0) return false;
    if (new_cap > SIZE_MAX / p->fs.feature_count) return false;

    size_t x_count = new_cap * p->fs.feature_count;
    float *new_x = (float *)calloc(x_count, sizeof(float));
    if (!new_x) return false;

    if (p->x_buf && p->rows > 0) {
        for (size_t r = 0; r < p->rows; r++) {
            memcpy(new_x + r * p->fs.feature_count,
                   p->x_buf + r * p->fs.feature_count,
                   p->fs.feature_count * sizeof(float));
        }
        free(p->x_buf);
    }
    p->x_buf = new_x;

    float *new_y = (float *)realloc(p->y_buf, new_cap * sizeof(float));
    if (!new_y) return false;
    p->y_buf = new_y;
    p->cap_rows = new_cap;
    return true;
}

static bool ensure_row_tmp(float **row_tmp, size_t *row_cap, size_t needed) {
    if (needed <= *row_cap) return true;
    size_t ncap = (*row_cap == 0) ? 8 : *row_cap;
    while (ncap < needed) {
        if (ncap > SIZE_MAX / 2) return false;
        ncap *= 2;
    }
    float *nt = (float *)realloc(*row_tmp, ncap * sizeof(float));
    if (!nt) return false;
    for (size_t i = *row_cap; i < ncap; i++) nt[i] = 0.0f;
    *row_tmp = nt;
    *row_cap = ncap;
    return true;
}

static bool get_or_create_numeric_feature(Pipeline *p, size_t col_idx, size_t *out_idx) {
    if (!p || !out_idx || col_idx >= p->fs.raw_col_count) return false;
    if (p->fs.num_feat_idx[col_idx] != SIZE_MAX) {
        *out_idx = p->fs.num_feat_idx[col_idx];
        return true;
    }

    int need = snprintf(NULL, 0, "N:%zu", col_idx);
    if (need < 0) return false;
    char *key = (char *)malloc((size_t)need + 1);
    if (!key) return false;
    snprintf(key, (size_t)need + 1, "N:%zu", col_idx);

    const char *lookup = key;
    const size_t *existing = (const size_t *)hm_get(&p->fs.category_maps, &lookup);
    if (existing) {
        p->fs.num_feat_idx[col_idx] = *existing;
        *out_idx = *existing;
        free(key);
        return true;
    }

    size_t idx = p->fs.feature_count;
    if (!grow_feature_space(p, idx + 1)) {
        free(key);
        return false;
    }

    if (!hm_put(&p->fs.category_maps, &lookup, &idx)) {
        free(key);
        return false;
    }

    p->fs.num_feat_idx[col_idx] = idx;
    *out_idx = idx;
    free(key);
    return true;
}

static bool get_or_create_onehot_feature(Pipeline *p, size_t col_idx, const char *token, size_t *out_idx) {
    if (!p || !token || !out_idx) return false;

    int need = snprintf(NULL, 0, "C:%zu:%s", col_idx, token);
    if (need < 0) return false;

    char *key = (char *)malloc((size_t)need + 1);
    if (!key) return false;
    snprintf(key, (size_t)need + 1, "C:%zu:%s", col_idx, token);

    const char *lookup = key;
    const size_t *existing = (const size_t *)hm_get(&p->fs.category_maps, &lookup);
    if (existing) {
        *out_idx = *existing;
        free(key);
        return true;
    }

    size_t idx = p->fs.feature_count;
    if (!grow_feature_space(p, idx + 1)) {
        free(key);
        return false;
    }

    if (!hm_put(&p->fs.category_maps, &lookup, &idx)) {
        free(key);
        return false;
    }

    *out_idx = idx;
    free(key);
    return true;
}

static bool parse_and_append_row(Pipeline *p, char *line_mutable) {
    if (!p || !line_mutable) return false;

    size_t ln = strlen(line_mutable);
    while (ln > 0 && (line_mutable[ln - 1] == '\n' || line_mutable[ln - 1] == '\r')) {
        line_mutable[--ln] = '\0';
    }
    if (ln == 0) return true;

    size_t cap = 16, count = 0;
    char **tokens = (char **)malloc(cap * sizeof(char *));
    if (!tokens) return false;

    char *saveptr = NULL;
    char *tok = strtok_r(line_mutable, ",", &saveptr);
    while (tok) {
        if (count == cap) {
            size_t ncap = cap * 2;
            char **nt = (char **)realloc(tokens, ncap * sizeof(char *));
            if (!nt) {
                free(tokens);
                return false;
            }
            tokens = nt;
            cap = ncap;
        }
        trim_inplace(tok);
        tokens[count++] = tok;
        tok = strtok_r(NULL, ",", &saveptr);
    }

    if (count < 2) {
        free(tokens);
        return false;
    }

    size_t raw_feats = count - 1;
    if (!fs_init_schema(&p->fs, raw_feats)) {
        free(tokens);
        return false;
    }
    if (p->fs.raw_col_count != raw_feats) {
        free(tokens);
        return false;
    }

    float *row_tmp = NULL;
    size_t row_cap = 0;
    if (!ensure_row_tmp(&row_tmp, &row_cap, p->fs.feature_count ? p->fs.feature_count : 8)) {
        free(tokens);
        return false;
    }

    for (size_t j = 0; j < raw_feats; j++) {
        float v = 0.0f;
        bool is_num = parse_float_token(tokens[j], &v);

        if (p->fs.col_kinds[j] == COL_UNKNOWN) {
            p->fs.col_kinds[j] = is_num ? COL_NUMERIC : COL_CATEGORICAL;
        }

        if (p->fs.col_kinds[j] == COL_NUMERIC) {
            size_t fidx = SIZE_MAX;
            if (!get_or_create_numeric_feature(p, j, &fidx)) {
                free(row_tmp);
                free(tokens);
                return false;
            }
            if (!ensure_row_tmp(&row_tmp, &row_cap, p->fs.feature_count)) {
                free(row_tmp);
                free(tokens);
                return false;
            }
            row_tmp[fidx] = is_num ? v : 0.0f;
        } else {
            size_t fidx = SIZE_MAX;
            if (!get_or_create_onehot_feature(p, j, tokens[j], &fidx)) {
                free(row_tmp);
                free(tokens);
                return false;
            }
            if (!ensure_row_tmp(&row_tmp, &row_cap, p->fs.feature_count)) {
                free(row_tmp);
                free(tokens);
                return false;
            }
            row_tmp[fidx] = 1.0f;
        }
    }

    if (!reserve_rows(p, p->rows + 1)) {
        free(row_tmp);
        free(tokens);
        return false;
    }

    float *row_ptr = p->x_buf + (p->rows * p->fs.feature_count);
    memcpy(row_ptr, row_tmp, p->fs.feature_count * sizeof(float));

    float yv = 0.0f;
    if (!parse_float_token(tokens[count - 1], &yv)) {
        yv = 0.0f;
    }
    p->y_buf[p->rows] = yv;
    p->rows++;

    free(row_tmp);
    free(tokens);
    return true;
}

// Initialization
bool pipeline_init(Pipeline *p) {
    if (!p) return false;
    memset(p, 0, sizeof(*p));

    // Queue of RawRecord struct
    if (!queue_init(&p->ingest_q, sizeof(RawRecord), raw_record_copy, NULL, raw_record_dtor, NULL)) {
	return false;
    }

    // String-Int maps
    if (!hm_init(&p->fs.category_maps,
		 sizeof(char *), sizeof(size_t), 256,
		 hm_hash_cstr_key, hm_eq_cstr_key,
		 hm_copy_cstr, NULL, hm_dtor_cstr, NULL, NULL)) {
	
	queue_free(&p->ingest_q);
	return false;
    }
    p->fs.feature_count = 0;
    p->fs.raw_col_count = 0;
    p->fs.col_kinds = NULL;
    p->fs.num_feat_idx = NULL;
    matrix_reset(&p->ds.x);
    matrix_reset(&p->ds.y);
    return true;
}

// Ingestion stage
bool pipeline_enqueue_csv_line(Pipeline *p, const char *line) {
    if (!p || !line) return false;
    RawRecord r = { (char*)line };
    return queue_push(&p->ingest_q, &r);
}

// Parse + Feature build stage
bool pipeline_process_one(Pipeline *p) {
    if (!p) return false;
    RawRecord r = {0};
    if (!queue_pop(&p->ingest_q, &r)) return false;

    // parse r.line, transform fields, update hash maps
    bool ok = parse_and_append_row(p, r.line);
    free(r.line);
    return ok;
}

bool pipeline_process_all(Pipeline *p) {
    if (!p) return false;
    while (!queue_is_empty(&p->ingest_q)) {
	if (!pipeline_process_one(p)) return false;
    }
    return true;
}

// Materialize dataset matrices
bool pipeline_build_dataset(Pipeline *p) {
    if (!p || p->rows == 0 || p->fs.feature_count == 0) return false;

    matrix_free(&p->ds.x);
    matrix_free(&p->ds.y);

    if (!matrix_init(&p->ds.x, p->rows, p->fs.feature_count, sizeof(float), NULL, NULL)) return false;
    if (!matrix_init(&p->ds.y, p->rows, 1, sizeof(float), NULL, NULL)) {
	matrix_free(&p->ds.x);
        return false;
    }
    memcpy(p->ds.x.data, p->x_buf, p->rows * p->fs.feature_count * sizeof(float));
    memcpy(p->ds.y.data, p->y_buf, p->rows * sizeof(float));
    return true;
}

// Mini-Batch tensor stage
Tensor *make_batch_tensor(const Matrix *x, size_t start, size_t batch_size) {
    if (!x || !x->data || x->elem_size != sizeof(float) || start >= x->rows) return NULL;

    size_t end = start + batch_size;
    if (end > x->rows) end = x->rows;
    size_t actual = end - start;

    size_t shape[2] = {actual, x->cols};
    Tensor *t = tensor_create(2, shape, TENSOR_F32);
    if (!t) return NULL;

    float *dst = (float *)t->data;
    const float *src = (const float*)x->data;

    for (size_t r = 0; r < actual; r++) {
	const float *src_row = src + (start + r) * x->cols;
	float *dst_row = dst + r * x->cols;
 	memcpy(dst_row, src_row, x->cols * sizeof(float));
    }
    return t;
}

static float sigmoidf_safe(float z) {
    if (z > 20.0f) return 1.0f;
    if (z < -20.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-z));
}

bool model_init(LogisticModel *m, size_t n_features) {
    if (!m || n_features == 0) return false;
    m->w = (float *)calloc(n_features, sizeof(float));
    if (!m->w) return false;
    m->n = n_features;
    m->b = 0.0f;
    return true;
}

void model_free(LogisticModel *m) {
    if (!m) return;
    free(m->w);
    m->w = NULL;
    m->n = 0;
    m->b = 0.0f;
}

float model_predict_proba_row(const LogisticModel *m, const float *x_row) {
    float z = m->b;
    for (size_t j = 0; j < m->n; j++) z += m->w[j] * x_row[j];
    return sigmoidf_safe(z);
}

static float binary_cross_entropy(const LogisticModel *m, const Matrix *x, const Matrix *y) {
    const float *xdat = (const float *)x->data;
    const float *ydat = (const float *)y->data;
    size_t rows = x->rows, cols = x->cols;
    float sum = 0.0f;

    for (size_t i = 0; i < rows; i++) {
    	const float *xr = xdat + i * cols;
    	float p = model_predict_proba_row(m, xr);
    	if (p < 1e-7f) p = 1e-7f;
    	if (p > 1.0f - 1e-7f) p = 1.0f - 1e-7f;
    	float yv = ydat[i];
    	sum += -(yv * logf(p) + (1.0f - yv) * logf(1.0f - p));
    }
    return sum / (float)rows;
}

static void fisher_yates_shuffle(size_t *idx, size_t n) {
    if (!idx || n < 2) return;
    for (size_t i = n - 1; i > 0; i--) {
	size_t j = (size_t) (rand() % (int)(i + 1));
	size_t t = idx[i];
	idx[i] = idx[j];
	idx[j] = t;
    }
}

bool train_logreg(LogisticModel *m, const Matrix *x, const Matrix *y, size_t epochs, float lr) {
    if (!m || !x || !y || !x->data || !y->data) return false;
    if (x->elem_size != sizeof(float) || y->elem_size != sizeof(float)) return false;
    if (x->rows != y->rows || x->cols != m->n) return false;

    const size_t rows = x->rows;
    const size_t cols = x->cols;
    const float *xdat = (const float *)x->data;
    const float *ydat = (const float *)y->data;

    float *grad_w = (float *)malloc(cols * sizeof(float));
    size_t *idx = (size_t *)malloc(rows * sizeof(size_t));
    if (!grad_w || !idx) {
    	free(grad_w);
    	free(idx);
    	return false;
    }

    for (size_t i = 0; i < rows; i++) idx[i] = i;

    srand((unsigned)time(NULL));

    for (size_t e = 0; e < epochs; e++) {
    	fisher_yates_shuffle(idx, rows);
    	memset(grad_w, 0, cols * sizeof(float));
    	float grad_b = 0.0f;

    for (size_t k = 0; k < rows; k++) {
        size_t i = idx[k];
        const float *x_row = xdat + i * cols;
        float yi = ydat[i];
        float p = model_predict_proba_row(m, x_row);
        float err = p - yi;

        for (size_t j = 0; j < cols; j++) grad_w[j] += err * x_row[j];
        grad_b += err;
    }

    float inv_n = 1.0f / (float)rows;
    for (size_t j = 0; j < cols; j++) m->w[j] -= lr * (grad_w[j] * inv_n);
    m->b -= lr * (grad_b * inv_n);

    if ((e + 1) % 100 == 0 || e == 0 || e + 1 == epochs) {
        float loss = binary_cross_entropy(m, x, y);
        printf("epoch=%zu loss=%.6f\n", e + 1, loss);
    }
   }
    free(grad_w);
    free(idx);
    return true;
}

float eval_accuracy(const LogisticModel *m, const Matrix *x, const Matrix *y) {
    if (!m || !x || !y || !x->data || !y->data) return 0.0f;
    if (x->rows == 0 || x->rows != y->rows || x->cols != m->n) return 0.0f;

    const float *xdat = (const float *)x->data;
    const float *ydat = (const float *)y->data;
    size_t correct = 0;

    for (size_t i = 0; i < x->rows; i++) {
	const float *x_row = xdat + i * x->cols;
	float p = model_predict_proba_row(m, x_row);
	int pred = (p >= 0.5f) ? 1 : 0;
	int truth = (ydat[i] >= 0.5f) ? 1 : 0;
	if (pred == truth) correct++;
    }

    return (float)correct / (float)x->rows;
}

typedef struct {
    size_t tp, tn, fp, fn;
    float accuracy;
    float precision;
    float recall;
    float f1;
} BinaryMetrics;

static void shuffle_indices(size_t *idx, size_t n, unsigned seed) {
    if (!idx || n < 2) return;
    srand(seed);
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)(rand() % (int)(i + 1));
        size_t t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
    }
}

static bool split_dataset(const Matrix *x, const Matrix *y, float test_ratio, unsigned seed, Matrix *x_train, Matrix *y_train, Matrix *x_test, Matrix *y_test) {
    if (!x || !y || !x->data || !y->data) return false;
    if (x->elem_size != sizeof(float) || y->elem_size != sizeof(float)) return false;
    if (x->rows != y->rows || y->cols != 1 || x->rows < 2) return false;
    if (test_ratio <= 0.0f || test_ratio >= 1.0f) return false;

    matrix_reset(x_train); matrix_reset(y_train);
    matrix_reset(x_test);  matrix_reset(y_test);

    const size_t rows = x->rows;
    const size_t cols = x->cols;
    const size_t test_n = (size_t)(rows * test_ratio);
    const size_t train_n = rows - test_n;
    if (train_n == 0 || test_n == 0) return false;

    size_t *idx = (size_t *)malloc(rows * sizeof(size_t));
    if (!idx) return false;
    for (size_t i = 0; i < rows; i++) idx[i] = i;
    shuffle_indices(idx, rows, seed);

    if (!matrix_init(x_train, train_n, cols, sizeof(float), NULL, NULL) ||
        !matrix_init(y_train, train_n, 1, sizeof(float), NULL, NULL) ||
        !matrix_init(x_test,  test_n,  cols, sizeof(float), NULL, NULL) ||
        !matrix_init(y_test,  test_n,  1, sizeof(float), NULL, NULL)) {
        free(idx);
        matrix_free(x_train); matrix_free(y_train);
        matrix_free(x_test);  matrix_free(y_test);
        return false;
    }

    const float *xdat = (const float *)x->data;
    const float *ydat = (const float *)y->data;
    float *xtr = (float *)x_train->data;
    float *ytr = (float *)y_train->data;
    float *xte = (float *)x_test->data;
    float *yte = (float *)y_test->data;

    for (size_t i = 0; i < train_n; i++) {
        size_t src = idx[i];
        memcpy(xtr + i * cols, xdat + src * cols, cols * sizeof(float));
        ytr[i] = ydat[src];
    }
    for (size_t i = 0; i < test_n; i++) {
        size_t src = idx[train_n + i];
        memcpy(xte + i * cols, xdat + src * cols, cols * sizeof(float));
        yte[i] = ydat[src];
    }

    free(idx);
    return true;
}

static BinaryMetrics eval_binary_metrics(const LogisticModel *m, const Matrix *x, const Matrix *y, float threshold) {
    BinaryMetrics out = {0};
    if (!m || !x || !y || !x->data || !y->data) return out;
    if (x->rows == 0 || x->rows != y->rows || x->cols != m->n) return out;

    const float *xdat = (const float *)x->data;
    const float *ydat = (const float *)y->data;

    for (size_t i = 0; i < x->rows; i++) {
        const float *x_row = xdat + i * x->cols;
        float p = model_predict_proba_row(m, x_row);
        int pred = (p >= threshold) ? 1 : 0;
        int truth = (ydat[i] >= 0.5f) ? 1 : 0;

        if (pred == 1 && truth == 1) out.tp++;
        else if (pred == 0 && truth == 0) out.tn++;
        else if (pred == 1 && truth == 0) out.fp++;
        else out.fn++;
    }

    float total = (float)(out.tp + out.tn + out.fp + out.fn);
    out.accuracy = (total > 0.0f) ? (float)(out.tp + out.tn) / total : 0.0f;

    float pred_pos = (float)(out.tp + out.fp);
    float actual_pos = (float)(out.tp + out.fn);
    out.precision = (pred_pos > 0.0f) ? (float)out.tp / pred_pos : 0.0f;
    out.recall    = (actual_pos > 0.0f) ? (float)out.tp / actual_pos : 0.0f;

    float denom = out.precision + out.recall;
    out.f1 = (denom > 0.0f) ? (2.0f * out.precision * out.recall) / denom : 0.0f;
    return out;
}

static void print_binary_metrics(const char *name, BinaryMetrics m) {
    printf("%s metrics:\n", name);
    printf("  accuracy : %.4f\n", m.accuracy);
    printf("  precision: %.4f\n", m.precision);
    printf("  recall   : %.4f\n", m.recall);
    printf("  f1       : %.4f\n", m.f1);
    printf("  confusion matrix (rows=true, cols=pred):\n");
    printf("    TN=%zu  FP=%zu\n", m.tn, m.fp);
    printf("    FN=%zu  TP=%zu\n", m.fn, m.tp);
}

typedef struct {
    size_t cols;
    float *mn;
    float *mx;
} MinMaxScaler;

static void scaler_free(MinMaxScaler *s) {
    if (!s) return;
    free(s->mn);
    free(s->mx);
    s->mn = NULL;
    s->mx = NULL;
    s->cols = 0;
}

static bool scaler_fit_minmax(const Matrix *x, MinMaxScaler *s) {
    if (!x || !s || !x->data || x->elem_size != sizeof(float)) return false;
    if (x->rows == 0 || x->cols == 0) return false;

    s->cols = x->cols;
    s->mn = (float *)malloc(s->cols * sizeof(float));
    s->mx = (float *)malloc(s->cols * sizeof(float));
    if (!s->mn || !s->mx) {
        scaler_free(s);
        return false;
    }

    const float *d = (const float *)x->data;
    for (size_t j = 0; j < s->cols; j++) {
        float mn = FLT_MAX, mx = -FLT_MAX;
        for (size_t i = 0; i < x->rows; i++) {
            float v = d[i * s->cols + j];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        s->mn[j] = mn;
        s->mx[j] = mx;
    }
    return true;
}

static bool scaler_apply_minmax(Matrix *x, const MinMaxScaler *s) {
    if (!x || !s || !x->data || !s->mn || !s->mx) return false;
    if (x->elem_size != sizeof(float) || x->cols != s->cols) return false;

    float *d = (float *)x->data;
    for (size_t j = 0; j < s->cols; j++) {
        float range = s->mx[j] - s->mn[j];
        if (range < 1e-12f) {
            for (size_t i = 0; i < x->rows; i++) d[i * s->cols + j] = 0.0f;
        } else {
            for (size_t i = 0; i < x->rows; i++) {
                float *v = &d[i * s->cols + j];
                *v = (*v - s->mn[j]) / range;
            }
        }
    }
    return true;
}

void pipeline_free(Pipeline *p) {
    if (!p) return;
    queue_free(&p->ingest_q);
    hm_free(&p->fs.category_maps);
    matrix_free(&p->ds.x);
    matrix_free(&p->ds.y);
    free(p->x_buf);
    free(p->y_buf);
    p->x_buf = NULL;
    p->y_buf = NULL;
    p->rows = 0;
    p->cap_rows = 0;
}

#define MODEL_MAGIC 0x4D4F4249u /* MOBI */
#define MODEL_VERSION 1u
#define MODEL_HASH_LEN 32u

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

static uint32_t fnv1a_u32(const void *data, size_t n, uint32_t seed) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = seed ? seed : 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* Current schema hash source:
   raw column count + inferred column kinds + expanded feature count.
*/

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

static uint32_t compute_schema_hash32(const FeatureState *fs) {
    if (!fs) return 0u;
    uint32_t h = 2166136261u;
    h = fnv1a_u32(&fs->raw_col_count, sizeof(fs->raw_col_count), h);
    h = fnv1a_u32(&fs->feature_count, sizeof(fs->feature_count), h);
    if (fs->col_kinds && fs->raw_col_count > 0) {
        h = fnv1a_u32(fs->col_kinds, fs->raw_col_count * sizeof(ColumnKind), h);
    }
    return h;
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

static bool artifact_payload_hash(const ModelHeader *h, const float *w, float b, const float *mn, const float *mx, uint8_t out[MODEL_HASH_LEN]) {
    if (!h || !w || !mn || !mx || !out) return false;

    size_t n = (size_t)h->feature_count;

    const void *chunks[] = {
        &h->magic,
        &h->version,
        &h->created_unix,
        &h->feature_count,
        &h->schema_hash32,
        &h->decision_threshold,
        &h->abstain_low,
        &h->abstain_high,
        w,
        &b,
        mn,
        mx
    };

    const size_t sizes[] = {
        sizeof(h->magic),
        sizeof(h->version),
        sizeof(h->created_unix),
        sizeof(h->feature_count),
        sizeof(h->schema_hash32),
        sizeof(h->decision_threshold),
        sizeof(h->abstain_low),
        sizeof(h->abstain_high),
        n * sizeof(float),
        sizeof(b),
        n * sizeof(float),
        n * sizeof(float)
    };

    return sha256_hash_chunks(chunks, sizes, sizeof(chunks) / sizeof(chunks[0]), out);
}

static bool save_model_artifact(
    const char *path,
    const LogisticModel *m,
    const MinMaxScaler *s,
    const FeatureState *fs,
    float decision_threshold,
    float abstain_low,
    float abstain_high
) {
    if (!path || !m || !s || !fs || !m->w || !s->mn || !s->mx) return false;
    if (m->n == 0 || s->cols != m->n) return false;
    if (!(abstain_low <= decision_threshold && decision_threshold <= abstain_high)) return false;

    ModelArtifact a;
    memset(&a, 0, sizeof(a));

    a.h.magic = MODEL_MAGIC;
    a.h.version = MODEL_VERSION;
    a.h.created_unix = (uint64_t)time(NULL);
    a.h.feature_count = (uint32_t)m->n;
    a.h.schema_hash32 = compute_schema_hash32(fs);
    a.h.decision_threshold = decision_threshold;
    a.h.abstain_low = abstain_low;
    a.h.abstain_high = abstain_high;

    if (!artifact_payload_hash(&a.h, m->w, m->b, s->mn, s->mx, a.h.payload_sha256)) {
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    bool ok = true;
    if (fwrite(&a.h, sizeof(a.h), 1, f) != 1) ok = false;
    if (ok && fwrite(m->w, sizeof(float), m->n, f) != m->n) ok = false;
    if (ok && fwrite(&m->b, sizeof(float), 1, f) != 1) ok = false;
    if (ok && fwrite(s->mn, sizeof(float), s->cols, f) != s->cols) ok = false;
    if (ok && fwrite(s->mx, sizeof(float), s->cols, f) != s->cols) ok = false;

    fclose(f);
    return ok;
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

    size_t n = out->h.feature_count;
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

typedef enum {
    DECISION_AUTO_NEG = 0,
    DECISION_MANUAL_REVIEW = 1,
    DECISION_AUTO_POS = 2
} DecisionClass;

typedef struct {
    float proba;
    DecisionClass cls;
} DecisionResult;

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

typedef struct {
    uint8_t prev_hash[MODEL_HASH_LEN];
    bool initialized;
} AuditChainState;

static void hex_encode(const uint8_t *in, size_t n, char *out_hex) {
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out_hex[i * 2] = hex[(in[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex[in[i] & 0xF];
    }
    out_hex[n * 2] = '\0';
}

static bool audit_append_event(const char *audit_path, AuditChainState *st, const char *request_id, const char *subject_id, const ModelArtifact *a, DecisionResult dr) {
    if (!audit_path || !st || !request_id || !subject_id || !a) return false;

    uint8_t prev[MODEL_HASH_LEN];
    if (!st->initialized) {
        memset(prev, 0, sizeof(prev));
    } else {
        memcpy(prev, st->prev_hash, sizeof(prev));
    }

    char prev_hex[MODEL_HASH_LEN * 2 + 1];
    hex_encode(prev, MODEL_HASH_LEN, prev_hex);

    char event_line[2048];
    int n = snprintf(
        event_line, sizeof(event_line),
        "{\"ts\":%" PRIu64 ",\"request_id\":\"%s\",\"subject_id\":\"%s\","
        "\"model_version\":%u,\"schema_hash32\":%u,\"threshold\":%.6f,"
        "\"abstain_low\":%.6f,\"abstain_high\":%.6f,\"proba\":%.6f,"
        "\"decision\":\"%s\",\"prev_hash\":\"%s\"}",
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
    SHA256_CTX ctx;
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

static bool lookup_feature_idx_for_numeric(const FeatureState *fs, size_t col_idx, size_t *out_idx) {
    if (!fs || !out_idx || col_idx >= fs->raw_col_count) return false;
    if (fs->num_feat_idx[col_idx] == SIZE_MAX) return false;
    *out_idx = fs->num_feat_idx[col_idx];
    return true;
}

static bool lookup_feature_idx_for_cat(const FeatureState *fs, size_t col_idx, const char *token, size_t *out_idx) {
    if (!fs || !token || !out_idx) return false;
    int need = snprintf(NULL, 0, "C:%zu:%s", col_idx, token);
    if (need < 0) return false;
    char *key = (char *)malloc((size_t)need + 1);
    if (!key) return false;
    snprintf(key, (size_t)need + 1, "C:%zu:%s", col_idx, token);
    const char *lookup = key;
    const size_t *p = (const size_t *)hm_get((HashMap *)&fs->category_maps, &lookup);
    free(key);
    if (!p) return false;
    *out_idx = *p;
    return true;
}

static bool build_and_save_schema_artifact_from_csv(
    const char *csv_path,
    const char *schema_path,
    const FeatureState *fs,
    const MinMaxScaler *scaler
) {
    if (!csv_path || !schema_path || !fs || !scaler) return false;
    FILE *fp = fopen(csv_path, "r");
    if (!fp) return false;

    char line[8192];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return false;
    }

    size_t ln = strlen(line);
    while (ln > 0 && (line[ln - 1] == '\n' || line[ln - 1] == '\r')) line[--ln] = '\0';

    char *hdr = strdup(line);
    if (!hdr) { fclose(fp); return false; }

    char *fields[2048];
    size_t nfields = 0;
    if (!schema_parse_csv_fields(hdr, fields, 2048, &nfields) || nfields < 2) {
        free(hdr);
        fclose(fp);
        return false;
    }

    size_t raw_cols = nfields - 1;
    if (raw_cols != fs->raw_col_count) {
        free(hdr);
        fclose(fp);
        return false;
    }

    FeatureSchema s;
    schema_init(&s);
    s.feature_count = fs->feature_count;

    for (size_t j = 0; j < raw_cols; j++) {
        SchemaKind kind = (fs->col_kinds[j] == COL_NUMERIC) ? SCHEMA_KIND_NUMERIC : SCHEMA_KIND_CATEGORICAL;
        if (!schema_add_column(&s, fields[j], kind)) {
            schema_free(&s);
            free(hdr);
            fclose(fp);
            return false;
        }
    }

    free(hdr);

    double *sum = (double *)calloc(raw_cols, sizeof(double));
    size_t *cnt = (size_t *)calloc(raw_cols, sizeof(size_t));
    if (!sum || !cnt) {
        free(sum); free(cnt);
        schema_free(&s);
        fclose(fp);
        return false;
    }

    while (fgets(line, sizeof(line), fp)) {
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = '\0';
        if (ll == 0) continue;

        char *row = strdup(line);
        if (!row) { free(sum); free(cnt); schema_free(&s); fclose(fp); return false; }

        char *vals[2048];
        size_t nv = 0;
        bool ok = schema_parse_csv_fields(row, vals, 2048, &nv);
        if (!ok || nv != raw_cols + 1) {
            free(row);
            continue;
        }

        for (size_t j = 0; j < raw_cols; j++) {
            if (s.cols[j].kind == SCHEMA_KIND_NUMERIC) {
                float v = 0.0f;
                if (parse_float_token(vals[j], &v)) {
                    sum[j] += (double)v;
                    cnt[j]++;
                }
            } else {
                const char *tok = vals[j][0] ? vals[j] : "__MISSING__";
                size_t fidx = 0;
                if (lookup_feature_idx_for_cat(fs, j, tok, &fidx)) {
                    schema_add_category(&s, j, tok, fidx);
                }
            }
        }

        free(row);
    }

    fclose(fp);

    for (size_t j = 0; j < raw_cols; j++) {
        if (s.cols[j].kind == SCHEMA_KIND_NUMERIC) {
            size_t fidx = SIZE_MAX;
            if (!lookup_feature_idx_for_numeric(fs, j, &fidx)) {
                free(sum); free(cnt); schema_free(&s); return false;
            }

            s.cols[j].missing_rule = SCHEMA_MISSING_MEAN;
            s.cols[j].numeric_feature_idx = fidx;
            s.cols[j].numeric_impute_value = (cnt[j] > 0) ? (float)(sum[j] / (double)cnt[j]) : 0.0f;
            s.cols[j].scaler_min = scaler->mn[fidx];
            s.cols[j].scaler_max = scaler->mx[fidx];
        } else {
            s.cols[j].missing_rule = SCHEMA_MISSING_TOKEN;
            size_t unk_idx = 0;
            if (!schema_find_category_feature(&s, j, "__UNK__", &unk_idx)) {
                if (lookup_feature_idx_for_cat(fs, j, "__UNK__", &unk_idx)) {
                    schema_add_category(&s, j, "__UNK__", unk_idx);
                }
            }
            size_t miss_idx = 0;
            if (!schema_find_category_feature(&s, j, "__MISSING__", &miss_idx)) {
                if (lookup_feature_idx_for_cat(fs, j, "__MISSING__", &miss_idx)) {
                    schema_add_category(&s, j, "__MISSING__", miss_idx);
                }
            }
        }
    }

    free(sum);
    free(cnt);

    bool saved = schema_save_artifact(schema_path, &s);
    schema_free(&s);
    return saved;
}

int main(int argc, char **argv) {    
    /* Check for help/version flags first */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    if (argc < 2) {
	fprintf(stderr, "Usage: %s <data.csv> [epochs] [lr]\n", argv[0]);
	return 1;
    }

    size_t epochs = (argc >= 3) ? (size_t)strtoul(argv[2], NULL, 10) : 2000;
    float lr = (argc >= 4) ? strtof(argv[3], NULL) : 0.01f;

    Pipeline p;
    if (!pipeline_init(&p)) {
	fprintf(stderr, "pipeline_init failed\n");
	return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
	perror("fopen");
	pipeline_free(&p);
	return 1;
    }

    char line[4096];
    bool seen_header = false;

    while(fgets(line, sizeof(line), fp)) {
        if (!seen_header) {
            seen_header = true;
            continue;
        }
    
	    if (!pipeline_enqueue_csv_line(&p, line)) {
	        fprintf(stderr, "enqueue failed\n");
	        fclose(fp);
	        pipeline_free(&p);
	        return 1;
	    }
    }

    if (!pipeline_process_all(&p)) {
    	fprintf(stderr, "processing failed\n");
    	pipeline_free(&p);
    	return 1;
    }

    if (!pipeline_build_dataset(&p)) {
	fprintf(stderr, "dataset build failed\n");
	pipeline_free(&p);
	return 1;
    }

    if (!binarize_labels_inplace(&p.ds.y)) {
        fprintf(stderr, "Label binarization failed\n");
        pipeline_free(&p);
        return 1;
    }

    Matrix x_train, y_train, x_test, y_test;
    matrix_reset(&x_train); 
    matrix_reset(&y_train);
    matrix_reset(&x_test);  
    matrix_reset(&y_test);

    if (!split_dataset(&p.ds.x, &p.ds.y, 0.20f, 42, &x_train, &y_train, &x_test, &y_test)) {
        fprintf(stderr, "train/test split failed\n");
        pipeline_free(&p);
        return 1;
    }

    MinMaxScaler scaler = {0};

    if (!scaler_fit_minmax(&x_train, &scaler)) {
        fprintf(stderr, "scaler fit failed\n");
        matrix_free(&x_train);
        matrix_free(&y_train);
        matrix_free(&x_test);
        matrix_free(&y_test);
        pipeline_free(&p);
        return 1;
    }

    if (!scaler_apply_minmax(&x_train, &scaler) || !scaler_apply_minmax(&x_test, &scaler)) {
        fprintf(stderr, "scaler apply failed\n");
        scaler_free(&scaler);
        matrix_free(&x_train);
        matrix_free(&y_train);
        matrix_free(&x_test);
        matrix_free(&y_test);
        pipeline_free(&p);
        return 1;
    }

    LogisticModel model;
    if (!model_init(&model, x_train.cols)) {
        fprintf(stderr, "model init failed\n");
        matrix_free(&x_train); 
        matrix_free(&y_train);
        matrix_free(&x_test);  
        matrix_free(&y_test);
        pipeline_free(&p);
        return 1;
    }

    if (!train_logreg(&model, &x_train, &y_train, epochs, lr)) {
        fprintf(stderr, "training failed\n");
        model_free(&model);
        matrix_free(&x_train); 
        matrix_free(&y_train);
        matrix_free(&x_test);  
        matrix_free(&y_test);
        pipeline_free(&p);
        return 1;
    }

    if (!save_model_artifact("model_v1.bin", &model, &scaler, &p.fs, 0.50f, 0.35f, 0.65f)) {
        fprintf(stderr, "save_model_artifact failed\n");
        return 1;
    }

    if (!build_and_save_schema_artifact_from_csv(argv[1], "feature_schema_v1.art", &p.fs, &scaler)) {
        fprintf(stderr, "feature schema artifact save failed\n");
        return 1;
    }

    BinaryMetrics train_m = eval_binary_metrics(&model, &x_train, &y_train, 0.5f);
    BinaryMetrics test_m  = eval_binary_metrics(&model, &x_test, &y_test, 0.5f);

    printf("Rows total: %zu, train: %zu, test: %zu, features: %zu\n",
        p.ds.x.rows, x_train.rows, x_test.rows, p.ds.x.cols);
    print_binary_metrics("Train", train_m);
    print_binary_metrics("Test", test_m);

    Tensor *batch0 = make_batch_tensor(&x_train, 0, 32);
    if (batch0) {
        printf("Batch tensor shape: [%zu, %zu]\n", batch0->shape[0], batch0->shape[1]);
        tensor_destroy(batch0);
    }

    model_free(&model);
    matrix_free(&x_train); 
    matrix_free(&y_train);
    matrix_free(&x_test);  
    matrix_free(&y_test);
    scaler_free(&scaler);
    pipeline_free(&p);
    return 0;
}


