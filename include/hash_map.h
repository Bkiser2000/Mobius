#ifndef HASH_MAP_H
#define HASH_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t (*hm_hash_fn)(const void *key, size_t key_size, void *ctx);
typedef bool (*hm_eq_fn)(const void *a, const void *b, size_t key_size, void *ctx);
typedef bool (*hm_copy_fn)(void *dst, const void *src, void *ctx);
typedef void (*hm_dtor_fn)(void *elem, void *ctx);

typedef struct HMEntry {
  struct HMEntry *next;
  uint64_t hash;
  unsigned char data[];
} HMEntry;

typedef struct HashMap {
  HMEntry **buckets;
  size_t bucket_count;
  size_t size;
  size_t key_size;
  size_t val_size;
  float max_load;
  hm_hash_fn hash_fn;
  hm_eq_fn eq_fn;
  hm_copy_fn key_copy;
  hm_copy_fn val_copy;
  hm_dtor_fn key_dtor;
  hm_dtor_fn val_dtor;
  void *ctx;
} HashMap;

static inline void *hm_key_ptr(const HashMap *m, HMEntry *e) {
  (void)m;
  return e->data;
}

static inline void *hm_val_ptr(const HashMap *m, HMEntry *e) {
  return e->data + m->key_size;
}

static inline bool hm_copy_elem(void *dst, const void *src, hm_copy_fn copy_fn, void *ctx, size_t size) {
  if (copy_fn) return copy_fn(dst, src, ctx);
  memcpy(dst, src, size);
  return true;
}

static inline void hm_dtor_elem(void *elem, hm_dtor_fn dtor_fn, void *ctx) {
  if (dtor_fn) dtor_fn(elem, ctx);
}

static inline void hm_reset(HashMap *m) {
  if (!m) return;
  m->buckets = NULL;
  m->bucket_count = 0;
  m->size = 0;
  m->key_size = 0;
  m->val_size = 0;
  m->max_load = 0.75f;
  m->hash_fn = NULL;
  m->eq_fn = NULL;
  m->key_copy = NULL;
  m->val_copy = NULL;
  m->key_dtor = NULL;
  m->val_dtor = NULL;
  m->ctx = NULL;
}

static inline bool hm_init(HashMap *m, size_t key_size, size_t val_size, size_t initial_buckets,
                           hm_hash_fn hash_fn, hm_eq_fn eq_fn,
                           hm_copy_fn key_copy, hm_copy_fn val_copy,
                           hm_dtor_fn key_dtor, hm_dtor_fn val_dtor, void *ctx) {
  if (!m || key_size == 0 || val_size == 0 || !hash_fn || !eq_fn) return false;
  if (initial_buckets == 0) initial_buckets = 16;

  hm_reset(m);
  m->buckets = (HMEntry **)calloc(initial_buckets, sizeof(HMEntry *));
  if (!m->buckets) return false;

  m->bucket_count = initial_buckets;
  m->key_size = key_size;
  m->val_size = val_size;
  m->hash_fn = hash_fn;
  m->eq_fn = eq_fn;
  m->key_copy = key_copy;
  m->val_copy = val_copy;
  m->key_dtor = key_dtor;
  m->val_dtor = val_dtor;
  m->ctx = ctx;
  return true;
}

static inline size_t hm_size(const HashMap *m) {
  return m ? m->size : 0;
}

static inline bool hm_is_empty(const HashMap *m) {
  return !m || m->size == 0;
}

static inline bool hm_rehash(HashMap *m, size_t new_bucket_count) {
  if (!m || new_bucket_count == 0) return false;
  HMEntry **new_buckets = (HMEntry **)calloc(new_bucket_count, sizeof(HMEntry *));
  if (!new_buckets) return false;

  for (size_t i = 0; i < m->bucket_count; i++) {
    HMEntry *cur = m->buckets[i];
    while (cur) {
      HMEntry *next = cur->next;
      size_t ni = (size_t)(cur->hash % new_bucket_count);
      cur->next = new_buckets[ni];
      new_buckets[ni] = cur;
      cur = next;
    }
  }

  free(m->buckets);
  m->buckets = new_buckets;
  m->bucket_count = new_bucket_count;
  return true;
}

static inline bool hm_maybe_grow(HashMap *m) {
  if (!m || m->bucket_count == 0) return false;
  float lf = (float)(m->size + 1) / (float)m->bucket_count;
  if (lf <= m->max_load) return true;
  if (m->bucket_count > (SIZE_MAX / 2)) return false;
  return hm_rehash(m, m->bucket_count * 2);
}

static inline HMEntry *hm_find_entry(HashMap *m, const void *key, uint64_t hash, size_t *bucket_idx_out) {
  size_t idx = (size_t)(hash % m->bucket_count);
  if (bucket_idx_out) *bucket_idx_out = idx;
  HMEntry *cur = m->buckets[idx];
  while (cur) {
    if (cur->hash == hash && m->eq_fn(hm_key_ptr(m, cur), key, m->key_size, m->ctx)) return cur;
    cur = cur->next;
  }
  return NULL;
}

