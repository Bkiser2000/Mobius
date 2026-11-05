#ifndef TENSOR_H
#define TENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    TENSOR_I8, TENSOR_U8,
    TENSOR_I16, TENSOR_U16,
    TENSOR_I32, TENSOR_U32,
    TENSOR_I64, TENSOR_U64,
    TENSOR_F32, TENSOR_F64,
    TENSOR_BOOL,
    TENSOR_STRING
} TensorType;

typedef struct {
    size_t rank;
    size_t elements;
    size_t *shape;
    size_t *strides;       /* row-major */
    TensorType type;
    void *data;            /* char ** for strings */
} Tensor;

static inline size_t tensor_type_size(TensorType type)
{
    switch (type) {
        case TENSOR_I8:     return sizeof(int8_t);
        case TENSOR_U8:     return sizeof(uint8_t);
        case TENSOR_I16:    return sizeof(int16_t);
        case TENSOR_U16:    return sizeof(uint16_t);
        case TENSOR_I32:    return sizeof(int32_t);
        case TENSOR_U32:    return sizeof(uint32_t);
        case TENSOR_I64:    return sizeof(int64_t);
        case TENSOR_U64:    return sizeof(uint64_t);
        case TENSOR_F32:    return sizeof(float);
        case TENSOR_F64:    return sizeof(double);
        case TENSOR_BOOL:   return sizeof(bool);
        case TENSOR_STRING: return sizeof(char *);
        default:            return 0;
    }
}

static inline void tensor_destroy(Tensor *t)
{
    if (!t) return;

    if (t->type == TENSOR_STRING && t->data) {
        char **strings = (char **)t->data;
        for (size_t i = 0; i < t->elements; ++i)
            free(strings[i]);
    }

    free(t->data);
    free(t->strides);
    free(t->shape);
    free(t);
}

/* Dimensions must be positive. Returns NULL on invalid input or failure. */
static inline Tensor *tensor_create(size_t rank, const size_t *shape, TensorType type) {
    size_t item_size = tensor_type_size(type);
    size_t count = 1;

    if (rank == 0 || !shape || item_size == 0 ||
        rank > SIZE_MAX / sizeof(size_t))
        return NULL;

    Tensor *t = (Tensor *)calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->rank = rank;
    t->type = type;
    t->shape = (size_t *)malloc(rank * sizeof(size_t));
    t->strides = (size_t *)malloc(rank * sizeof(size_t));

    if (!t->shape || !t->strides)
        goto fail;

    for (size_t i = rank; i-- > 0;) {
        if (shape[i] == 0 || count > SIZE_MAX / shape[i])
            goto fail;

        t->shape[i] = shape[i];
        t->strides[i] = count;
        count *= shape[i];
    }

    if (count > SIZE_MAX / item_size)
        goto fail;

    t->elements = count;
    t->data = calloc(count, item_size);
    if (!t->data)
        goto fail;

    return t;

fail:
    tensor_destroy(t);
    return NULL;
}

/* Get a fixed-size element by linear index; cast to the matching type.
   For strings, use tensor_set_string/tensor_get_string instead. */
static inline void *tensor_at(Tensor *t, size_t linear_index)
{
    if (!t || !t->data || t->type == TENSOR_STRING ||
        linear_index >= t->elements)
        return NULL;

    return (unsigned char *)t->data +
           linear_index * tensor_type_size(t->type);
}

/* Convert multidimensional coordinates to a linear row-major index. */
static inline bool tensor_offset(const Tensor *t, const size_t *indices, size_t *out_offset) {
    if (!t || !indices || !out_offset) return false;

    size_t offset = 0;
    for (size_t i = 0; i < t->rank; ++i) {
        if (indices[i] >= t->shape[i]) return false;
        offset += indices[i] * t->strides[i];
    }

    *out_offset = offset;
    return true;
}

static inline bool tensor_set_string(Tensor *t, size_t linear_index, const char *value) {
    if (!t || t->type != TENSOR_STRING ||
        linear_index >= t->elements || !value)
        return false;

    size_t length = strlen(value);
    if (length == SIZE_MAX) return false;

    char *copy = (char *)malloc(length + 1);
    if (!copy) return false;
    memcpy(copy, value, length + 1);

    char **strings = (char **)t->data;
    free(strings[linear_index]);
    strings[linear_index] = copy;
    return true;
}

static inline const char *tensor_get_string(const Tensor *t, size_t linear_index) {
    if (!t || t->type != TENSOR_STRING ||
        linear_index >= t->elements)
        return NULL;

    char *const *strings = (char *const *)t->data;
    return strings[linear_index];
}

/* -------- Typed scalar set/get API (strict type checks) -------- */

