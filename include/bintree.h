#ifndef BINTREE_H
#define BINTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Return <0 if a<b, 0 if equal, >0 if a>b */
typedef int  (*bt_compare_fn)(const void *a, const void *b, void *ctx);
/* Copy src element into dst storage; return false on failure */
typedef bool (*bt_copy_fn)(void *dst_elem, const void *src_elem, void *ctx);
/* Destroy one stored element */
typedef void (*bt_dtor_fn)(void *elem, void *ctx);

typedef struct BTNode {
    struct BTNode *left;
    struct BTNode *right;
    unsigned char data[];
} BTNode;

typedef struct BTree {
    BTNode *root;
    size_t size;
    size_t elem_size;
    bt_compare_fn cmp;
    bt_copy_fn copy_fn;   /* if NULL, memcpy is used */
    bt_dtor_fn dtor_fn;   /* optional */
    void *ctx;
} BTree;

typedef bool (*bt_visit_fn)(const void *elem, void *ctx);

static inline void bt_reset(BTree *t) {
    if (!t) return;
    t->root = NULL;
    t->size = 0;
    t->elem_size = 0;
    t->cmp = NULL;
    t->copy_fn = NULL;
    t->dtor_fn = NULL;
    t->ctx = NULL;
}

static inline bool bt_init(BTree *t, size_t elem_size, bt_compare_fn cmp,
                           bt_copy_fn copy_fn, bt_dtor_fn dtor_fn, void *ctx) {
    if (!t || elem_size == 0 || !cmp) return false;
    bt_reset(t);
    t->elem_size = elem_size;
    t->cmp = cmp;
    t->copy_fn = copy_fn;
    t->dtor_fn = dtor_fn;
    t->ctx = ctx;
    return true;
}

static inline bool bt_init_plain(BTree *t, size_t elem_size, bt_compare_fn cmp) {
    return bt_init(t, elem_size, cmp, NULL, NULL, NULL);
}

static inline bool bt_is_empty(const BTree *t) {
    return (!t || t->size == 0);
}

static inline size_t bt_size(const BTree *t) {
    return t ? t->size : 0;
}

static inline BTNode *bt_make_node(const BTree *t, const void *elem) {
    if (!t || !elem) return NULL;
    if (t->elem_size > (SIZE_MAX - sizeof(BTNode))) return NULL;

    BTNode *n = (BTNode *)malloc(sizeof(BTNode) + t->elem_size);
    if (!n) return NULL;

    n->left = NULL;
    n->right = NULL;

    if (t->copy_fn) {
        if (!t->copy_fn(n->data, elem, t->ctx)) {
            free(n);
            return NULL;
        }
    } else {
        memcpy(n->data, elem, t->elem_size);
    }
    return n;
}

static inline bool bt_insert(BTree *t, const void *elem) {
    if (!t || !elem || !t->cmp || t->elem_size == 0) return false;

    BTNode **link = &t->root;
    while (*link) {
        int c = t->cmp(elem, (*link)->data, t->ctx);
        if (c == 0) return false; /* reject duplicates */
        link = (c < 0) ? &(*link)->left : &(*link)->right;
    }

    BTNode *n = bt_make_node(t, elem);
    if (!n) return false;

    *link = n;
    t->size++;
    return true;
}

static inline const void *bt_find(const BTree *t, const void *key) {
    if (!t || !key || !t->cmp) return NULL;
    BTNode *cur = t->root;
    while (cur) {
        int c = t->cmp(key, cur->data, t->ctx);
        if (c == 0) return cur->data;
        cur = (c < 0) ? cur->left : cur->right;
    }
    return NULL;
}

static inline bool bt_contains(const BTree *t, const void *key) {
    return bt_find(t, key) != NULL;
}

static inline void bt_destroy_node(BTree *t, BTNode *n) {
    if (!n) return;
    bt_destroy_node(t, n->left);
    bt_destroy_node(t, n->right);
    if (t->dtor_fn) t->dtor_fn(n->data, t->ctx);
    free(n);
}

static inline void bt_clear(BTree *t) {
    if (!t) return;
    bt_destroy_node(t, t->root);
    t->root = NULL;
    t->size = 0;
}

static inline void bt_free(BTree *t) {
    if (!t) return;
    bt_clear(t);
    bt_reset(t);
}

static inline bool bt_inorder(const BTNode *n, bt_visit_fn visit, void *ctx) {
    if (!n || !visit) return true;
    if (!bt_inorder(n->left, visit, ctx)) return false;
    if (!visit(n->data, ctx)) return false;
    if (!bt_inorder(n->right, visit, ctx)) return false;
    return true;
}

static inline bool bt_foreach_inorder(const BTree *t, bt_visit_fn visit, void *ctx) {
    if (!t || !visit) return false;
    return bt_inorder(t->root, visit, ctx);
}

