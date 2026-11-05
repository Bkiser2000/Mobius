#ifndef VECTOR_H
#define VECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline char *vector_strdup_local(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (!d) return NULL;
    memcpy(d, s, n);
    return d;
}

#define VECTOR_FIRST(a, ...) (a)

#define VECTOR_DEFINE_PLAIN_TYPE(NAME, TYPE)                                              \
typedef struct {                                                                           \
    TYPE *data;                                                                            \
    size_t size;                                                                           \
    size_t capacity;                                                                       \
} vector_##NAME;                                                                           \
                                                                                            \
static inline void vector_##NAME##_init(vector_##NAME *v) {                               \
    v->data = NULL;                                                                        \
    v->size = 0;                                                                           \
    v->capacity = 0;                                                                       \
}                                                                                          \
                                                                                            \
static inline int vector_##NAME##_reserve(vector_##NAME *v, size_t wanted) {              \
    if (wanted <= v->capacity) return 1;                                                   \
    if (wanted > SIZE_MAX / sizeof(TYPE)) return 0;                                        \
    size_t cap = v->capacity ? v->capacity : 8;                                            \
    while (cap < wanted) {                                                                 \
        if (cap > SIZE_MAX / 2) { cap = wanted; break; }                                   \
        cap *= 2;                                                                          \
    }                                                                                      \
    TYPE *p = (TYPE *)realloc(v->data, cap * sizeof(TYPE));                               \
    if (!p) return 0;                                                                      \
    v->data = p;                                                                           \
    v->capacity = cap;                                                                     \
    return 1;                                                                              \
}                                                                                          \
                                                                                            \
static inline int vector_##NAME##_insert(vector_##NAME *v, size_t i, TYPE x) {            \
    if (i > v->size || v->size == SIZE_MAX) return 0;                                      \
    if (!vector_##NAME##_reserve(v, v->size + 1)) return 0;                                \
    memmove(&v->data[i + 1], &v->data[i], (v->size - i) * sizeof(TYPE));                  \
    v->data[i] = x;                                                                        \
    v->size++;                                                                             \
    return 1;                                                                              \
}                                                                                          \
                                                                                            \
static inline int vector_##NAME##_push(vector_##NAME *v, TYPE x) {                         \
    return vector_##NAME##_insert(v, v->size, x);                                          \
}                                                                                          \
                                                                                            \
static inline int vector_##NAME##_erase(vector_##NAME *v, size_t i) {                      \
    if (i >= v->size) return 0;                                                            \
    memmove(&v->data[i], &v->data[i + 1], (v->size - i - 1) * sizeof(TYPE));              \
    v->size--;                                                                             \
    return 1;                                                                              \
}                                                                                          \
                                                                                            \
static inline int vector_##NAME##_swap(vector_##NAME *v, size_t a, size_t b) {             \
    if (a >= v->size || b >= v->size) return 0;                                            \
    TYPE temp = v->data[a];                                                                \
    v->data[a] = v->data[b];                                                               \
    v->data[b] = temp;                                                                     \
    return 1;                                                                              \
}                                                                                          \
                                                                                            \
static inline void vector_##NAME##_free(vector_##NAME *v) {                                \
    free(v->data);                                                                         \
    vector_##NAME##_init(v);                                                               \
}                                                                                          \
                                                                                            \
static inline vector_##NAME vector_##NAME##_from_array(const TYPE *src, size_t n) {        \
    vector_##NAME v;                                                                       \
    vector_##NAME##_init(&v);                                                              \
    if (n && !vector_##NAME##_reserve(&v, n)) return v;                                    \
    if (n) memcpy(v.data, src, n * sizeof(TYPE));                                          \
    v.size = n;                                                                            \
    return v;                                                                              \
}                                                                                          \
                                                                                            \
static inline vector_##NAME vector_##NAME##_make_impl(const TYPE *arr, size_t n) {         \
    return vector_##NAME##_from_array(arr, n);                                             \
}

VECTOR_DEFINE_PLAIN_TYPE(int, int)
VECTOR_DEFINE_PLAIN_TYPE(float, float)
VECTOR_DEFINE_PLAIN_TYPE(double, double)
VECTOR_DEFINE_PLAIN_TYPE(char, char)
VECTOR_DEFINE_PLAIN_TYPE(bool, bool)

typedef struct {
    char **data;
    size_t size;
    size_t capacity;
} vector_string;

