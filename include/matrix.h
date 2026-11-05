#ifndef MATRIX_H
#define MATRIX_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef void (*matrix_elem_dtor_fn)(void *elem, void *ctx);

typedef struct {
    unsigned char *data;
    size_t rows;
    size_t cols;
    size_t elem_size;
    matrix_elem_dtor_fn elem_dtor;
    void *dtor_ctx;
} Matrix;

static inline void matrix_reset(Matrix *m) {
    if (!m) return;
    m->data = NULL;
    m->rows = 0;
    m->cols = 0;
    m->elem_size = 0;
    m->elem_dtor = NULL;
    m->dtor_ctx = NULL;
}

static inline bool matrix_init(Matrix *m, size_t rows, 
			       size_t cols, 
			       size_t elem_size, 
			       matrix_elem_dtor_fn elem_dtor, 
			       void *dtor_ctx) {

    if (!m || elem_size == 0) return false;
    matrix_reset(m);
    
    if (rows != 0 && cols > SIZE_MAX / rows) return false;
    size_t count = rows * cols;

    if (count != 0 && elem_size > SIZE_MAX / count) return false;
    size_t bytes = count * elem_size;

    unsigned char *p = bytes ? (unsigned char *)calloc(1, bytes) : NULL;
    if (bytes && !p) return false;

    m->data = p;
    m->rows = rows;
    m->cols = cols;
    m->elem_size = elem_size;
    m->elem_dtor = elem_dtor;
    m->dtor_ctx = dtor_ctx;
    return true;
}

static inline size_t matrix_count(const Matrix *m) {
    return (m && m->rows != 0 && m->cols <= SIZE_MAX / m->rows) ? (m->rows * m->cols) : 0;
}

static inline bool matrix_in_bounds(const Matrix *m, size_t r, size_t c) {
    return m && r < m->rows && c < m->cols;
}

static inline void *matrix_at(Matrix *m, size_t r, size_t c) {
    if (!matrix_in_bounds(m, r, c)) return NULL;
    size_t idx = r * m->cols + c;
    return m->data + idx * m->elem_size;
}

static inline const void *matrix_at_const(const Matrix *m, size_t r, size_t c) {
    if (!matrix_in_bounds(m, r, c)) return NULL;
    size_t idx = r * m->cols + c;
    return m->data + idx * m->elem_size;
}

static inline bool matrix_set(Matrix *m, size_t r, size_t c, const void *value) {
    if (!value) return false;
    void *slot = matrix_at(m, r, c);
    if (!slot) return false;
    memcpy(slot, value, m->elem_size);
    return true;
}

static inline bool matrix_get(const Matrix *m, size_t r, size_t c, void *out_value) {
    if (!out_value) return false;
    const void *slot = matrix_at_const(m, r, c);
    if (!slot) return false;
    memcpy(out_value, slot, m->elem_size);
    return true;
}

static inline void matrix_free(Matrix *m) {
    if (!m) return;

    if (m->data && m->elem_dtor) {
        size_t n = matrix_count(m);
        for (size_t i = 0; i < n; ++i) {
            void *elem = m->data + i * m->elem_size;
            m->elem_dtor(elem, m->dtor_ctx);
        }
    }

    free(m->data);
    matrix_reset(m);
}

#define MATRIX_INIT_TYPE(m, rows, cols, Type) \
    matrix_init((m), (rows), (cols), sizeof(Type), NULL, NULL)

#define MATRIX_AT_TYPE(m, r, c, Type) \
    ((Type *)matrix_at((m), (r), (c)))

#define MATRIX_AT_CONST_TYPE(m, r, c, Type) \
    ((const Type *)matrix_at_const((m), (r), (c)))

/*
 * Typed wrapper generator.
 * Example:
 *   MATRIX_DEFINE_TYPE(matrix_int, int)
 *   matrix_int m = matrix_int_make(3, 4);
 */
