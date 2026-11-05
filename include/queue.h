#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef bool (*queue_copy_fn)(void *dst_elem, const void *src_elem, void *ctx);
typedef bool (*queue_move_out_fn)(void *out_elem, void *src_elem, void *ctx);
typedef void (*queue_dtor_fn)(void *elem, void *ctx);

typedef struct QueueNode {
    struct QueueNode *next;
    unsigned char data[];
} QueueNode;

typedef struct Queue {
    QueueNode *front;
    QueueNode *rear;
    size_t size;
    size_t elem_size;
    queue_copy_fn copy_fn;
    queue_move_out_fn move_out_fn;
    queue_dtor_fn dtor_fn;
    void *ctx;
} Queue;

static inline void queue_reset(Queue *q) {
    if (!q) return;
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
    q->elem_size = 0;
    q->copy_fn = NULL;
    q->move_out_fn = NULL;
    q->dtor_fn = NULL;
    q->ctx = NULL;
}

static inline bool queue_init(Queue *q, size_t elem_size,
                              queue_copy_fn copy_fn,
                              queue_move_out_fn move_out_fn,
                              queue_dtor_fn dtor_fn,
                              void *ctx) {

    if (!q || elem_size == 0) return false;
    queue_reset(q);
    q->elem_size = elem_size;
    q->copy_fn = copy_fn;
    q->move_out_fn = move_out_fn;
    q->dtor_fn = dtor_fn;
    q->ctx = ctx;
    return true;
}

static inline bool queue_init_plain(Queue *q, size_t elem_size) {
    return queue_init(q, elem_size, NULL, NULL, NULL, NULL);
}

static inline bool queue_is_empty(const Queue *q) {
    return (!q || q->size == 0);
}

static inline size_t queue_size(const Queue *q) {
    return q ? q->size : 0;
}

static inline bool queue_push(Queue *q, const void *elem) {
    if (!q || !elem || q->elem_size == 0) return false;
    if (q->elem_size > (SIZE_MAX - sizeof(QueueNode))) return false;

    QueueNode *n = (QueueNode *)malloc(sizeof(QueueNode) + q->elem_size);
    if (!n) return false;
    n->next = NULL;

    if (q->copy_fn) {
        if (!q->copy_fn(n->data, elem, q->ctx)) {
            free(n);
            return false;
        }
    } else {
        memcpy(n->data, elem, q->elem_size);
    }

    if (q->rear) q->rear->next = n;
    else q->front = n;
    q->rear = n;
    q->size++;
    return true;
}

static inline bool queue_peek(const Queue *q, void *out_elem) {
    if (!q || !q->front || !out_elem) return false;
    memcpy(out_elem, q->front->data, q->elem_size);
    return true;
}

static inline bool queue_pop(Queue *q, void *out_elem) {
    if (!q || !q->front) return false;

    QueueNode *n = q->front;
    q->front = n->next;
    if (!q->front) q->rear = NULL;

    if (out_elem) {
        if (q->move_out_fn) {
            if (!q->move_out_fn(out_elem, n->data, q->ctx)) {
                if (q->dtor_fn) q->dtor_fn(n->data, q->ctx);
                free(n);
                q->size--;
                return false;
            }
        } else {
            memcpy(out_elem, n->data, q->elem_size);
        }
    } else {
        if (q->dtor_fn) q->dtor_fn(n->data, q->ctx);
    }

    free(n);
    q->size--;
    return true;
}

static inline void queue_clear(Queue *q) {
    if (!q) return;
    QueueNode *cur = q->front;
    while (cur) {
        QueueNode *next = cur->next;
        if (q->dtor_fn) q->dtor_fn(cur->data, q->ctx);
        free(cur);
        cur = next;
    }
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
}

static inline void queue_free(Queue *q) {
    if (!q) return;
    queue_clear(q);
    queue_reset(q);
}

static inline bool queue_copy_cstr(void *dst_elem, const void *src_elem, void *ctx) {
    (void)ctx;
    const char *s = *(const char *const *)src_elem;
    char *dup = NULL;
    if (s) {
        size_t n = strlen(s) + 1;
        dup = (char *)malloc(n);
        if (!dup) return false;
        memcpy(dup, s, n);
    }
    *(char **)dst_elem = dup;
    return true;
}

static inline bool queue_move_out_cstr(void *out_elem, void *src_elem, void *ctx) {
    (void)ctx;
    *(char **)out_elem = *(char **)src_elem;
    *(char **)src_elem = NULL;
    return true;
}

