#define _POSIX_C_SOURCE 200809L
#include "include/feature_schema_artifact.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_cstr_local(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}

static void trim_inplace_local(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    size_t b = 0;
    while (b < len && isspace((unsigned char)s[b])) b++;
    size_t e = len;
    while (e > b && isspace((unsigned char)s[e - 1])) e--;
    if (b > 0) memmove(s, s + b, e - b);
    s[e - b] = '\0';
}

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static char hex_digit(unsigned v) {
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

static bool pct_encode(const char *in, char *out, size_t out_sz) {
    if (!in || !out || out_sz == 0) return false;
    size_t k = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
	unsigned char c = (unsigned char)in[i];
	bool safe = (isalnum(c) || c == '_' || c == '-' || c == '.' || c == ' ');
	if (safe) {
	    if (k + 1 >= out_sz) return false;
	    out[k++] = (char)c;
	    } else {
	    if (k + 3 >= out_sz) return false;
	    out[k++] = '%';
	    out[k++] = hex_digit((c >> 4) & 0xF);
	    out[k++] = hex_digit(c & 0xF);
	}
    }
    if (k >= out_sz) return false;
    out[k] = '\0';
    return true;
}

static int from_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

static bool pct_decode(const char *in, char *out, size_t out_sz) {
    if (!in || !out || out_sz == 0) return false;
    size_t k = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
	if (in[i] == '%' && in[i + 1] && in[i + 2]) {
	    int hi = from_hex(in[i + 1]);
	    int lo = from_hex(in[i + 2]);
	    if (hi < 0 || lo < 0) return false;
	    unsigned char c = (unsigned char)((hi << 4) | lo);
	    if (k + 1 >= out_sz) return false;
	    out[k++] = (char)c;
	    i += 2;
	} else {
	    if (k + 1 >= out_sz) return false;
	    out[k++] = in[i];
	}
    }
    if (k >= out_sz) return false;
    out[k] = '\0';
    return true;
}

void schema_init(FeatureSchema *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->version = 1;
}

void schema_free(FeatureSchema *s) {
    if (!s) return;
    if (s->cols) {
	for (size_t i = 0; i < s->raw_col_count; i++) {
	     free(s->cols[i].name);
		if (s->cols[i].cats) {
		    for (size_t j = 0; j < s->cols[i].cat_count; j++) {
			free(s->cols[i].cats[j].token);
		    }
	        }
		free(s->cols[i].cats);
	}
    }
    free(s->cols);
    memset(s, 0, sizeof(*s));
}

bool schema_add_column(FeatureSchema *s, const char *name, SchemaKind kind) {
    if (!s || !name) return false;
    size_t n = s->raw_col_count;
    SchemaColumn *new_cols = (SchemaColumn *)realloc(s->cols, (n + 1) * sizeof(SchemaColumn));
    if (!new_cols) return false;
    s->cols = new_cols;
    memset(&s->cols[n], 0, sizeof(SchemaColumn));
    s->cols[n].name = dup_cstr_local(name);
    if (!s->cols[n].name) return false;
    s->cols[n].kind = kind;
    s->cols[n].missing_rule = (kind == SCHEMA_KIND_NUMERIC) ? SCHEMA_MISSING_MEAN : SCHEMA_MISSING_TOKEN;
    s->cols[n].numeric_feature_idx = (size_t)-1;
    s->raw_col_count++;
    return true;
}

bool schema_add_category(FeatureSchema *s, size_t col_idx, const char *token, size_t feature_idx) {
    if (!s || !token || col_idx >= s->raw_col_count) return false;
    SchemaColumn *c = &s->cols[col_idx];
    if (c->kind != SCHEMA_KIND_CATEGORICAL) return false;

    for (size_t i = 0; i < c->cat_count; i++) {
    	if (str_eq(c->cats[i].token, token)) return true;
    }

    if (c->cat_count == c->cat_cap) {
        size_t ncap = c->cat_cap ? c->cat_cap * 2 : 8;
        SchemaCategory *nc = (SchemaCategory *)realloc(c->cats, ncap * sizeof(SchemaCategory));
        if (!nc) return false;
        c->cats = nc;
        c->cat_cap = ncap;
    }

    c->cats[c->cat_count].token = dup_cstr_local(token);
    if (!c->cats[c->cat_count].token) return false;
    c->cats[c->cat_count].feature_idx = feature_idx;
    c->cat_count++;
    return true;
}

ssize_t schema_find_column_by_name(const FeatureSchema *s, const char *name) {
    if (!s || !name) return -1;
    for (size_t i = 0; i < s->raw_col_count; i++) {
	if (str_eq(s->cols[i].name, name)) return (ssize_t)i;
    }
    return -1;
}

bool schema_find_category_feature(const FeatureSchema *s, size_t col_idx, const char *token, size_t *out_feature_idx) {
    if (!s || !token || !out_feature_idx || col_idx >= s->raw_col_count) return false;
    const SchemaColumn *c = &s->cols[col_idx];
    if (c->kind != SCHEMA_KIND_CATEGORICAL) return false;
    for (size_t i = 0; i < c->cat_count; i++) {
	if (str_eq(c->cats[i].token, token)) {
	    *out_feature_idx = c->cats[i].feature_idx;
	    return true;
        }
    }
    return false;
}