#define MATRIX_DEFINE_TYPE(Name, ElemType)                                         \
    typedef struct {                                                               \
        Matrix raw;                                                                \
    } Name;                                                                        \
                                                                                   \
    static inline bool Name##_init(Name *m, size_t rows, size_t cols) {           \
        if (!m) return false;                                                      \
        return matrix_init(&m->raw, rows, cols, sizeof(ElemType), NULL, NULL);    \
    }                                                                              \
                                                                                   \
    static inline bool Name##_init_with_dtor(                                      \
        Name *m,                                                                   \
        size_t rows,                                                               \
        size_t cols,                                                               \
        matrix_elem_dtor_fn dtor,                                                  \
        void *ctx                                                                  \
    ) {                                                                            \
        if (!m) return false;                                                      \
        return matrix_init(&m->raw, rows, cols, sizeof(ElemType), dtor, ctx);     \
    }                                                                              \
                                                                                   \
    static inline Name Name##_make(size_t rows, size_t cols) {                     \
        Name m;                                                                    \
        matrix_reset(&m.raw);                                                      \
        (void)Name##_init(&m, rows, cols);                                         \
        return m;                                                                  \
    }                                                                              \
                                                                                   \
    static inline Name Name##_make_with_dtor(                                      \
        size_t rows,                                                               \
        size_t cols,                                                               \
        matrix_elem_dtor_fn dtor,                                                  \
        void *ctx                                                                  \
    ) {                                                                            \
        Name m;                                                                    \
        matrix_reset(&m.raw);                                                      \
        (void)Name##_init_with_dtor(&m, rows, cols, dtor, ctx);                   \
        return m;                                                                  \
    }                                                                              \
                                                                                   \
    static inline void Name##_free(Name *m) {                                      \
        if (!m) return;                                                            \
        matrix_free(&m->raw);                                                      \
    }                                                                              \
                                                                                   \
    static inline size_t Name##_rows(const Name *m) {                              \
        return m ? m->raw.rows : 0;                                                \
    }                                                                              \
                                                                                   \
    static inline size_t Name##_cols(const Name *m) {                              \
        return m ? m->raw.cols : 0;                                                \
    }                                                                              \
                                                                                   \
    static inline size_t Name##_count(const Name *m) {                             \
        return m ? matrix_count(&m->raw) : 0;                                      \
    }                                                                              \
                                                                                   \
    static inline bool Name##_in_bounds(const Name *m, size_t r, size_t c) {       \
        return m ? matrix_in_bounds(&m->raw, r, c) : false;                        \
    }                                                                              \
                                                                                   \
    static inline ElemType *Name##_at(Name *m, size_t r, size_t c) {               \
        return m ? (ElemType *)matrix_at(&m->raw, r, c) : NULL;                    \
    }                                                                              \
                                                                                   \
    static inline const ElemType *Name##_at_const(                                 \
        const Name *m,                                                             \
        size_t r,                                                                  \
        size_t c                                                                   \
    ) {                                                                            \
        return m ? (const ElemType *)matrix_at_const(&m->raw, r, c) : NULL;        \
    }                                                                              \
                                                                                   \
    static inline bool Name##_set(Name *m, size_t r, size_t c, ElemType value) {   \
        return m ? matrix_set(&m->raw, r, c, &value) : false;                      \
    }                                                                              \
                                                                                   \
    static inline bool Name##_get(                                                 \
        const Name *m,                                                             \
        size_t r,                                                                  \
        size_t c,                                                                  \
        ElemType *out_value                                                        \
    ) {                                                                            \
        return (m && out_value) ? matrix_get(&m->raw, r, c, out_value) : false;   \
    }

/* Optional naming sugar */
#define matrix(T) matrix_##T
#define matrix_init_t(T, mptr, rows, cols) matrix_##T##_init((mptr), (rows), (cols))
#define matrix_make_t(T, rows, cols) matrix_##T##_make((rows), (cols))


/* Built-in typed matrices */
MATRIX_DEFINE_TYPE(matrix_int, int)
MATRIX_DEFINE_TYPE(matrix_float, float)
MATRIX_DEFINE_TYPE(matrix_double, double)
MATRIX_DEFINE_TYPE(matrix_bool, bool)
MATRIX_DEFINE_TYPE(matrix_char, char)

#endif    
