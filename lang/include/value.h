/* value.h — tagged union values + environment chain. */
#ifndef BOBSHIT_VALUE_H
#define BOBSHIT_VALUE_H

#include "ast.h"
#include "common.h"

typedef enum { V_NIL, V_BOOL, V_NUM, V_STR, V_LIST, V_MAP, V_FUN, V_NATIVE } VKind;

typedef struct Value Value;
typedef struct Env Env;

/* user defined function */
typedef struct {
    Node *decl; /* N_FUN node */
    Env *env;   /* closure environment */
} FunVal;

typedef Value *(*NativeFn)(Value **args, int nargs, void *ctx);

typedef struct {
    const char *name;
    NativeFn fn;
} Native;

typedef struct Binding Binding;

struct Env {
    Binding *vars;
    Env *parent;
    int captured;
};

struct Binding {
    char *name;
    Value *v;
    Binding *next;
};

struct Value {
    VKind k;
    unsigned char gc_mark;
    union {
        int b;
        double n;
        struct { char *s; size_t len; } str;
        struct { Value **items; size_t n; size_t cap; } list;
        struct { char **keys; Value **vals; size_t n; size_t cap; } map;
        FunVal fun;
        Native nat;
    } as;
};

/* ------------------------------------------------------------------ */
/* constructors (all heap allocated; garbage is collected at exit)    */
/* ------------------------------------------------------------------ */
Value *v_nil(void);
Value *v_bool(int b);
Value *v_num(double d);
Value *v_str(const char *s);
Value *v_strn(const char *s, size_t len);
Value *v_list(void);
Value *v_map(void);
Value *v_fun(Node *decl, Env *env);
Value *v_native(const char *name, NativeFn fn);

/* singletons (immutable) */
Value *V_NIL_S(void);

/* ------------------------------------------------------------------ */
/* list & map operations                                               */
/* ------------------------------------------------------------------ */
void list_push(Value *list, Value *v);
Value *list_pop(Value *list);
Value *list_copy(Value *list);
size_t list_len(Value *list);
Value *map_get(Value *map, const char *key);
void map_set(Value *map, const char *key, Value *val);
int map_del(Value *map, const char *key);
Value *map_copy(Value *map);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
const char *v_kind_name(const Value *v);
int v_truthy(const Value *v);
int v_eq(const Value *a, const Value *b);
int v_cmp(const Value *a, const Value *b); /* <0, 0, >0 — numbers and strings */
char *v_display(const Value *v);   /* what `say` prints: no quotes for strings */
char *v_inspect(const Value *v);   /* what str() gives back: quoted strings */
double v_number(const Value *v);   /* numeric coercion, soft friendly */
Value *v_from_number(double d);
size_t v_length(const Value *v);
int v_index_of(const Value *list, const Value *needle);

/* ------------------------------------------------------------------ */
/* environment                                                         */
/* ------------------------------------------------------------------ */
Env *env_new(Env *parent);
void env_free(Env *e);
Env *bs_global_env(void);
void bs_set_global_env(Env *e);
/* A closure keeps its defining scope alive: mark it so the call frame does
 * not free it. */
void env_mark_captured(Env *e);
int env_is_captured(const Env *e);
Value *env_get(Env *e, const char *name);
void env_set(Env *e, const char *name, Value *v);  /* in the current scope */
int env_assign(Env *e, const char *name, Value *v); /* walks the scopes up */
int env_has_local(Env *e, const char *name);

#endif /* BOBSHIT_VALUE_H */
