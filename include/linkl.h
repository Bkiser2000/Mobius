#ifndef LINKL_H
#define LINKL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef bool (*ll_copy_fn)(void *dst_elem, const void *src_elem, void *ctx);
typedef void (*ll_dtor_fn)(void *elem, void *ctx);
typedef bool (*ll_visit_fn)(const void *elem, size_t index, void *ctx);

typedef struct LLNode {
    struct LLNode *next;
    unsigned char data[];
} LLNode;

typedef struct LinkedList {
    LLNode *head;
    LLNode *tail;
    size_t size;
    size_t elem_size;
    ll_copy_fn copy_fn;
    ll_dtor_fn dtor_fn;
    void *ctx;
} LinkedList;

static inline void ll_reset(LinkedList *list) {
    if (!list) return;
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->elem_size = 0;
    list->copy_fn = NULL;
    list->dtor_fn = NULL;
    list->ctx = NULL;
}

static inline bool ll_init(LinkedList *list, size_t elem_size,
                           ll_copy_fn copy_fn,
                           ll_dtor_fn dtor_fn,
                           void *ctx) {
    if (!list || elem_size == 0) return false;
    ll_reset(list);
    list->elem_size = elem_size;
    list->copy_fn = copy_fn;
    list->dtor_fn = dtor_fn;
    list->ctx = ctx;
    return true;
}

static inline bool ll_init_plain(LinkedList *list, size_t elem_size) {
    return ll_init(list, elem_size, NULL, NULL, NULL);
}

static inline bool ll_is_empty(const LinkedList *list) {
    return (!list || list->size == 0);
}

static inline size_t ll_size(const LinkedList *list) {
    return list ? list->size : 0;
}

static inline LLNode *ll_make_node(const LinkedList *list, const void *elem) {
    if (!list || !elem || list->elem_size == 0) return NULL;
    if (list->elem_size > (SIZE_MAX - sizeof(LLNode))) return NULL;

    LLNode *node = (LLNode *)malloc(sizeof(LLNode) + list->elem_size);
    if (!node) return NULL;
    node->next = NULL;

    if (list->copy_fn) {
        if (!list->copy_fn(node->data, elem, list->ctx)) {
            free(node);
            return NULL;
        }
    } else {
        memcpy(node->data, elem, list->elem_size);
    }
    return node;
}

static inline bool ll_push_front(LinkedList *list, const void *elem) {
    if (!list || !elem) return false;
    LLNode *node = ll_make_node(list, elem);
    if (!node) return false;

    node->next = list->head;
    list->head = node;
    if (!list->tail) list->tail = node;
    list->size++;
    return true;
}

static inline bool ll_push_back(LinkedList *list, const void *elem) {
    if (!list || !elem) return false;
    LLNode *node = ll_make_node(list, elem);
    if (!node) return false;

    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->size++;
    return true;
}

static inline bool ll_insert_at(LinkedList *list, size_t index, const void *elem) {
    if (!list || !elem || index > list->size) return false;
    if (index == 0) return ll_push_front(list, elem);
    if (index == list->size) return ll_push_back(list, elem);

    LLNode *node = ll_make_node(list, elem);
    if (!node) return false;

    LLNode *prev = list->head;
    for (size_t i = 0; i + 1 < index; i++) prev = prev->next;

    node->next = prev->next;
    prev->next = node;
    list->size++;
    return true;
}

static inline bool ll_pop_front(LinkedList *list, void *out_elem) {
    if (!list || !list->head) return false;

    LLNode *node = list->head;
    list->head = node->next;
    if (!list->head) list->tail = NULL;

    if (out_elem) memcpy(out_elem, node->data, list->elem_size);
    if (list->dtor_fn) list->dtor_fn(node->data, list->ctx);
    free(node);
    list->size--;
    return true;
}

static inline bool ll_remove_at(LinkedList *list, size_t index, void *out_elem) {
    if (!list || index >= list->size) return false;
    if (index == 0) return ll_pop_front(list, out_elem);

    LLNode *prev = list->head;
    for (size_t i = 0; i + 1 < index; i++) prev = prev->next;

    LLNode *node = prev->next;
    prev->next = node->next;
    if (node == list->tail) list->tail = prev;

    if (out_elem) memcpy(out_elem, node->data, list->elem_size);
    if (list->dtor_fn) list->dtor_fn(node->data, list->ctx);
    free(node);
    list->size--;
    return true;
}

static inline bool ll_at(const LinkedList *list, size_t index, void *out_elem) {
    if (!list || !out_elem || index >= list->size) return false;
    LLNode *cur = list->head;
    for (size_t i = 0; i < index; i++) cur = cur->next;
    memcpy(out_elem, cur->data, list->elem_size);
    return true;
}

static inline bool ll_for_each(const LinkedList *list, ll_visit_fn visit, void *ctx) {
    if (!list || !visit) return false;
    LLNode *cur = list->head;
    size_t index = 0;
    while (cur) {
        if (!visit(cur->data, index, ctx)) return false;
        cur = cur->next;
        index++;
    }
    return true;
}

static inline bool ll_for_each_recursive_node(const LLNode *node, size_t index,
                                              ll_visit_fn visit, void *ctx) {
    if (!node) return true;
    if (!visit(node->data, index, ctx)) return false;
    return ll_for_each_recursive_node(node->next, index + 1, visit, ctx);
}

static inline bool ll_for_each_recursive(const LinkedList *list, ll_visit_fn visit, void *ctx) {
    if (!list || !visit) return false;
    return ll_for_each_recursive_node(list->head, 0, visit, ctx);
}

static inline bool ll_for_each_reverse_node(const LLNode *node, size_t index,
                                            ll_visit_fn visit, void *ctx) {
    if (!node) return true;
    if (!ll_for_each_reverse_node(node->next, index + 1, visit, ctx)) return false;
    return visit(node->data, index, ctx);
}

static inline bool ll_for_each_reverse(const LinkedList *list, ll_visit_fn visit, void *ctx) {
    if (!list || !visit) return false;
    return ll_for_each_reverse_node(list->head, 0, visit, ctx);
}

static inline void ll_reverse(LinkedList *list) {
    if (!list || !list->head || !list->head->next) return;
    LLNode *prev = NULL;
    LLNode *cur = list->head;
    LLNode *old_head = list->head;

    while (cur) {
        LLNode *next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }

    list->head = prev;
    list->tail = old_head;
}

static inline void ll_clear(LinkedList *list) {
    if (!list) return;
    LLNode *cur = list->head;
    while (cur) {
        LLNode *next = cur->next;
        if (list->dtor_fn) list->dtor_fn(cur->data, list->ctx);
        free(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

static inline void ll_free(LinkedList *list) {
    if (!list) return;
    ll_clear(list);
    ll_reset(list);
}

static inline bool ll_copy_cstr(void *dst_elem, const void *src_elem, void *ctx) {
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

static inline void ll_dtor_cstr(void *elem, void *ctx) {
    (void)ctx;
    free(*(char **)elem);
    *(char **)elem = NULL;
}

#endif
