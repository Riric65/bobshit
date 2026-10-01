/* value.c — runtime values, containers and the environment chain.
 *
 * Memory model: v0.1 has no garbage collector. Values are immutable
 * containers-by-copy; lists and maps are reference values that mutate in
 * place. Everything is freed when the process exits.
 */
#include "value.h"

#include "gc.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* constructors                                                        */
/* ------------------------------------------------------------------ */

static Env *g_global;

Env *bs_global_env(void) { return g_global; }
void bs_set_global_env(Env *e) { g_global = e; }

static Value *alloc_value(VKind k) {
    Value *v = gc_alloc(sizeof(Value));
    v->k = k;
    return v;
}

Value *v_nil(void) { return alloc_value(V_NIL); }
Value *v_bool(int b) { Value *v = alloc_value(V_BOOL); v->as.b = b ? 1 : 0; return v; }
Value *v_num(double d) { Value *v = alloc_value(V_NUM); v->as.n = d; return v; }

Value *v_strn(const char *s, size_t len) {
    Value *v = alloc_value(V_STR);
    v->as.str.s = bs_strndup(s ? s : "", len);
    v->as.str.len = v->as.str.s ? strlen(v->as.str.s) : 0;
    return v;
}

Value *v_str(const char *s) { return v_strn(s, s ? strlen(s) : 0); }
Value *v_list(void) { return alloc_value(V_LIST); }
Value *v_map(void) { return alloc_value(V_MAP); }

Value *v_fun(Node *decl, Env *env) {
    Value *v = alloc_value(V_FUN);
    v->as.fun.decl = decl;
    v->as.fun.env = env;
    env_mark_captured(env);
    return v;
}

Value *v_native(const char *name, NativeFn fn) {
    Value *v = alloc_value(V_NATIVE);
    v->as.nat.name = name;
    v->as.nat.fn = fn;
    return v;
}

Value *V_NIL_S(void) {
    static Value *nil = NULL;
    if (!nil) nil = v_nil();
    return nil;
}

/* ------------------------------------------------------------------ */
/* lists                                                               */
/* ------------------------------------------------------------------ */

void list_push(Value *list, Value *v) {
    if (!list || list->k != V_LIST) return;
    if (list->as.list.n == list->as.list.cap) {
        list->as.list.cap = list->as.list.cap ? list->as.list.cap * 2 : 8;
        list->as.list.items =
            bs_realloc(list->as.list.items, sizeof(Value *) * list->as.list.cap);
    }
    list->as.list.items[list->as.list.n++] = v;
}

Value *list_pop(Value *list) {
    if (!list || list->k != V_LIST || list->as.list.n == 0) return V_NIL_S();
    return list->as.list.items[--list->as.list.n];
}

Value *list_copy(Value *list) {
    Value *out = v_list();
    if (!list || list->k != V_LIST) return out;
    for (size_t i = 0; i < list->as.list.n; i++) list_push(out, list->as.list.items[i]);
    return out;
}

size_t list_len(Value *list) { return (list && list->k == V_LIST) ? list->as.list.n : 0; }

/* ------------------------------------------------------------------ */
/* maps (insertion ordered)                                            */
/* ------------------------------------------------------------------ */

Value *map_get(Value *map, const char *key) {
    if (!map || map->k != V_MAP || !key) return NULL;
    for (size_t i = 0; i < map->as.map.n; i++)
        if (strcmp(map->as.map.keys[i], key) == 0) return map->as.map.vals[i];
    return NULL;
}

void map_set(Value *map, const char *key, Value *val) {
    if (!map || map->k != V_MAP || !key) return;
    for (size_t i = 0; i < map->as.map.n; i++) {
        if (strcmp(map->as.map.keys[i], key) == 0) {
            map->as.map.vals[i] = val;
            return;
        }
    }
    if (map->as.map.n == map->as.map.cap) {
        map->as.map.cap = map->as.map.cap ? map->as.map.cap * 2 : 8;
        map->as.map.keys = bs_realloc(map->as.map.keys, sizeof(char *) * map->as.map.cap);
        map->as.map.vals = bs_realloc(map->as.map.vals, sizeof(Value *) * map->as.map.cap);
    }
    map->as.map.keys[map->as.map.n] = bs_strdup(key);
    map->as.map.vals[map->as.map.n] = val;
    map->as.map.n++;
}

