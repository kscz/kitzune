typedef struct strstack* strstack_handle_t;

strstack_handle_t strstack_new(void);
size_t strstack_depth(strstack_handle_t s);
bool strstack_push(strstack_handle_t s, const char *str);
void strstack_pop(strstack_handle_t s);
char *strstack_top(strstack_handle_t s);
char *strstack_get(strstack_handle_t s, size_t stack_depth);
void strstack_destroy(strstack_handle_t s);