static inline bool hm_put(HashMap *m, const void *key, const void *value) {
  if (!m || !m->buckets || !key || !value) return false;
  if (!hm_maybe_grow(m)) return false;

  uint64_t h = m->hash_fn(key, m->key_size, m->ctx);
  size_t idx = 0;
  HMEntry *e = hm_find_entry(m, key, h, &idx);

  if (e) {
    void *tmp = malloc(m->val_size);
    if (!tmp) return false;
    if (!hm_copy_elem(tmp, value, m->val_copy, m->ctx, m->val_size)) {
      free(tmp);
      return false;
    }
    hm_dtor_elem(hm_val_ptr(m, e), m->val_dtor, m->ctx);
    memcpy(hm_val_ptr(m, e), tmp, m->val_size);
    free(tmp);
    return true;
  }

  if (m->key_size > SIZE_MAX - m->val_size) return false;
  size_t kv_size = m->key_size + m->val_size;
  if (kv_size > SIZE_MAX - sizeof(HMEntry)) return false;

  HMEntry *n = (HMEntry *)malloc(sizeof(HMEntry) + kv_size);
  if (!n) return false;
  n->next = NULL;
  n->hash = h;

  void *kdst = hm_key_ptr(m, n);
  void *vdst = hm_val_ptr(m, n);

  if (!hm_copy_elem(kdst, key, m->key_copy, m->ctx, m->key_size)) {
    free(n);
    return false;
  }
  if (!hm_copy_elem(vdst, value, m->val_copy, m->ctx, m->val_size)) {
    hm_dtor_elem(kdst, m->key_dtor, m->ctx);
    free(n);
    return false;
  }

  n->next = m->buckets[idx];
  m->buckets[idx] = n;
  m->size++;
  return true;
}

static inline const void *hm_get(const HashMap *m, const void *key) {
  if (!m || !m->buckets || !key) return NULL;
  uint64_t h = m->hash_fn(key, m->key_size, m->ctx);
  size_t idx = (size_t)(h % m->bucket_count);
  HMEntry *cur = m->buckets[idx];
  while (cur) {
    if (cur->hash == h && m->eq_fn(hm_key_ptr(m, cur), key, m->key_size, m->ctx)) {
      return hm_val_ptr(m, cur);
    }
    cur = cur->next;
  }
  return NULL;
}

static inline bool hm_remove(HashMap *m, const void *key) {
  if (!m || !m->buckets || !key) return false;
  uint64_t h = m->hash_fn(key, m->key_size, m->ctx);
  size_t idx = (size_t)(h % m->bucket_count);
  HMEntry *cur = m->buckets[idx];
  HMEntry *prev = NULL;

  while (cur) {
    if (cur->hash == h && m->eq_fn(hm_key_ptr(m, cur), key, m->key_size, m->ctx)) {
      if (prev) prev->next = cur->next;
      else m->buckets[idx] = cur->next;

      hm_dtor_elem(hm_key_ptr(m, cur), m->key_dtor, m->ctx);
      hm_dtor_elem(hm_val_ptr(m, cur), m->val_dtor, m->ctx);
      free(cur);
      m->size--;
      return true;
    }
    prev = cur;
    cur = cur->next;
  }
  return false;
}

static inline void hm_clear(HashMap *m) {
  if (!m || !m->buckets) return;
  for (size_t i = 0; i < m->bucket_count; i++) {
    HMEntry *cur = m->buckets[i];
    while (cur) {
      HMEntry *next = cur->next;
      hm_dtor_elem(hm_key_ptr(m, cur), m->key_dtor, m->ctx);
      hm_dtor_elem(hm_val_ptr(m, cur), m->val_dtor, m->ctx);
      free(cur);
      cur = next;
    }
    m->buckets[i] = NULL;
  }
  m->size = 0;
}

static inline void hm_free(HashMap *m) {
  if (!m) return;
  hm_clear(m);
  free(m->buckets);
  hm_reset(m);
}

static inline uint64_t hm_hash_bytes(const void *data, size_t n, void *ctx) {
  (void)ctx;
  const unsigned char *p = (const unsigned char *)data;
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint64_t)p[i];
    h *= 1099511628211ull;
  }
  return h;
}

static inline bool hm_eq_bytes(const void *a, const void *b, size_t n, void *ctx) {
  (void)ctx;
  return memcmp(a, b, n) == 0;
}

static inline bool hm_copy_cstr(void *dst, const void *src, void *ctx) {
  (void)ctx;
  const char *s = *(const char *const *)src;
  char *dup = NULL;
  if (s) {
    size_t n = strlen(s) + 1;
    dup = (char *)malloc(n);
    if (!dup) return false;
    memcpy(dup, s, n);
  }
  *(char **)dst = dup;
  return true;
}

static inline void hm_dtor_cstr(void *elem, void *ctx) {
  (void)ctx;
  free(*(char **)elem);
  *(char **)elem = NULL;
}

static inline uint64_t hm_hash_cstr_key(const void *key, size_t key_size, void *ctx) {
  (void)key_size;
  (void)ctx;
  const char *s = *(const char *const *)key;
  if (!s) return 0;
  return hm_hash_bytes(s, strlen(s), NULL);
}

static inline bool hm_eq_cstr_key(const void *a, const void *b, size_t key_size, void *ctx) {
  (void)key_size;
  (void)ctx;
  const char *sa = *(const char *const *)a;
  const char *sb = *(const char *const *)b;
  if (!sa && !sb) return true;
  if (!sa || !sb) return false;
  return strcmp(sa, sb) == 0;
}

#endif