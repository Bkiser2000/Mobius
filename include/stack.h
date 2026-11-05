#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
//#include "vector.h"

typedef enum {
    STACK_VAL_INT,
    STACK_VAL_FLOAT,
    STACK_VAL_DOUBLE,
    STACK_VAL_CHAR,
    STACK_VAL_STRING,
    STACK_VAL_BOOL
} StackValueKind;

typedef struct StackNode {
    StackValueKind kind;
    union {
    	int i;
    	float f;
    	double d;
    	char c;
    	char *s;
	bool b;
    } value;
    struct StackNode *next;
} StackNode;

typedef struct Stack {
    StackNode *head;
    size_t size;
} Stack;

// Create/Destroy/Clear Stack:

static inline Stack *create_stack(void) {
    Stack *s = (Stack *)malloc(sizeof(*s));
    if (!s) return NULL;
    s->head = NULL;
    s->size = 0;
    return s;
}

static inline char *stack_strdup(const char *src) {
    if (!src) src = "";
    size_t n = strlen(src) + 1;
    char *dst = (char *)malloc(n);
    if (!dst) return NULL;
    memcpy(dst, src, n);
    return dst;
}

static inline void clear_stack(Stack *s) {
    if (!s) return;
    StackNode *cur = s->head;
    while (cur) {
	StackNode *next = cur->next;

	if (cur->kind == STACK_VAL_STRING) {
	   free(cur->value.s);
	   cur->value.s = NULL;
	}

	free(cur);
	cur = next;
    }
    s->head = NULL;
    s->size = 0;
}

static inline void destroy_stack(Stack *s) {
    if (!s) return;
    clear_stack(s);
    free(s);
}

// Stack Operations:

static inline bool push_stack_char(Stack *s, char value) {
    if (!s) return false;
    StackNode *n = (StackNode *)malloc(sizeof(*n));
    if (!n) return false;
    n->kind = STACK_VAL_CHAR;
    n->value.c = value;
    n->next = s->head;
    s->head = n;
    s->size++;
    return true;
}

static inline bool pop_stack_char(Stack *s, char *out_value) {
    if (!s || s->size == 0) return false;
    StackNode *top = s->head;
    s->head = top->next;
    if (out_value) *out_value = top->value.c;
    free(top);
    s->size--;
    return true;
}

static inline bool peek_stack_char(const Stack *s, char *out_value) {
    if (!s || s->size == 0) return false;
    if (out_value) *out_value = s->head->value.c;
    return true;
}

static inline bool push_stack_bool(Stack *s, bool value) {
    if (!s) return false;
    StackNode *n = (StackNode *)malloc(sizeof(*n));
    if (!n) return false;
    n->kind = STACK_VAL_BOOL;
    n->value.b = value;
    n->next = s->head;
    s->head = n;
    s->size++;
    return true;
}

static inline bool pop_stack_bool(Stack *s, bool *out_value) {
    if (!s || s->size == 0) return false;
    StackNode *top = s->head;
    s->head = top->next;
    if (out_value) *out_value = top->value.b;
    free(top);
    s->size--;
    return true;
}

static inline bool peek_stack_bool(const Stack *s, bool *out_value) {
    if (!s || s->size == 0) return false;
    if (out_value) *out_value = s->head->value.b;
    return true;
}

static inline bool push_stack_int(Stack *s, int value) {
    if (!s) return false;
    StackNode *n = (StackNode *)malloc(sizeof(*n));
    if (!n) return false;
    n->kind = STACK_VAL_INT;
    n->value.i = value;
    n->next = s->head;
    s->head = n;
    s->size++;
    return true;
}

static inline bool pop_stack_int(Stack *s, int *out_value) {
    if (!s || s->size == 0) return false;
    StackNode *top = s->head;
    s->head = top->next;
    if (out_value) *out_value = top->value.i;
    free(top);
    s->size--;
    return true;
}

static inline bool peek_stack_int(const Stack *s, int *out_value) {
    if (!s || s->size == 0) return false;
    if (out_value) *out_value = s->head->value.i;
    return true;
}

static inline bool push_stack_float(Stack *s, float value) {
    if (!s) return false;
    StackNode *n = (StackNode *)malloc(sizeof(*n));
    if (!n) return false;
    n->kind = STACK_VAL_FLOAT;
    n->value.f = value;
    n->next = s->head;
    s->head = n;
    s->size++;
    return true;
}

static inline bool pop_stack_float(Stack *s, float *out_value) {
    if (!s || s->size == 0) return false;
    StackNode *top = s->head;
    s->head = top->next;
    if (out_value) *out_value = top->value.f;
    free(top);
    s->size--;
    return true;
}

static inline bool peek_stack_float(const Stack *s, float *out_value) {
    if (!s || s->size == 0) return false;
    if (out_value) *out_value = s->head->value.f;
    return true;
}

static inline bool push_stack_double(Stack *s, double value) {
    if (!s) return false;
    StackNode *n = (StackNode *)malloc(sizeof(*n));
    if (!n) return false;
    n->kind = STACK_VAL_DOUBLE;
    n->value.d = value;
    n->next = s->head;
    s->head = n;
    s->size++;
    return true;
}