/* Optional c-string helpers (for trees storing char*) */
static inline bool bt_copy_cstr(void *dst_elem, const void *src_elem, void *ctx) {
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

static inline void bt_dtor_cstr(void *elem, void *ctx) {
    (void)ctx;
    free(*(char **)elem);
    *(char **)elem = NULL;
}

/* ---------- Typed wrappers + tree(type) facade ---------- */

static inline int bt_cmp_int(const void *a, const void *b, void *ctx) {
    (void)ctx;
    int aa = *(const int *)a;
    int bb = *(const int *)b;
    return (aa > bb) - (aa < bb);
}

static inline int bt_cmp_char(const void *a, const void *b, void *ctx) {
    (void)ctx;
    char aa = *(const char *)a;
    char bb = *(const char *)b;
    return (aa > bb) - (aa < bb);
}

static inline int bt_cmp_float(const void *a, const void *b, void *ctx) {
    (void)ctx;
    float aa = *(const float *)a;
    float bb = *(const float *)b;
    return (aa > bb) - (aa < bb);
}

static inline int bt_cmp_double(const void *a, const void *b, void *ctx) {
    (void)ctx;
    double aa = *(const double *)a;
    double bb = *(const double *)b;
    return (aa > bb) - (aa < bb);
}

static inline int bt_cmp_cstr(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    if (!sa && !sb) return 0;
    if (!sa) return -1;
    if (!sb) return 1;
    return strcmp(sa, sb);
}

#define BTREE_DEFINE_PLAIN_TYPE(NAME, TYPE, CMP_FN)                                      \
typedef struct {                                                                          \
    BTree t;                                                                              \
} BTree_##NAME;                                                                           \
                                                                                          \
static inline bool btree_##NAME##_init(BTree_##NAME *tt) {                               \
    return tt && bt_init_plain(&tt->t, sizeof(TYPE), (CMP_FN));                          \
}                                                                                         \
                                                                                          \
static inline bool btree_##NAME##_insert(BTree_##NAME *tt, TYPE value) {                 \
    return tt && bt_insert(&tt->t, &value);                                               \
}                                                                                         \
                                                                                          \
static inline bool btree_##NAME##_contains(const BTree_##NAME *tt, TYPE value) {         \
    return tt && bt_contains(&tt->t, &value);                                             \
}                                                                                         \
                                                                                          \
static inline const TYPE *btree_##NAME##_find_ptr(const BTree_##NAME *tt, TYPE value) {  \
    return tt ? (const TYPE *)bt_find(&tt->t, &value) : NULL;                            \
}                                                                                         \
                                                                                          \
static inline bool btree_##NAME##_find(const BTree_##NAME *tt, TYPE value, TYPE *out) {  \
    const TYPE *p = btree_##NAME##_find_ptr(tt, value);                                   \
    if (!p || !out) return false;                                                         \
    *out = *p;                                                                            \
    return true;                                                                          \
}                                                                                         \
                                                                                          \
static inline size_t btree_##NAME##_size(const BTree_##NAME *tt) {                       \
    return tt ? bt_size(&tt->t) : 0;                                                      \
}                                                                                         \
                                                                                          \
static inline bool btree_##NAME##_is_empty(const BTree_##NAME *tt) {                     \
    return !tt || bt_is_empty(&tt->t);                                                    \
}                                                                                         \
                                                                                          \
static inline void btree_##NAME##_clear(BTree_##NAME *tt) {                              \
    if (tt) bt_clear(&tt->t);                                                             \
}                                                                                         \
                                                                                          \
static inline void btree_##NAME##_free(BTree_##NAME *tt) {                               \
    if (tt) bt_free(&tt->t);                                                              \
}

/* Plain scalar trees */
BTREE_DEFINE_PLAIN_TYPE(int, int, bt_cmp_int)
BTREE_DEFINE_PLAIN_TYPE(char, char, bt_cmp_char)
BTREE_DEFINE_PLAIN_TYPE(float, float, bt_cmp_float)
BTREE_DEFINE_PLAIN_TYPE(double, double, bt_cmp_double)

/* C-string tree (owning) */
typedef struct {
    BTree t;
} BTree_cstr;

static inline bool btree_cstr_init(BTree_cstr *tt) {
    return tt && bt_init(
        &tt->t,
        sizeof(char *),
        bt_cmp_cstr,
        bt_copy_cstr,
        bt_dtor_cstr,
        NULL
    );
}

static inline bool btree_cstr_insert(BTree_cstr *tt, const char *s) {
    return tt && bt_insert(&tt->t, &s);
}

static inline bool btree_cstr_contains(const BTree_cstr *tt, const char *s) {
    return tt && bt_contains(&tt->t, &s);
}

/* Returns borrowed pointer owned by the tree */
static inline const char *btree_cstr_find_ptr(const BTree_cstr *tt, const char *s) {
    const char *const *p = tt ? (const char *const *)bt_find(&tt->t, &s) : NULL;
    return p ? *p : NULL;
}

static inline bool btree_cstr_find(const BTree_cstr *tt, const char *s, const char **out) {
    const char *p = btree_cstr_find_ptr(tt, s);
    if (!p || !out) return false;
    *out = p;
    return true;
}

static inline size_t btree_cstr_size(const BTree_cstr *tt) {
    return tt ? bt_size(&tt->t) : 0;
}

static inline bool btree_cstr_is_empty(const BTree_cstr *tt) {
    return !tt || bt_is_empty(&tt->t);
}

static inline void btree_cstr_clear(BTree_cstr *tt) {
    if (tt) bt_clear(&tt->t);
}

static inline void btree_cstr_free(BTree_cstr *tt) {
    if (tt) bt_free(&tt->t);
}

/* tree(type) facade */
#define tree(type) BTree_##type

#define tree_init_t(type, tptr)                  btree_##type##_init((tptr))
#define tree_insert_t(type, tptr, value)         btree_##type##_insert((tptr), (value))
#define tree_contains_t(type, tptr, key)         btree_##type##_contains((tptr), (key))
#define tree_find_t(type, tptr, key, out_ptr)    btree_##type##_find((tptr), (key), (out_ptr))
#define tree_size_t(type, tptr)                  btree_##type##_size((tptr))
#define tree_empty_t(type, tptr)                 btree_##type##_is_empty((tptr))
#define tree_clear_t(type, tptr)                 btree_##type##_clear((tptr))
#define tree_free_t(type, tptr)                  btree_##type##_free((tptr))

#endif