static inline void queue_dtor_cstr(void *elem, void *ctx) {
    (void)ctx;
    free(*(char **)elem);
    *(char **)elem = NULL;
}

// Typed wrappers for Queue 

#define QUEUE_DEFINE_PLAIN_TYPE(NAME, TYPE)                                      \
typedef struct {                                                                  \
    Queue q;                                                                      \
} Queue_##NAME;                                                                   \
                                                                                  \
static inline bool queue_##NAME##_init(Queue_##NAME *qq) {                       \
    return qq && queue_init_plain(&qq->q, sizeof(TYPE));                         \
}                                                                                 \
                                                                                  \
static inline bool queue_##NAME##_push(Queue_##NAME *qq, TYPE value) {           \
    return qq && queue_push(&qq->q, &value);                                     \
}                                                                                 \
                                                                                  \
static inline bool queue_##NAME##_peek(const Queue_##NAME *qq, TYPE *out) {      \
    return qq && out && queue_peek(&qq->q, out);                                 \
}                                                                                 \
                                                                                  \
static inline bool queue_##NAME##_pop(Queue_##NAME *qq, TYPE *out) {             \
    return qq && out && queue_pop(&qq->q, out);                                  \
}                                                                                 \
                                                                                  \
static inline bool queue_##NAME##_pop_discard(Queue_##NAME *qq) {                \
    return qq && queue_pop(&qq->q, NULL);                                        \
}                                                                                 \
                                                                                  \
static inline size_t queue_##NAME##_size(const Queue_##NAME *qq) {               \
    return qq ? queue_size(&qq->q) : 0;                                          \
}                                                                                 \
                                                                                  \
static inline bool queue_##NAME##_is_empty(const Queue_##NAME *qq) {             \
    return !qq || queue_is_empty(&qq->q);                                        \
}                                                                                 \
                                                                                  \
static inline void queue_##NAME##_clear(Queue_##NAME *qq) {                      \
    if (qq) queue_clear(&qq->q);                                                 \
}                                                                                 \
                                                                                  \
static inline void queue_##NAME##_free(Queue_##NAME *qq) {                       \
    if (qq) queue_free(&qq->q);                                                  \
}

/* Generate wrappers for plain scalar types */
QUEUE_DEFINE_PLAIN_TYPE(int, int)
QUEUE_DEFINE_PLAIN_TYPE(char, char)
QUEUE_DEFINE_PLAIN_TYPE(float, float)
QUEUE_DEFINE_PLAIN_TYPE(double, double)

// String queue  
typedef struct {
    Queue q;
} Queue_cstr;

static inline bool queue_cstr_init(Queue_cstr *qq) {
    return qq && queue_init(
        &qq->q,
        sizeof(char *),
        queue_copy_cstr,
        queue_move_out_cstr,
        queue_dtor_cstr,
        NULL
    );
}

static inline bool queue_cstr_push(Queue_cstr *qq, const char *s) {
    return qq && queue_push(&qq->q, &s);
}

/* Peek returns a borrowed pointer owned by the queue node */
static inline bool queue_cstr_peek(const Queue_cstr *qq, const char **out) {
    return qq && out && queue_peek(&qq->q, out);
}

/* Pop transfers ownership of the string to caller; caller must free(*out) */
static inline bool queue_cstr_pop(Queue_cstr *qq, char **out) {
    return qq && out && queue_pop(&qq->q, out);
}

static inline bool queue_cstr_pop_discard(Queue_cstr *qq) {
    return qq && queue_pop(&qq->q, NULL);
}

static inline size_t queue_cstr_size(const Queue_cstr *qq) {
    return qq ? queue_size(&qq->q) : 0;
}

static inline bool queue_cstr_is_empty(const Queue_cstr *qq) {
    return !qq || queue_is_empty(&qq->q);
}

static inline void queue_cstr_clear(Queue_cstr *qq) {
    if (qq) queue_clear(&qq->q);
}

static inline void queue_cstr_free(Queue_cstr *qq) {
    if (qq) queue_free(&qq->q);
}

/* queue(type) facade over typed wrappers */

#define queue(type) Queue_##type