static inline bool pop_stack_double(Stack *s, double *out_value) {
    if (!s || s->size == 0) return false;
    StackNode *top = s->head;
    if (top->kind != STACK_VAL_DOUBLE) return false;
    s->head = top->next;
    if (out_value) *out_value = top->value.d;
    free(top);
    s->size--;
    return true;
}

static inline bool peek_stack_double(Stack *s, double *out_value) {
    if (!s || s->size == 0) return false;
    if (out_value) *out_value = s->head->value.d;
    return true;
}

static inline bool push_stack_string(Stack* s, const char *value) {
    if (!s) return false;
    StackNode *n = (StackNode *)malloc(sizeof(*n));
    if (!n) return false;

    char *owned = stack_strdup(value);
    if (!owned) {
	free(n);
	return false;
    }

    n->kind = STACK_VAL_STRING;
    n->value.s = owned;
    n->next = s->head;
    s->head = n;
    s->size++;
    return true;
}

static inline bool pop_stack_string(Stack *s, char **out_value) {
    if (!s || s->size == 0) return false;
    StackNode *top = s->head;
    if (top->kind != STACK_VAL_STRING) return false;

    s->head = top->next;
    if (out_value) {
	*out_value = top->value.s;
    } else {
	free(top->value.s);
    }

    free(top);
    s->size--;
    return true;
}

static inline bool peek_stack_string(const Stack *s, const char **out_value) {
    if (!s || s->size == 0 || !out_value) return false;
    *out_value = s->head->value.s;
    return true;
}

// Stack Queries:

static bool stack_is_empty(const Stack *s) {
    return !s || s->size == 0;
}

static inline size_t stack_size(const Stack *s) {
    return s ? s->size : 0u;
}

typedef Stack *stack_char;
typedef Stack *stack_int;
typedef Stack *stack_float;
typedef Stack *stack_double;
typedef Stack *stack_string;
typedef Stack *stack_bool;

#define stack(T) stack_##T

#define svalues(T, ...) values_##T(__VA_ARGS__)

#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))

#define svalues_arr(T, arr) \ 
        values_##T##_from_array((arr), \
        COUNT_OF(arr))

static inline stack_char values_char_from_array(const char *arr, size_t n) {
    stack_char s = create_stack();
    if (!s) return NULL;

    for (size_t i = 0; i < n; ++i) {
        if (!push_stack_char(s, arr[i])) {
            destroy_stack(s);
            return NULL;
        }
    }
    return s;
}

static inline stack_string values_string_from_array(const char *arr[], size_t n) {
    stack_string s = create_stack();
    if (!s) return NULL;

    for (size_t i = 0; i < n; ++i) {
	if (!push_stack_string(s, arr[i])) {
	    destroy_stack(s);
	    return NULL;
	}
    }
    return s;
}

static inline stack_int values_int_from_array(const int *arr, size_t n) {
    stack_int s = create_stack();
    if (!s) return NULL;

    for (size_t i = 0; i < n; ++i) {
	if (!push_stack_int(s, arr[i])) {
	    destroy_stack(s);
	    return NULL;
	}
    }
    return s;
}

static inline stack_float values_float_from_array(const float *arr, size_t n) {
    stack_float s = create_stack();
    if (!s) return NULL;

    for (size_t i = 0; i < n; ++i) {
	if (!push_stack_float(s, arr[i])) {
	    destroy_stack(s);
	    return NULL;
	}
    }
    return s;
}

static inline stack_double values_double_from_array(const double *arr, size_t n) {
    stack_double s = create_stack();
    if (!s) return NULL;

    for (size_t i = 0; i < n; ++i) {
	if (!push_stack_double(s, arr[i])) {
	    destroy_stack(s);
	    return NULL;
	}
    }
    return s;
}

static inline stack_bool values_bool_from_array(const bool *arr, size_t n) {
    stack_bool s = create_stack();
    if (!s) return NULL;

    for (size_t i = 0; i < n; ++i) {
	if (!push_stack_bool(s, arr[i])) {
	    destroy_stack(s);
	    return NULL;
	}
    }
    return s;
}

#define values_char(...) \
    values_char_from_array((const char[]){__VA_ARGS__}, \
    sizeof((const char[]){__VA_ARGS__}) / sizeof(char))

#define values_int(...) \
    values_int_from_array((const int[]){__VA_ARGS__}, \
    sizeof((const int[]){__VA_ARGS__}) / sizeof(int))

#define values_float(...) \
    values_float_from_array((const float[]){__VA_ARGS__}, \
    sizeof((const float[]){__VA_ARGS__}) / sizeof(float))

#define values_double(...) \
    values_double_from_array((const double[]){__VA_ARGS__}, \
    sizeof((const double[]){__VA_ARGS__}) / sizeof(double))

#define stack_op(op, s, x) _Generic((x), \
    int: op##_stack_int, \
    float: op##_stack_float, \
    double: op##_stack_double, \
    char: op##_stack_char, \
    bool: op##_stack_bool,  \
    const char *: op##_stack_string, \
    int *: op##_stack_int, \
    bool *: op##_stack_bool, \
    float *: op##_stack_float, \
    double *: op##_stack_double, \
    char *: op##_stack_string, \
    char **: op##_stack_string \
)((s), (x))

#endif

