typedef struct dynstr* dynstr_handle_t;

dynstr_handle_t dynstr_new(void);
size_t dynstr_len(dynstr_handle_t dstr);
size_t dynstr_truncate(dynstr_handle_t dstr, const size_t len);
bool dynstr_assign(dynstr_handle_t dstr, const char *c_str);
bool dynstr_append_c_str(dynstr_handle_t dstr, const char *c_str);
char *dynstr_as_c_str(dynstr_handle_t dstr);
void dynstr_destroy(dynstr_handle_t dstr);