static inline bool tensor_set_raw(
    Tensor *t, size_t linear_index, const void *value, TensorType expected_type)
{
    if (!t || !value || !t->data) return false;
    if (t->type != expected_type || t->type == TENSOR_STRING) return false;
    if (linear_index >= t->elements) return false;

    size_t item_size = tensor_type_size(expected_type);
    if (item_size == 0) return false;

    unsigned char *base = (unsigned char *)t->data;
    memcpy(base + linear_index * item_size, value, item_size);
    return true;
}

static inline bool tensor_get_raw(
    const Tensor *t, size_t linear_index, void *out_value, TensorType expected_type)
{
    if (!t || !out_value || !t->data) return false;
    if (t->type != expected_type || t->type == TENSOR_STRING) return false;
    if (linear_index >= t->elements) return false;

    size_t item_size = tensor_type_size(expected_type);
    if (item_size == 0) return false;

    const unsigned char *base = (const unsigned char *)t->data;
    memcpy(out_value, base + linear_index * item_size, item_size);
    return true;
}

static inline bool tensor_set_raw_at(
    Tensor *t, const size_t *indices, const void *value, TensorType expected_type)
{
    size_t offset = 0;
    if (!tensor_offset(t, indices, &offset)) return false;
    return tensor_set_raw(t, offset, value, expected_type);
}

static inline bool tensor_get_raw_at(
    const Tensor *t, const size_t *indices, void *out_value, TensorType expected_type)
{
    size_t offset = 0;
    if (!tensor_offset(t, indices, &offset)) return false;
    return tensor_get_raw(t, offset, out_value, expected_type);
}

#define TENSOR_DEFINE_TYPED_ACCESSORS(SUFFIX, CTYPE, ENUM_TYPE)                           \
    static inline bool tensor_set_##SUFFIX(Tensor *t, size_t linear_index, CTYPE value)   \
    {                                                                                       \
        return tensor_set_raw(t, linear_index, &value, ENUM_TYPE);                         \
    }                                                                                       \
                                                                                            \
    static inline bool tensor_get_##SUFFIX(                                                 \
        const Tensor *t, size_t linear_index, CTYPE *out_value)                            \
    {                                                                                       \
        return tensor_get_raw(t, linear_index, out_value, ENUM_TYPE);                      \
    }                                                                                       \
                                                                                            \
    static inline bool tensor_set_##SUFFIX##_at(                                            \
        Tensor *t, const size_t *indices, CTYPE value)                                     \
    {                                                                                       \
        return tensor_set_raw_at(t, indices, &value, ENUM_TYPE);                           \
    }                                                                                       \
                                                                                            \
    static inline bool tensor_get_##SUFFIX##_at(                                            \
        const Tensor *t, const size_t *indices, CTYPE *out_value)                          \
    {                                                                                       \
        return tensor_get_raw_at(t, indices, out_value, ENUM_TYPE);                        \
    }

TENSOR_DEFINE_TYPED_ACCESSORS(i8,   int8_t,   TENSOR_I8)
TENSOR_DEFINE_TYPED_ACCESSORS(u8,   uint8_t,  TENSOR_U8)
TENSOR_DEFINE_TYPED_ACCESSORS(i16,  int16_t,  TENSOR_I16)
TENSOR_DEFINE_TYPED_ACCESSORS(u16,  uint16_t, TENSOR_U16)
TENSOR_DEFINE_TYPED_ACCESSORS(i32,  int32_t,  TENSOR_I32)
TENSOR_DEFINE_TYPED_ACCESSORS(u32,  uint32_t, TENSOR_U32)
TENSOR_DEFINE_TYPED_ACCESSORS(i64,  int64_t,  TENSOR_I64)
TENSOR_DEFINE_TYPED_ACCESSORS(u64,  uint64_t, TENSOR_U64)
TENSOR_DEFINE_TYPED_ACCESSORS(f32,  float,    TENSOR_F32)
TENSOR_DEFINE_TYPED_ACCESSORS(f64,  double,   TENSOR_F64)
TENSOR_DEFINE_TYPED_ACCESSORS(bool, bool,     TENSOR_BOOL)

#undef TENSOR_DEFINE_TYPED_ACCESSORS

static inline bool tensor_set_string_at(Tensor *t, const size_t *indices, const char *value) {
    size_t offset = 0;
    if (!tensor_offset(t, indices, &offset)) return false;
    return tensor_set_string(t, offset, value);
}

static inline const char *tensor_get_string_at(const Tensor *t, const size_t *indices) {
    size_t offset = 0;
    if (!tensor_offset(t, indices, &offset)) return NULL;
    return tensor_get_string(t, offset);
}

