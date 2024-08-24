#include <stdbool.h>
#include <string.h>

#include "dynstr.h"

#define DYNSTR_INITIAL_SIZE 15

typedef struct dynstr {
    char *str;
    size_t size;
    size_t len;
} dynstr_t;

static bool dynstr_grow_to_fit(dynstr_handle_t dstr, const size_t c_str_len) {
    size_t new_size = dstr->size == 0 ? 1 : dstr->size;
    while (new_size < c_str_len + dstr->len + 1) {
        new_size = new_size * 2;
    }
    if (new_size == dstr->size) {
        return true;
    }
    char *new_str = realloc(dstr->str, sizeof(*dstr->str) * new_size);
    if (new_str == NULL) {
        return false;
    }
    dstr->str = new_str;
    return true;
}

bool dynstr_append_c_str(dynstr_handle_t dstr, const char *c_str) {
    if (dstr == NULL || c_str == NULL) {
        return false;
    }
    const size_t c_str_len = strlen(c_str);
    if (!dynstr_grow_to_fit(dstr, c_str_len)) {
        return false;
    }

    strcat(dstr->str, c_str);
    dstr->len += c_str_len;

    return true;
}

size_t dynstr_truncate(dynstr_handle_t dstr, const size_t len) {
    const size_t new_len = len < dstr->len ? len : dstr->len;
    dstr->len = new_len;
    dstr->str[new_len] = '\0';

    return new_len;
}

size_t dynstr_len(dynstr_handle_t dstr) {
    return dstr->len;
}

char *dynstr_as_c_str(dynstr_handle_t dstr) {
    return dstr->str;
}

bool dynstr_assign(dynstr_handle_t dstr, const char *c_str) {
    dynstr_truncate(dstr, 0);
    return dynstr_append_c_str(dstr, c_str);
}

dynstr_handle_t dynstr_new(void) {
    dynstr_t *dstr = malloc(sizeof(dynstr_t));
    if (dstr == NULL) {
        return NULL;
    }
    dstr->size = 0;
    dstr->len = 0;
    dstr->str = NULL;
    dynstr_grow_to_fit(dstr, DYNSTR_INITIAL_SIZE);

    if (dstr->str == NULL) {
        free(dstr);
        return NULL;
    }

    // Make sure we terminate our empty string!
    dstr->str[0] = '\0';

    return dstr;
}

void dynstr_destroy(dynstr_handle_t dstr) {
    if (dstr == NULL) {
        return;
    }
    free(dstr->str);
    dstr->str = NULL;
    dstr->len = 0;
    dstr->size = 0;
    free(dstr);
}
