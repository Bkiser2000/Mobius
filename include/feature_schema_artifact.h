#ifndef FEATURE_SCHEMA_ARTIFACT_H
#define FEATURE_SCHEMA_ARTIFACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
    SCHEMA_KIND_NUMERIC = 1,
    SCHEMA_KIND_CATEGORICAL = 2
} SchemaKind;

typedef enum {
    SCHEMA_MISSING_ZERO = 0,
    SCHEMA_MISSING_MEAN = 1,
    SCHEMA_MISSING_TOKEN = 2
} SchemaMissingRule;

typedef struct {
    char *token;
    size_t feature_idx;
} SchemaCategory;

typedef struct {
    char *name;
    SchemaKind kind;
    SchemaMissingRule missing_rule;
    size_t numeric_feature_idx;
    float numeric_impute_value;
    float scaler_min;
    float scaler_max;
    SchemaCategory *cats;
    size_t cat_count;
    size_t cat_cap;
} SchemaColumn;

typedef struct {
    uint32_t version;
    size_t raw_col_count;
    size_t feature_count;
    SchemaColumn *cols;
} FeatureSchema;

void schema_init(FeatureSchema *s);
void schema_free(FeatureSchema *s);

bool schema_add_column(FeatureSchema *s, const char *name, SchemaKind kind);
bool schema_add_category(FeatureSchema *s, size_t col_idx, const char *token, size_t feature_idx);

bool schema_save_artifact(const char *path, const FeatureSchema *s);
bool schema_load_artifact(const char *path, FeatureSchema *s);

bool schema_find_category_feature(const FeatureSchema *s, size_t col_idx, const char *token, size_t *out_feature_idx);
ssize_t schema_find_column_by_name(const FeatureSchema *s, const char *name);

bool schema_parse_csv_fields(char *line_mutable, char **out_fields, size_t max_fields, size_t *out_count);
bool schema_json_get_flat_value(const char *json, const char *key, char *out, size_t out_sz);

#endif