static inline bool tensor_fill_raw(Tensor *t, const void *value, TensorType expected_type) {
    if (!t || !value || !t->data) return false;
    if (t->type != expected_type || t->type == TENSOR_STRING) return false;

    size_t item_size = tensor_type_size(expected_type);
    if (item_size == 0) return false;

    unsigned char *base = (unsigned char *)t->data;
    for (size_t i = 0; i < t->elements; ++i) {
        memcpy(base + i * item_size, value, item_size);
    }
    return true;
}

#define TENSOR_DEFINE_TYPED_FILL(SUFFIX, CTYPE, ENUM_TYPE)                          \
    static inline bool tensor_fill_##SUFFIX(Tensor *t, CTYPE value)                 \
    {                                                                                \
        return tensor_fill_raw(t, &value, ENUM_TYPE);                               \
    }

TENSOR_DEFINE_TYPED_FILL(i8,   int8_t,   TENSOR_I8)
TENSOR_DEFINE_TYPED_FILL(u8,   uint8_t,  TENSOR_U8)
TENSOR_DEFINE_TYPED_FILL(i16,  int16_t,  TENSOR_I16)
TENSOR_DEFINE_TYPED_FILL(u16,  uint16_t, TENSOR_U16)
TENSOR_DEFINE_TYPED_FILL(i32,  int32_t,  TENSOR_I32)
TENSOR_DEFINE_TYPED_FILL(u32,  uint32_t, TENSOR_U32)
TENSOR_DEFINE_TYPED_FILL(i64,  int64_t,  TENSOR_I64)
TENSOR_DEFINE_TYPED_FILL(u64,  uint64_t, TENSOR_U64)
TENSOR_DEFINE_TYPED_FILL(f32,  float,    TENSOR_F32)
TENSOR_DEFINE_TYPED_FILL(f64,  double,   TENSOR_F64)
TENSOR_DEFINE_TYPED_FILL(bool, bool,     TENSOR_BOOL)

#undef TENSOR_DEFINE_TYPED_FILL

static inline bool tensor_fill_string(Tensor *t, const char *value) {
    if (!t || t->type != TENSOR_STRING || !value) return false;
    for (size_t i = 0; i < t->elements; ++i) {
        if (!tensor_set_string(t, i, value)) return false;
    }
    return true;
}

static inline bool tensor_reshape(Tensor *t, size_t new_rank, const size_t *new_shape) {
    if (!t || !new_shape || new_rank == 0) return false;
    if (new_rank > SIZE_MAX / sizeof(size_t)) return false;

    size_t new_count = 1;
    for (size_t i = new_rank; i-- > 0;) {
        if (new_shape[i] == 0 || new_count > SIZE_MAX / new_shape[i]) return false;
        new_count *= new_shape[i];
    }

    if (new_count != t->elements) return false;

    size_t *shape2 = (size_t *)malloc(new_rank * sizeof(size_t));
    size_t *strides2 = (size_t *)malloc(new_rank * sizeof(size_t));
    if (!shape2 || !strides2) {
        free(shape2);
        free(strides2);
        return false;
    }

    size_t stride = 1;
    for (size_t i = new_rank; i-- > 0;) {
        shape2[i] = new_shape[i];
        strides2[i] = stride;
        stride *= new_shape[i];
    }

    free(t->shape);
    free(t->strides);
    t->shape = shape2;
    t->strides = strides2;
    t->rank = new_rank;
    return true;
}

static inline Tensor *tensor_clone(const Tensor *src) {
    if (!src || !src->shape) return NULL;

    Tensor *dst = tensor_create(src->rank, src->shape, src->type);
    if (!dst) return NULL;

    if (src->type == TENSOR_STRING) {
        char *const *s = (char *const *)src->data;
        for (size_t i = 0; i < src->elements; ++i) {
            if (s[i] && !tensor_set_string(dst, i, s[i])) {
                tensor_destroy(dst);
                return NULL;
            }
        }
        return dst;
    }

    size_t item_size = tensor_type_size(src->type);
    if (item_size == 0) {
        tensor_destroy(dst);
        return NULL;
    }

    memcpy(dst->data, src->data, src->elements * item_size);
    return dst;
}