int map_del(Value *map, const char *key) {
    if (!map || map->k != V_MAP || !key) return 0;
    for (size_t i = 0; i < map->as.map.n; i++) {
        if (strcmp(map->as.map.keys[i], key) == 0) {
            free(map->as.map.keys[i]);
            for (size_t j = i + 1; j < map->as.map.n; j++) {
                map->as.map.keys[j - 1] = map->as.map.keys[j];
                map->as.map.vals[j - 1] = map->as.map.vals[j];
            }
            map->as.map.n--;
            return 1;
        }
    }
    return 0;
}

Value *map_copy(Value *map) {
    Value *out = v_map();
    if (!map || map->k != V_MAP) return out;
    for (size_t i = 0; i < map->as.map.n; i++)
        map_set(out, map->as.map.keys[i], map->as.map.vals[i]);
    return out;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

const char *v_kind_name(const Value *v) {
    if (!v) return "nil";
    switch (v->k) {
        case V_NIL: return "nil";
        case V_BOOL: return "bool";
        case V_NUM: return "num";
        case V_STR: return "str";
        case V_LIST: return "list";
        case V_MAP: return "map";
        case V_FUN: return "fun";
        case V_NATIVE: return "native";
    }
    return "nil";
}

int v_truthy(const Value *v) {
    if (!v) return 0;
    switch (v->k) {
        case V_NIL: return 0;
        case V_BOOL: return v->as.b;
        case V_NUM: return v->as.n != 0.0;
        case V_STR: return v->as.str.len > 0;
        case V_LIST: return v->as.list.n > 0;
        case V_MAP: return v->as.map.n > 0;
        case V_FUN:
        case V_NATIVE: return 1;
    }
    return 0;
}

static int num_equal(double a, double b) {
    /* soft: NaN never equals anything */
    if (a != a || b != b) return 0;
    return a == b;
}

int v_eq(const Value *a, const Value *b) {
    if (!a || !b) return a == b;
    if (a->k == V_NIL || b->k == V_NIL) return a->k == V_NIL && b->k == V_NIL;
    if (a->k == V_NUM && b->k == V_NUM) return num_equal(a->as.n, b->as.n);
    if (a->k == V_STR && b->k == V_STR)
        return a->as.str.len == b->as.str.len && memcmp(a->as.str.s, b->as.str.s, a->as.str.len) == 0;
    if (a->k == V_BOOL && b->k == V_BOOL) return a->as.b == b->as.b;
    if (a->k == V_LIST && b->k == V_LIST) {
        if (a == b) return 1;
        if (a->as.list.n != b->as.list.n) return 0;
        for (size_t i = 0; i < a->as.list.n; i++)
            if (!v_eq(a->as.list.items[i], b->as.list.items[i])) return 0;
        return 1;
    }
    if (a->k == V_MAP && b->k == V_MAP) {
        if (a == b) return 1;
        if (a->as.map.n != b->as.map.n) return 0;
        for (size_t i = 0; i < a->as.map.n; i++) {
            Value *other = map_get((Value *)b, a->as.map.keys[i]);
            if (!other) return 0;
            if (!v_eq(a->as.map.vals[i], other)) return 0;
        }
        return 1;
    }
    if (a->k == V_FUN && b->k == V_FUN) return a->as.fun.decl == b->as.fun.decl;
    if (a->k == V_NATIVE && b->k == V_NATIVE) return a->as.nat.fn == b->as.nat.fn;
    /* mixed types: soft comparison against numbers */
    if (a->k == V_NUM || b->k == V_NUM) {
        if (a->k == V_BOOL || b->k == V_BOOL) return 0;
        return num_equal(v_number(a), v_number(b));
    }
    return 0;
}

int v_cmp(const Value *a, const Value *b) {
    if (!a) a = V_NIL_S();
    if (!b) b = V_NIL_S();
    if (a->k == V_STR && b->k == V_STR) {
        size_t n = a->as.str.len < b->as.str.len ? a->as.str.len : b->as.str.len;
        int c = memcmp(a->as.str.s, b->as.str.s, n);
        if (c != 0) return c < 0 ? -1 : 1;
        if (a->as.str.len == b->as.str.len) return 0;
        return a->as.str.len < b->as.str.len ? -1 : 1;
    }
    if (a->k == V_BOOL && b->k == V_BOOL) return a->as.b - b->as.b;
    if (a->k == V_NIL && b->k == V_NIL) return 0;
    if (a->k == V_LIST && b->k == V_LIST) {
        size_t n = a->as.list.n < b->as.list.n ? a->as.list.n : b->as.list.n;
        for (size_t i = 0; i < n; i++) {
            int c = v_cmp(a->as.list.items[i], b->as.list.items[i]);
            if (c) return c;
        }
        if (a->as.list.n == b->as.list.n) return 0;
        return a->as.list.n < b->as.list.n ? -1 : 1;
    }
    double x = v_number(a), y = v_number(b);
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

char *v_display(const Value *v) {
    if (!v) return bs_strdup("nil");
    switch (v->k) {
        case V_NIL: return bs_strdup("nil");
        case V_BOOL: return bs_strdup(v->as.b ? "true" : "false");
        case V_NUM: return bs_dtoa(v->as.n);
        case V_STR: return bs_strndup(v->as.str.s, v->as.str.len);
        case V_LIST: {
            Str s;
            str_init(&s);
            str_addc(&s, '[');
            for (size_t i = 0; i < v->as.list.n; i++) {
                if (i) str_add(&s, ", ");
                char *e = v_display(v->as.list.items[i]);
                str_add(&s, e);
                free(e);
            }
            str_addc(&s, ']');
            return str_release(&s);
        }
        case V_MAP: {
            Str s;
            str_init(&s);
            str_addc(&s, '{');
            for (size_t i = 0; i < v->as.map.n; i++) {
                if (i) str_add(&s, ", ");
                str_add(&s, v->as.map.keys[i]);
                str_add(&s, ": ");
                char *e = v_display(v->as.map.vals[i]);
                str_add(&s, e);
                free(e);
            }
            str_addc(&s, '}');
            return str_release(&s);
        }
        case V_FUN:
            return v->as.fun.decl && v->as.fun.decl->str
                       ? bs_strdup(v->as.fun.decl->str)
                       : bs_strdup("<fun>");
        case V_NATIVE: return bs_strdup(v->as.nat.name);
    }
    return bs_strdup("nil");
}

static void write_quoted(Str *s, const char *p, size_t len) {
    str_addc(s, '"');
    for (size_t i = 0; i < len; i++) {
        char c = p[i];
        switch (c) {
            case '"': str_add(s, "\\\""); break;
            case '\\': str_add(s, "\\\\"); break;
            case '\n': str_add(s, "\\n"); break;
            case '\t': str_add(s, "\\t"); break;
            case '\r': str_add(s, "\\r"); break;
            default: str_addc(s, c);
        }
    }
    str_addc(s, '"');
}

char *v_inspect(const Value *v) {
    if (!v) return bs_strdup("nil");
    switch (v->k) {
        case V_NIL: return bs_strdup("nil");
        case V_BOOL: return bs_strdup(v->as.b ? "true" : "false");
        case V_NUM: return bs_dtoa(v->as.n);
        case V_STR: {
            Str s;
            str_init(&s);
            write_quoted(&s, v->as.str.s, v->as.str.len);
            return str_release(&s);
        }
        case V_LIST: {
            Str s;
            str_init(&s);
            str_addc(&s, '[');
            for (size_t i = 0; i < v->as.list.n; i++) {
                if (i) str_add(&s, ", ");
                char *e = v_inspect(v->as.list.items[i]);
                str_add(&s, e);
                free(e);
            }
            str_addc(&s, ']');
            return str_release(&s);
        }
        case V_MAP: {
            Str s;
            str_init(&s);
            str_addc(&s, '{');
            for (size_t i = 0; i < v->as.map.n; i++) {
                if (i) str_add(&s, ", ");
                write_quoted(&s, v->as.map.keys[i], strlen(v->as.map.keys[i]));
                str_add(&s, ": ");
                char *e = v_inspect(v->as.map.vals[i]);
                str_add(&s, e);
                free(e);
            }
            str_addc(&s, '}');
            return str_release(&s);
        }
        case V_FUN:
            return v->as.fun.decl && v->as.fun.decl->str ? bs_strdup(v->as.fun.decl->str)
                                                        : bs_strdup("<fun>");
        case V_NATIVE: return bs_strdup(v->as.nat.name);
    }
    return bs_strdup("nil");
}

/* soft numeric coercion: "42", true, nil, ... */
double v_number(const Value *v) {
    if (!v) return 0.0;
    switch (v->k) {
        case V_NUM: return v->as.n;
        case V_BOOL: return v->as.b ? 1.0 : 0.0;
        case V_NIL: return 0.0;
        case V_STR: {
            const char *p = v->as.str.s;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            char *end = NULL;
            double d = strtod(p, &end);
            if (end == p) return 0.0;
            return d;
        }
        case V_LIST: return v->as.list.n;
        case V_MAP: return v->as.map.n;
        default: return 1.0;
    }
}

Value *v_from_number(double d) { return v_num(d); }

size_t v_length(const Value *v) {
    if (!v) return 0;
    switch (v->k) {
        case V_STR: {
            /* count utf-8 code points */
            size_t count = 0;
            for (size_t i = 0; i < v->as.str.len;) {
                size_t step = bs_utf8_seqlen((unsigned char)v->as.str.s[i]);
                if (i + step > v->as.str.len) step = 1;
                i += step;
                count++;
            }
            return count;
        }
        case V_LIST: return v->as.list.n;
        case V_MAP: return v->as.map.n;
        case V_NIL: return 0;
        case V_BOOL: return v->as.b ? 1 : 0;
        case V_NUM: {
            char *s = bs_dtoa(v->as.n);
            size_t n = strlen(s);
            free(s);
            return n;
        }
        default: return 1;
    }
}

int v_index_of(const Value *list, const Value *needle) {
    if (!list || list->k != V_LIST) return -1;
    for (size_t i = 0; i < list->as.list.n; i++)
        if (v_eq(list->as.list.items[i], needle)) return (int)i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* environment                                                         */
/* ------------------------------------------------------------------ */

Env *env_new(Env *parent) {
    Env *e = bs_calloc(1, sizeof(Env));
    e->parent = parent;
    return e;
}

void env_mark_captured(Env *e) {
    if (e) e->captured = 1;
}

int env_is_captured(const Env *e) { return e ? e->captured : 0; }

void env_free(Env *e) {
    if (!e) return;
    Binding *b = e->vars;
    while (b) {
        Binding *next = b->next;
        free(b->name);
        free(b);
        b = next;
    }
    free(e);
}

Value *env_get(Env *e, const char *name) {
    for (Env *s = e; s; s = s->parent) {
        for (Binding *b = s->vars; b; b = b->next) {
            if (bs_streq_ci(b->name, name)) return b->v;
        }
    }
    return NULL;
}

int env_has_local(Env *e, const char *name) {
    if (!e) return 0;
    for (Binding *b = e->vars; b; b = b->next)
        if (bs_streq_ci(b->name, name)) return 1;
    return 0;
}

void env_set(Env *e, const char *name, Value *v) {
    for (Binding *b = e->vars; b; b = b->next) {
        if (bs_streq_ci(b->name, name)) {
            b->v = v;
            return;
        }
    }
    Binding *b = bs_calloc(1, sizeof(Binding));
    b->name = bs_strdup(name);
    b->v = v;
    b->next = e->vars;
    e->vars = b;
}

int env_assign(Env *e, const char *name, Value *v) {
    for (Env *s = e; s; s = s->parent) {
        for (Binding *b = s->vars; b; b = b->next) {
            if (bs_streq_ci(b->name, name)) {
                b->v = v;
                return 1;
            }
        }
    }
    env_set(e, name, v);
    return 0;
}