#define queue_init_t(type, qptr)            queue_##type##_init((qptr))
#define queue_push_t(type, qptr, value)     queue_##type##_push((qptr), (value))
#define queue_peek_t(type, qptr, out_ptr)   queue_##type##_peek((qptr), (out_ptr))
#define queue_pop_t(type, qptr, out_ptr)    queue_##type##_pop((qptr), (out_ptr))
#define queue_drop_t(type, qptr)            queue_##type##_pop_discard((qptr))
#define queue_size_t(type, qptr)            queue_##type##_size((qptr))
#define queue_empty_t(type, qptr)           queue_##type##_is_empty((qptr))
#define queue_clear_t(type, qptr)           queue_##type##_clear((qptr))
#define queue_free_t(type, qptr)            queue_##type##_free((qptr))

/* _Generic auto-dispatch facade */

#define queue_init(qptr) \
    _Generic((qptr), \
        Queue_int*:    queue_int_init, \
        Queue_char*:   queue_char_init, \
        Queue_float*:  queue_float_init, \
        Queue_double*: queue_double_init, \
        Queue_cstr*:   queue_cstr_init \
    )((qptr))

#define queue_push(qptr, value) \
    _Generic((qptr), \
        Queue_int*:    queue_int_push, \
        Queue_char*:   queue_char_push, \
        Queue_float*:  queue_float_push, \
        Queue_double*: queue_double_push, \
        Queue_cstr*:   queue_cstr_push \
    )((qptr), (value))

#define queue_peek(qptr, out_ptr) \
    _Generic((qptr), \
        Queue_int*:          queue_int_peek, \
        const Queue_int*:    queue_int_peek, \
        Queue_char*:         queue_char_peek, \
        const Queue_char*:   queue_char_peek, \
        Queue_float*:        queue_float_peek, \
        const Queue_float*:  queue_float_peek, \
        Queue_double*:       queue_double_peek, \
        const Queue_double*: queue_double_peek, \
        Queue_cstr*:         queue_cstr_peek, \
        const Queue_cstr*:   queue_cstr_peek \
    )((qptr), (out_ptr))

#define queue_pop(qptr, out_ptr) \
    _Generic((qptr), \
        Queue_int*:    queue_int_pop, \
        Queue_char*:   queue_char_pop, \
        Queue_float*:  queue_float_pop, \
        Queue_double*: queue_double_pop, \
        Queue_cstr*:   queue_cstr_pop \
    )((qptr), (out_ptr))

#define queue_drop(qptr) \
    _Generic((qptr), \
        Queue_int*:    queue_int_pop_discard, \
        Queue_char*:   queue_char_pop_discard, \
        Queue_float*:  queue_float_pop_discard, \
        Queue_double*: queue_double_pop_discard, \
        Queue_cstr*:   queue_cstr_pop_discard \
    )((qptr))

#define queue_size(qptr) \
    _Generic((qptr), \
        Queue_int*:          queue_int_size, \
        const Queue_int*:    queue_int_size, \
        Queue_char*:         queue_char_size, \
        const Queue_char*:   queue_char_size, \
        Queue_float*:        queue_float_size, \
        const Queue_float*:  queue_float_size, \
        Queue_double*:       queue_double_size, \
        const Queue_double*: queue_double_size, \
        Queue_cstr*:         queue_cstr_size, \
        const Queue_cstr*:   queue_cstr_size \
    )((qptr))

#define queue_empty(qptr) \
    _Generic((qptr), \
        Queue_int*:          queue_int_is_empty, \
        const Queue_int*:    queue_int_is_empty, \
        Queue_char*:         queue_char_is_empty, \
        const Queue_char*:   queue_char_is_empty, \
        Queue_float*:        queue_float_is_empty, \
        const Queue_float*:  queue_float_is_empty, \
        Queue_double*:       queue_double_is_empty, \
        const Queue_double*: queue_double_is_empty, \
        Queue_cstr*:         queue_cstr_is_empty, \
        const Queue_cstr*:   queue_cstr_is_empty \
    )((qptr))

#define queue_clear(qptr) \
    _Generic((qptr), \
        Queue_int*:    queue_int_clear, \
        Queue_char*:   queue_char_clear, \
        Queue_float*:  queue_float_clear, \
        Queue_double*: queue_double_clear, \
        Queue_cstr*:   queue_cstr_clear \
    )((qptr))

#define queue_free(qptr) \
    _Generic((qptr), \
        Queue_int*:    queue_int_free, \
        Queue_char*:   queue_char_free, \
        Queue_float*:  queue_float_free, \
        Queue_double*: queue_double_free, \
        Queue_cstr*:   queue_cstr_free \
    )((qptr))

#endif