bool schema_parse_csv_fields(char *line_mutable, char **out_fields, size_t max_fields, size_t *out_count) {
    if (!line_mutable || !out_fields || max_fields == 0 || !out_count) return false;
    *out_count = 0;
    char *save = NULL;
    char *tok = strtok_r(line_mutable, ",", &save);
    while (tok) {
	if (*out_count >= max_fields) return false;
	trim_inplace_local(tok);
	out_fields[(*out_count)++] = tok;
	tok = strtok_r(NULL, ",", &save);
    }
    return true;
}

bool schema_json_get_flat_value(const char *json, const char *key, char *out, size_t out_sz) {
    if (!json || !key || !out || out_sz == 0) return false;
    out[0] = '\0';
    char needle[512];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return false;

    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return false;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;

    size_t k = 0;
    if (*p == '"') {
    	p++;
    	while (*p && *p != '"') {
              if (k + 1 >= out_sz) return false;
              out[k++] = *p++;
        }
        if (*p != '"') return false;
    } else {
    	while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
        	if (k + 1 >= out_sz) return false;
        	out[k++] = *p++;
        }
    }
    out[k] = '\0';
    return true;
}

bool schema_save_artifact(const char *path, const FeatureSchema *s) {
    if (!path || !s) return false;
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "VERSION|%u\n", s->version);
    fprintf(f, "RAW_COL_COUNT|%zu\n", s->raw_col_count);
    fprintf(f, "FEATURE_COUNT|%zu\n", s->feature_count);

    char enc_name[4096];
    char enc_tok[4096];

    for (size_t i = 0; i < s->raw_col_count; i++) {
    	const SchemaColumn *c = &s->cols[i];
    	if (!pct_encode(c->name ? c->name : "", enc_name, sizeof(enc_name))) { fclose(f); return false; }
    	fprintf(
        	f,
        	"COL|%zu|%s|%d|%d|%zu|%.9g|%.9g|%.9g\n",
        	i, enc_name, (int)c->kind, (int)c->missing_rule,
        	c->numeric_feature_idx,
        	c->numeric_impute_value, c->scaler_min, c->scaler_max
        );
        for (size_t j = 0; j < c->cat_count; j++) {
            if (!pct_encode(c->cats[j].token ? c->cats[j].token : "", enc_tok, sizeof(enc_tok))) { fclose(f); return false; }
            fprintf(f, "CAT|%zu|%s|%zu\n", i, enc_tok, c->cats[j].feature_idx);
        }
    }

    fclose(f);
    return true;
}

bool schema_load_artifact(const char *path, FeatureSchema *s) {
    if (!path || !s) return false;
    schema_free(s);
    schema_init(s);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln - 1] == '\n' || line[ln - 1] == '\r')) line[--ln] = '\0';
        if (ln == 0) continue;

        char *parts[16];
        size_t np = 0;
        char *save = NULL;
        char *tok = strtok_r(line, "|", &save);
    	while (tok && np < 16) {
        	parts[np++] = tok;
        	tok = strtok_r(NULL, "|", &save);
        }
        if (np == 0) continue;

    	if (strcmp(parts[0], "VERSION") == 0 && np >= 2) {
        	s->version = (uint32_t)strtoul(parts[1], NULL, 10);
        } else if (strcmp(parts[0], "RAW_COL_COUNT") == 0 && np >= 2) {
        	size_t want = (size_t)strtoull(parts[1], NULL, 10);
        	while (s->raw_col_count < want) {
            		if (!schema_add_column(s, "", SCHEMA_KIND_NUMERIC)) { fclose(f); return false; }
        }
    	} else if (strcmp(parts[0], "FEATURE_COUNT") == 0 && np >= 2) {
        	s->feature_count = (size_t)strtoull(parts[1], NULL, 10);
    	} else if (strcmp(parts[0], "COL") == 0 && np >= 9) {
        	size_t idx = (size_t)strtoull(parts[1], NULL, 10);
        	char name_dec[4096];
        	if (!pct_decode(parts[2], name_dec, sizeof(name_dec))) { fclose(f); return false; }

        	while (s->raw_col_count <= idx) {
            		if (!schema_add_column(s, "", SCHEMA_KIND_NUMERIC)) { fclose(f); return false; }
        	}

        	free(s->cols[idx].name);
        	s->cols[idx].name = dup_cstr_local(name_dec);
        	s->cols[idx].kind = (SchemaKind)atoi(parts[3]);
        	s->cols[idx].missing_rule = (SchemaMissingRule)atoi(parts[4]);
        	s->cols[idx].numeric_feature_idx = (size_t)strtoull(parts[5], NULL, 10);
        	s->cols[idx].numeric_impute_value = strtof(parts[6], NULL);
        	s->cols[idx].scaler_min = strtof(parts[7], NULL);
        	s->cols[idx].scaler_max = strtof(parts[8], NULL);
    	} else if (strcmp(parts[0], "CAT") == 0 && np >= 4) {
        	size_t col = (size_t)strtoull(parts[1], NULL, 10);
        	char tok_dec[4096];
        	if (!pct_decode(parts[2], tok_dec, sizeof(tok_dec))) { fclose(f); return false; }
        	size_t fidx = (size_t)strtoull(parts[3], NULL, 10);
        	if (!schema_add_category(s, col, tok_dec, fidx)) { fclose(f); return false; }
        }
    }

    fclose(f);
    return true;
}