static inline void vector_string_init(vector_string *v) {
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

static inline int vector_string_reserve(vector_string *v, size_t wanted) {
    if (wanted <= v->capacity) return 1;
    if (wanted > SIZE_MAX / sizeof(char *)) return 0;
    size_t cap = v->capacity ? v->capacity : 8;
    while (cap < wanted) {
        if (cap > SIZE_MAX / 2) { cap = wanted; break; }
        cap *= 2;
    }
    char **p = (char **)realloc(v->data, cap * sizeof(char *));
    if (!p) return 0;
    v->data = p;
    v->capacity = cap;
    return 1;
}

static inline int vector_string_insert(vector_string *v, size_t i, const char *s) {
    if (i > v->size || v->size == SIZE_MAX) return 0;
    if (!vector_string_reserve(v, v->size + 1)) return 0;

    memmove(&v->data[i + 1], &v->data[i], (v->size - i) * sizeof(char *));

    v->data[i] = vector_strdup_local(s);
    if (!v->data[i]) {
        memmove(&v->data[i], &v->data[i + 1], (v->size - i) * sizeof(char *));
        return 0;
    }

    v->size++;
    return 1;
}

static inline int vector_string_push(vector_string *v, const char *s) {
    return vector_string_insert(v, v->size, s);
}

static inline int vector_string_erase(vector_string *v, size_t i) {
    if (i >= v->size) return 0;
    free(v->data[i]);
    memmove(&v->data[i], &v->data[i + 1], (v->size - i - 1) * sizeof(char *));
    v->size--;
    return 1;
}

static inline int vector_string_swap(vector_string *v, size_t a, size_t b) {
    if (a >= v->size || b >= v->size) return 0;
    char *temp = v->data[a];
    v->data[a] = v->data[b];
    v->data[b] = temp;
    return 1;
}

static inline void vector_string_free(vector_string *v) {
    for (size_t i = 0; i < v->size; ++i) {
        free(v->data[i]);
    }
    free(v->data);
    vector_string_init(v);
}

static inline vector_string vector_string_from_array(const char *const *src, size_t n) {
    vector_string v;
    vector_string_init(&v);

    for (size_t i = 0; i < n; ++i) {
        if (!vector_string_push(&v, src[i])) {
            vector_string_free(&v);
            return v;
        }
    }

    return v;
}

static inline vector_string vector_string_make_impl(const char *const *arr, size_t n) {
    return vector_string_from_array(arr, n);
}

#define vector(T) vector_##T

#define values_int(...) \
    vector_int_make_impl((const int[]){__VA_ARGS__}, sizeof((const int[]){__VA_ARGS__}) / sizeof(int))

#define values_float(...) \
    vector_float_make_impl((const float[]){__VA_ARGS__}, sizeof((const float[]){__VA_ARGS__}) / sizeof(float))

#define values_double(...) \
    vector_double_make_impl((const double[]){__VA_ARGS__}, sizeof((const double[]){__VA_ARGS__}) / sizeof(double))

#define values_char(...) \
    vector_char_make_impl((const char[]){__VA_ARGS__}, sizeof((const char[]){__VA_ARGS__}) / sizeof(char))

#define values_bool(...) \
    vector_bool_make_impl((const bool[]){__VA_ARGS__}, sizeof((const bool[]){__VA_ARGS__}) / sizeof(bool))

#define values_string(...) \
    vector_string_make_impl((const char *const[]){__VA_ARGS__}, sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *))

#define values(...) _Generic((VECTOR_FIRST(__VA_ARGS__)), \
    int: values_int, \
    float: values_float, \
    double: values_double, \
    char: values_char, \
    bool: values_bool, \
    char *: values_string, \
    const char *: values_string \
)(__VA_ARGS__)

#define vector_push(v, x) _Generic((v), \
    vector_int *: vector_int_push, \
    vector_float *: vector_float_push, \
    vector_double *: vector_double_push, \
    vector_char *: vector_char_push, \
    vector_bool *: vector_bool_push, \
    vector_string *: vector_string_push \
)((v), (x))

#define vector_insert(v, i, x) _Generic((v), \
    vector_int *: vector_int_insert, \
    vector_float *: vector_float_insert, \
    vector_double *: vector_double_insert, \
    vector_char *: vector_char_insert, \
    vector_bool *: vector_bool_insert, \
    vector_string *: vector_string_insert \
)((v), (i), (x))

#define vector_erase(v, i) _Generic((v), \
    vector_int *: vector_int_erase, \
    vector_float *: vector_float_erase, \
    vector_double *: vector_double_erase, \
    vector_char *: vector_char_erase, \
    vector_bool *: vector_bool_erase, \
    vector_string *: vector_string_erase \
)((v), (i))

#define vector_swap(v, a, b) _Generic((v), \
    vector_int *: vector_int_swap, \
    vector_float *: vector_float_swap, \
    vector_double *: vector_double_swap, \
    vector_char *: vector_char_swap, \
    vector_bool *: vector_bool_swap, \
    vector_string *: vector_string_swap \
)((v), (a), (b))

#define vector_free(v) _Generic((v), \
    vector_int *: vector_int_free, \
    vector_float *: vector_float_free, \
    vector_double *: vector_double_free, \
    vector_char *: vector_char_free, \
    vector_bool *: vector_bool_free, \
    vector_string *: vector_string_free \
)((v))

#endif