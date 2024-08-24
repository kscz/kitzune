#include <stdbool.h>
#include <string.h>

#include "strstack.h"

#define STRSTACK_INITIAL_STACK_SIZE 32
#define STRSTACK_INITIAL_ENDS_SIZE 4

typedef struct strstack {
    char *stack;
    size_t stack_size;
    size_t *ends;
    size_t ends_count, ends_size;
} strstack_t;

strstack_handle_t strstack_new(void) {
    strstack_t *s = calloc(1, sizeof(strstack_t));
    if (s == NULL) {
        return NULL;
    }
    s->stack_size = STRSTACK_INITIAL_STACK_SIZE;
    s->stack = malloc(sizeof(*s->stack) * s->stack_size);
    if (s->stack == NULL) {
        goto strstack_new_cleanup;
    }
    s->ends_size = STRSTACK_INITIAL_ENDS_SIZE;
    s->ends = malloc(sizeof(*s->ends) * s->ends_size);
    if (s->ends == NULL) {
        goto strstack_new_cleanup;
    }

    return s;

strstack_new_cleanup:
    free(s->ends);
    free(s->stack);
    free(s);

    return NULL;
}

static bool strstack_grow_stack(strstack_handle_t s, size_t grow_to_fit) {
    size_t new_size = s->stack_size == 0 ? 1 : s->stack_size;
    size_t stack_end = s->ends_count == 0 ? 0 : s->ends[s->ends_count - 1];
    while (new_size < stack_end + grow_to_fit) {
        new_size = new_size * 2;
    }

    if (new_size == s->stack_size) {
        return true;
    }

    char *new_stack = realloc(s->stack, sizeof(*s->stack) * new_size);
    if (new_stack == NULL) {
        return false;
    }
    s->stack = new_stack;
    s->stack_size = new_size;

    return true;
}

static bool strstack_grow_ends(strstack_handle_t s) {
    size_t new_size = s->ends_size * 2;
    size_t *new_ends = realloc(s->ends, sizeof(*s->ends) * new_size);

    if (new_ends == NULL) {
        return false;
    }

    s->ends_size = new_size;
    s->ends = new_ends;

    return true;
}

size_t strstack_depth(strstack_handle_t s) {
    return s->ends_count;
}

bool strstack_push(strstack_handle_t s, const char *str) {
    size_t str_len = strlen(str) + 1;
    if (!strstack_grow_stack(s, str_len)) {
        return false;
    }
    if (s->ends_size == s->ends_count) {
        if (!strstack_grow_ends(s)) {
            return false;
        }
    }
    size_t start_pos = s->ends_count == 0 ? 0 : s->ends[s->ends_count - 1];
    strcpy(&s->stack[start_pos], str);
    s->ends[s->ends_count] = start_pos + str_len;
    s->ends_count++;

    return true;
}

void strstack_pop(strstack_handle_t s) {
    if (s->ends_count == 0) {
        return;
    }
    s->ends_count--;
}

const char *strstack_peek_top(strstack_handle_t s) {
    if (s->ends_count == 0) {
        return NULL;
    }
    if (s->ends_count == 1) {
        return s->stack;
    }
    return &s->stack[s->ends[s->ends_count - 2]];
}

const char *strstack_peek(strstack_handle_t s, size_t stack_depth) {
    if (stack_depth + 1 > s->ends_count) {
        return NULL;
    }
    if (stack_depth == s->ends_count - 1) {
        return s->stack;
    }
    return &s->stack[s->ends[s->ends_count - stack_depth - 2]];
}

const char *strstack_peek_lifo(strstack_handle_t s, size_t position) {
    if (position >= s->ends_count) {
        return NULL;
    }
    if (position == 0) {
        return s->stack;
    }
    return &s->stack[s->ends[position - 1]];
}

void strstack_destroy(strstack_handle_t s) {
    if (s == NULL) {
        return;
    }
    free(s->stack);
    s->stack = NULL;
    s->stack_size = 0;
    free(s->ends);
    s->ends = NULL;
    s->ends_count = 0;
    s->ends_size = 0;
    free(s);
}