static inline bool tensor_print_flat(const Tensor *t, FILE *out) {
    if (!t) return false;
    if (!out) out = stdout;

    fprintf(out, "Tensor(rank=%zu, elements=%zu, shape=[", t->rank, t->elements);
    for (size_t i = 0; i < t->rank; ++i) {
        fprintf(out, "%zu%s", t->shape[i], (i + 1 < t->rank) ? ", " : "");
    }
    fprintf(out, "], values=[");

    switch (t->type) {
        case TENSOR_I8: {
            const int8_t *p = (const int8_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%d%s", (int)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_U8: {
            const uint8_t *p = (const uint8_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%u%s", (unsigned)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_I16: {
            const int16_t *p = (const int16_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%d%s", (int)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_U16: {
            const uint16_t *p = (const uint16_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%u%s", (unsigned)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_I32: {
            const int32_t *p = (const int32_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%d%s", (int)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_U32: {
            const uint32_t *p = (const uint32_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%u%s", (unsigned)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_I64: {
            const int64_t *p = (const int64_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%lld%s", (long long)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_U64: {
            const uint64_t *p = (const uint64_t *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%llu%s", (unsigned long long)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_F32: {
            const float *p = (const float *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%g%s", (double)p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_F64: {
            const double *p = (const double *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%g%s", p[i], (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_BOOL: {
            const bool *p = (const bool *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "%s%s", p[i] ? "true" : "false", (i + 1 < t->elements) ? ", " : "");
            break;
        }
        case TENSOR_STRING: {
            char *const *p = (char *const *)t->data;
            for (size_t i = 0; i < t->elements; ++i)
                fprintf(out, "\"%s\"%s", p[i] ? p[i] : "", (i + 1 < t->elements) ? ", " : "");
            break;
        }
        default:
            return false;
    }

    fprintf(out, "])\n");
    return true;
}

static inline bool tensor_print_scalar_at(const Tensor *t, size_t linear, FILE *out) {
    if (!t || !out || linear >= t->elements) return false;

    switch (t->type) {
        case TENSOR_I8: {
            const int8_t *p = (const int8_t *)t->data;
            fprintf(out, "%d", (int)p[linear]);
            return true;
        }
        case TENSOR_U8: {
            const uint8_t *p = (const uint8_t *)t->data;
            fprintf(out, "%u", (unsigned)p[linear]);
            return true;
        }
        case TENSOR_I16: {
            const int16_t *p = (const int16_t *)t->data;
            fprintf(out, "%d", (int)p[linear]);
            return true;
        }
        case TENSOR_U16: {
            const uint16_t *p = (const uint16_t *)t->data;
            fprintf(out, "%u", (unsigned)p[linear]);
            return true;
        }
        case TENSOR_I32: {
            const int32_t *p = (const int32_t *)t->data;
            fprintf(out, "%d", (int)p[linear]);
            return true;
        }
        case TENSOR_U32: {
            const uint32_t *p = (const uint32_t *)t->data;
            fprintf(out, "%u", (unsigned)p[linear]);
            return true;
        }
        case TENSOR_I64: {
            const int64_t *p = (const int64_t *)t->data;
            fprintf(out, "%lld", (long long)p[linear]);
            return true;
        }
        case TENSOR_U64: {
            const uint64_t *p = (const uint64_t *)t->data;
            fprintf(out, "%llu", (unsigned long long)p[linear]);
            return true;
        }
        case TENSOR_F32: {
            const float *p = (const float *)t->data;
            fprintf(out, "%g", (double)p[linear]);
            return true;
        }
        case TENSOR_F64: {
            const double *p = (const double *)t->data;
            fprintf(out, "%g", p[linear]);
            return true;
        }
        case TENSOR_BOOL: {
            const bool *p = (const bool *)t->data;
            fprintf(out, "%s", p[linear] ? "true" : "false");
            return true;
        }
        case TENSOR_STRING: {
            char *const *p = (char *const *)t->data;
            fprintf(out, "\"%s\"", p[linear] ? p[linear] : "");
            return true;
        }
        default:
            return false;
    }
}

static inline bool tensor_print_nd_recursive(const Tensor *t, FILE *out, size_t dim, size_t base_offset) {
    if (!t || !out || dim >= t->rank) return false;

    fprintf(out, "[");

    size_t n = t->shape[dim];
    size_t stride = t->strides[dim];

    for (size_t i = 0; i < n; ++i) {
        size_t off = base_offset + i * stride;

        if (dim + 1 == t->rank) {
            if (!tensor_print_scalar_at(t, off, out)) return false;
        } else {
            if (!tensor_print_nd_recursive(t, out, dim + 1, off)) return false;
        }

        if (i + 1 < n) fprintf(out, ", ");
    }

    fprintf(out, "]");
    return true;
}

static inline bool tensor_print_pretty(const Tensor *t, FILE *out) {
    if (!t) return false;
    if (!out) out = stdout;

    fprintf(out, "Tensor(shape=[");
    for (size_t i = 0; i < t->rank; ++i) {
        fprintf(out, "%zu%s", t->shape[i], (i + 1 < t->rank) ? ", " : "");
    }
    fprintf(out, "], type=%d)\n", (int)t->type);

    if (!t->data) {
        fprintf(out, "null\n");
        return true;
    }

    if (t->rank == 0) {
        fprintf(out, "[]\n");
        return true;
    }

    if (!tensor_print_nd_recursive(t, out, 0, 0)) return false;
    fprintf(out, "\n");
    return true;
}

#endif

