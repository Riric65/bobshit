/* eval.c — tree walking interpreter + native builtins.
 *
 * Scoping rules (deliberately simple, this is a soft language):
 *   - `if` / `while` / `for` / `try` bodies share the enclosing scope,
 *   - only a function call creates a new scope (parameters + locals),
 *   - functions capture the environment they were created in (closures).
 *
 * Control flow uses a signal slot instead of return codes so that deep
 * expression trees stay readable; `return` values travel in `sig_value`.
 */
#include "eval.h"

#include "lexer.h"
#include "parser.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* interpreter state                                                   */
/* ------------------------------------------------------------------ */

enum { SIG_NONE = 0, SIG_BREAK, SIG_CONTINUE, SIG_RETURN };

static int sig = SIG_NONE;
static Value *sig_value = NULL;
static int frame_depth = 0;

static Value *eval_node(Node *n, Env *env);
static Value *eval_block_node(Node *n, Env *env);

static Value *vstr(const char *s) { return v_str(s); }

static Value *nilv(void) { return V_NIL_S(); }

/* ------------------------------------------------------------------ */
/* containers / indexing                                               */
/* ------------------------------------------------------------------ */

static Value *index_get(Value *obj, Value *key, int line) {
    if (!obj) return nilv();
    switch (obj->k) {
        case V_LIST: {
            long idx = (long)v_number(key);
            long n = (long)obj->as.list.n;
            if (idx < 0) idx += n;
            if (idx < 0 || idx >= n) {
                if (bs_soft) {
                    bs_warn(line, "list index out of range (size %ld), value is nil", n);
                    return nilv();
                }
                char *d = bs_dtoa((double)v_number(key));
                bs_throw(line, "list index %s out of range (size %ld)", d, n);
            }
            return obj->as.list.items[idx];
        }
        case V_STR: {
            long idx = (long)v_number(key);
            long n = (long)v_length(obj);
            if (idx < 0) idx += n;
            if (idx < 0 || idx >= n) {
                if (bs_soft) {
                    bs_warn(line, "string index out of range (length %ld), value is nil", n);
                    return nilv();
                }
                bs_throw(line, "string index out of range (length %ld)", n);
            }
            size_t off = bs_utf8_offset(obj->as.str.s, obj->as.str.len, (size_t)idx);
            size_t next = bs_utf8_offset(obj->as.str.s, obj->as.str.len, (size_t)idx + 1);
            return v_strn(obj->as.str.s + off, next - off);
        }
        case V_MAP: {
            char *k = (key && key->k == V_STR) ? bs_strndup(key->as.str.s, key->as.str.len)
                                               : v_display(key);
            Value *got = map_get(obj, k);
            free(k);
            return got ? got : nilv();
        }
        case V_NIL:
        default:
            if (bs_soft) {
                bs_warn(line, "cannot index a %s value", v_kind_name(obj));
                return nilv();
            }
            bs_throw(line, "cannot index a %s value", v_kind_name(obj));
    }
    return nilv();
}

static void index_set(Value *obj, Value *key, Value *val, int line) {
    if (!obj) return;
    switch (obj->k) {
        case V_LIST: {
            long idx = (long)v_number(key);
            long n = (long)obj->as.list.n;
            if (idx < 0) idx += n;
            if (idx < 0) {
                bs_throw(line, "list index %ld is negative", idx);
            }
            if (idx >= n) {
                if (!bs_soft) bs_throw(line, "list index %ld out of range (size %ld)", idx, n);
                while ((long)obj->as.list.n <= idx) list_push(obj, nilv());
            }
            obj->as.list.items[idx] = val;
            return;
        }
        case V_MAP: {
            char *k = (key && key->k == V_STR) ? bs_strndup(key->as.str.s, key->as.str.len)
                                               : v_display(key);
            map_set(obj, k, val);
            free(k);
            return;
        }
        case V_STR: {
            /* strings are immutable: build a new one */
            long idx = (long)v_number(key);
            long n = (long)v_length(obj);
            if (idx < 0) idx += n;
            if (idx < 0 || idx >= n) bs_throw(line, "string index %ld out of range", idx);
            size_t off = bs_utf8_offset(obj->as.str.s, obj->as.str.len, (size_t)idx);
            size_t next = bs_utf8_offset(obj->as.str.s, obj->as.str.len, (size_t)idx + 1);
            Str s;
            str_init(&s);
            str_addn(&s, obj->as.str.s, off);
            char *rep = v_display(val);
            str_add(&s, rep);
            free(rep);
            str_addn(&s, obj->as.str.s + next, obj->as.str.len - next);
            obj->as.str.s = str_release(&s);
            obj->as.str.len = strlen(obj->as.str.s);
            return;
        }
        default:
            bs_throw(line, "cannot assign into a %s value", v_kind_name(obj));
    }
}

/* ------------------------------------------------------------------ */
/* calling                                                             */
/* ------------------------------------------------------------------ */

Value *eval_call(Value *callee, Value **args, int nargs, int line) {
    if (!callee) return nilv();
    if (callee->k == V_NATIVE) return callee->as.nat.fn(args, nargs, NULL);

    if (callee->k != V_FUN) {
        char *d = v_display(callee);
        if (bs_soft) {
            bs_warn(line, "'%s' is a %s, not a function, call ignored", d, v_kind_name(callee));
            free(d);
            return nilv();
        }
        bs_throw(line, "'%s' is a %s, not a function", d, v_kind_name(callee));
    }

    Node *decl = callee->as.fun.decl;
    if (!decl) return nilv();

    if (frame_depth >= BS_MAX_FRAMES) {
        bs_throw(line, "stack overflow: more than %d nested calls", BS_MAX_FRAMES);
    }

    Env *env = env_new(callee->as.fun.env);
    int np = decl->nparams;
    for (int i = 0; i < np; i++)
        env_set(env, decl->params[i], i < nargs ? args[i] : nilv());
    /* `arguments` is not part of v0.1; extras are dropped on purpose */

    int saved_sig = sig;
    Value *saved_val = sig_value;
    sig = SIG_NONE;
    sig_value = NULL;
    frame_depth++;

    Value *result = nilv();
    if (decl->a) result = eval_block_node(decl->a, env); /* implicit return */

    frame_depth--;
    if (sig == SIG_RETURN) {
        result = sig_value ? sig_value : nilv();
    } else if (sig != SIG_NONE) {
        bs_warn(line, "break/continue outside of a loop, ignored");
    }
    sig = saved_sig;
    sig_value = saved_val;
    if (!env_is_captured(env)) env_free(env); /* a closure may still need it */
    return result;
}

/* ------------------------------------------------------------------ */
/* operators                                                           */
/* ------------------------------------------------------------------ */

static Value *op_plus(Value *a, Value *b) {
    if (a->k == V_STR || b->k == V_STR) {
        Str s;
        str_init(&s);
        char *x = v_display(a), *y = v_display(b);
        str_add(&s, x);
        str_add(&s, y);
        free(x);
        free(y);
        return v_strn(s.data, s.len);
    }
    if (a->k == V_LIST && b->k == V_LIST) {
        Value *out = list_copy(a);
        for (size_t i = 0; i < b->as.list.n; i++) list_push(out, b->as.list.items[i]);
        return out;
    }
    if (a->k == V_LIST) {
        Value *out = list_copy(a);
        list_push(out, b);
        return out;
    }
    if (a->k == V_MAP && b->k == V_MAP) {
        Value *out = map_copy(a);
        for (size_t i = 0; i < b->as.map.n; i++) map_set(out, b->as.map.keys[i], b->as.map.vals[i]);
        return out;
    }
    if (a->k == V_NIL && b->k == V_NIL) return v_num(0);
    return v_num(v_number(a) + v_number(b));
}

static Value *op_div(Value *a, Value *b, int line) {
    double d = v_number(b);
    if (d == 0.0) {
        if (bs_soft) {
            bs_warn(line, "division by zero, result is 0");
            return v_num(0);
        }
        bs_throw(line, "division by zero");
    }
    return v_num(v_number(a) / d);
}

static Value *op_mod(Value *a, Value *b, int line) {
    double d = v_number(b);
    if (d == 0.0) {
        if (bs_soft) {
            bs_warn(line, "modulo by zero, result is 0");
            return v_num(0);
        }
        bs_throw(line, "modulo by zero");
    }
    return v_num(fmod(v_number(a), d));
}

static Value *do_binop(int op, Value *a, Value *b, int line) {
    switch ((TokKind)op) {
        case T_PLUS: return op_plus(a, b);
        case T_MINUS: return v_num(v_number(a) - v_number(b));
        case T_STAR: return v_num(v_number(a) * v_number(b));
        case T_SLASH: return op_div(a, b, line);
        case T_PERCENT: return op_mod(a, b, line);
        case T_EQ: return v_bool(v_eq(a, b));
        case T_NE: return v_bool(!v_eq(a, b));
        case T_LT: return v_bool(v_cmp(a, b) < 0);
        case T_GT: return v_bool(v_cmp(a, b) > 0);
        case T_LE: return v_bool(v_cmp(a, b) <= 0);
        case T_GE: return v_bool(v_cmp(a, b) >= 0);
        case T_KW_AND: return v_truthy(a) ? b : a;
        case T_KW_OR: return v_truthy(a) ? a : b;
        default: return nilv();
    }
}

/* ------------------------------------------------------------------ */
/* assignment                                                          */
/* ------------------------------------------------------------------ */

static Value *do_assign(Node *n, Env *env) {
    Value *val = eval_node(n->b, env);
    if (n->op != (int)T_ASSIGN && n->op != (int)T_DEFL) {
        /* compound: x += y */
        TokKind op;
        switch ((TokKind)n->op) {
            case T_ADDEQ: op = T_PLUS; break;
            case T_SUBEQ: op = T_MINUS; break;
            case T_MULEQ: op = T_STAR; break;
            case T_DIVEQ: op = T_SLASH; break;
            default: op = T_PERCENT; break;
        }
        Value *old = eval_node(n->a, env);
        val = do_binop((int)op, old, val, n->line);
    }

    Node *t = n->a;
    if (t->kind == N_IDENT) {
        env_assign(env, t->str, val);
    } else if (t->kind == N_INDEX) {
        Value *obj = eval_node(t->a, env);
        Value *key = eval_node(t->b, env);
        index_set(obj, key, val, n->line);
    } else if (t->kind == N_DOT) {
        Value *obj = eval_node(t->a, env);
        if (!obj || obj->k != V_MAP) {
            if (bs_soft) {
                bs_warn(n->line, "cannot set field '%s' on a %s", t->str,
                        v_kind_name(obj));
                return val;
            }
            bs_throw(n->line, "cannot set field '%s' on a %s", t->str, v_kind_name(obj));
        }
        map_set(obj, t->str, val);
    }
    return val;
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static void eval_say(Node *n, Env *env) {
    int count = n->nitems;
    for (int i = 0; i < count; i++) {
        if (i > 0 && !(n->op == 1 && i == count - 1)) fputc(' ', stdout);
        Value *v = eval_node(n->items[i], env);
        char *s = v_display(v);
        fputs(s, stdout);
        free(s);
    }
    fputc('\n', stdout);
}

/* Every loop statement gets its own iteration budget: a runaway loop stops
 * with a catchable error instead of hanging the program. */
static void loop_tick(long *guard, int line) {
    if (--*guard <= 0) {
        *guard = BS_MAX_ITERATIONS;
        bs_throw(line, "loop budget exhausted (more than %ld iterations)", BS_MAX_ITERATIONS);
    }
}

static void eval_while(Node *n, Env *env) {
    long guard = BS_MAX_ITERATIONS;
    while (v_truthy(eval_node(n->a, env))) {
        loop_tick(&guard, n->line);
        eval_block_node(n->b, env);
        if (sig == SIG_BREAK) { sig = SIG_NONE; break; }
        if (sig == SIG_CONTINUE) sig = SIG_NONE;
        if (sig != SIG_NONE) break;
    }
}

static void eval_for(Node *n, Env *env) {
    double start = v_number(eval_node(n->a, env));
    double end = v_number(eval_node(n->b, env));
    double step = n->c ? v_number(eval_node(n->c, env)) : 1.0;
    if (step == 0.0) bs_throw(n->line, "range step cannot be 0");
    if (n->nparams > 1) bs_throw(n->line, "a range loop takes a single variable");
    const char *var = n->nparams > 0 ? n->params[0] : "i";
    long guard = BS_MAX_ITERATIONS;

    for (double i = start; (step > 0) ? (i <= end) : (i >= end); i += step) {
        loop_tick(&guard, n->line);
        env_assign(env, var, v_num(i));
        eval_block_node(n->d, env);
        if (sig == SIG_BREAK) { sig = SIG_NONE; break; }
        if (sig == SIG_CONTINUE) sig = SIG_NONE;
        if (sig != SIG_NONE) break;
    }
}

static void eval_foreach(Node *n, Env *env) {
    Value *it = eval_node(n->b, env);
    int nv = n->nparams;
    long guard = BS_MAX_ITERATIONS;
    if (nv == 0) return;

    if (it->k == V_LIST) {
        for (size_t i = 0; i < it->as.list.n; i++) {
            loop_tick(&guard, n->line);
            if (nv == 1) {
                env_assign(env, n->params[0], it->as.list.items[i]);
            } else {
                env_assign(env, n->params[0], v_num((double)i));
                env_assign(env, n->params[1], it->as.list.items[i]);
            }
            eval_block_node(n->c, env);
            if (sig == SIG_BREAK) { sig = SIG_NONE; break; }
            if (sig == SIG_CONTINUE) sig = SIG_NONE;
            if (sig != SIG_NONE) break;
        }
    } else if (it->k == V_STR) {
        size_t chars = v_length(it);
        for (size_t i = 0; i < chars; i++) {
            loop_tick(&guard, n->line);
            size_t off = bs_utf8_offset(it->as.str.s, it->as.str.len, i);
            size_t nxt = bs_utf8_offset(it->as.str.s, it->as.str.len, i + 1);
            Value *c = v_strn(it->as.str.s + off, nxt - off);
            if (nv == 1) env_assign(env, n->params[0], c);
            else {
                env_assign(env, n->params[0], v_num((double)i));
                env_assign(env, n->params[1], c);
            }
            eval_block_node(n->c, env);
            if (sig == SIG_BREAK) { sig = SIG_NONE; break; }
            if (sig == SIG_CONTINUE) sig = SIG_NONE;
            if (sig != SIG_NONE) break;
        }
    } else if (it->k == V_MAP) {
        for (size_t i = 0; i < it->as.map.n; i++) {
            loop_tick(&guard, n->line);
            env_assign(env, n->params[0], vstr(it->as.map.keys[i]));
            if (nv > 1) env_assign(env, n->params[1], it->as.map.vals[i]);
            eval_block_node(n->c, env);
            if (sig == SIG_BREAK) { sig = SIG_NONE; break; }
            if (sig == SIG_CONTINUE) sig = SIG_NONE;
            if (sig != SIG_NONE) break;
        }
    } else if (it->k == V_NIL) {
        if (bs_soft) {
            bs_warn(n->line, "nothing to iterate over (nil)");
            return;
        }
        bs_throw(n->line, "cannot iterate over nil");
    } else {
        if (bs_soft) {
            bs_warn(n->line, "cannot iterate over a %s", v_kind_name(it));
            return;
        }
        bs_throw(n->line, "cannot iterate over a %s", v_kind_name(it));
    }
}

static void eval_try(Node *n, Env *env) {
    BsCatch frame;
    frame.prev = bs_catch_top;
    frame.frame_depth = frame_depth;
    int saved_sig = sig;
    Value *saved_val = sig_value;

    bs_catch_top = &frame;
    if (setjmp(frame.jb) == 0) {
        eval_block_node(n->a, env);
        bs_catch_top = frame.prev;
        return;
    }

    /* caught */
    bs_catch_top = frame.prev;
    frame_depth = frame.frame_depth;
    sig = saved_sig;
    sig_value = saved_val;
    char *msg = bs_error_msg ? bs_error_msg : bs_strdup("unknown error");
    bs_error_msg = NULL;
    if (n->d) {
        if (n->str) env_assign(env, n->str, vstr(msg));
        eval_block_node(n->d, env);
    }
    free(msg);
}

/* Runs a statement list and returns the value of the last statement: a
 * function body without an explicit `return` gives back its last
 * expression, which keeps lambdas and callbacks short. */
static Value *eval_block_node(Node *n, Env *env) {
    Value *last = nilv();
    if (!n) return last;
    for (int i = 0; i < n->nitems; i++) {
        last = eval_node(n->items[i], env);
        if (sig != SIG_NONE) return last;
    }
    return last;
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static Value *eval_node(Node *n, Env *env) {
    if (!n) return nilv();
    switch (n->kind) {
        case N_NUM: return v_num(n->num);
        case N_STR: return v_strn(n->str ? n->str : "", n->str ? strlen(n->str) : 0);
        case N_BOOL: return v_bool(n->boolean);
        case N_NIL: return nilv();
        case N_IDENT: {
            Value *v = env_get(env, n->str);
            if (!v) {
                if (bs_soft) {
                    bs_warn(n->line, "unknown variable '%s', treated as nil", n->str);
                    return nilv();
                }
                bs_throw(n->line, "unknown variable '%s'", n->str);
            }
            return v;
        }
        case N_LIST: {
            Value *l = v_list();
            for (int i = 0; i < n->nitems; i++) list_push(l, eval_node(n->items[i], env));
            return l;
        }
        case N_MAP: {
            Value *m = v_map();
            for (int i = 0; i + 1 < n->nitems; i += 2) {
                char *k = n->items[i]->str ? bs_strdup(n->items[i]->str)
                                           : bs_strdup(v_display(eval_node(n->items[i], env)));
                map_set(m, k, eval_node(n->items[i + 1], env));
                free(k);
            }
            return m;
        }
        case N_INDEX: {
            Value *obj = eval_node(n->a, env);
            Value *key = eval_node(n->b, env);
            return index_get(obj, key, n->line);
        }
        case N_DOT: {
            Value *obj = eval_node(n->a, env);
            if (!obj || obj->k != V_MAP) {
                if (bs_soft) {
                    bs_warn(n->line, "cannot read field '%s' on a %s", n->str,
                            v_kind_name(obj));
                    return nilv();
                }
                bs_throw(n->line, "cannot read field '%s' on a %s", n->str, v_kind_name(obj));
            }
            Value *got = map_get(obj, n->str);
            return got ? got : nilv();
        }
        case N_CALL: {
            Value *callee = eval_node(n->a, env);
            int nargs = n->nitems;
            Value **args = NULL;
            if (nargs) {
                args = bs_calloc((size_t)nargs, sizeof(Value *));
                for (int i = 0; i < nargs; i++) args[i] = eval_node(n->items[i], env);
            }
            Value *out = eval_call(callee, args, nargs, n->line);
            free(args);
            return out;
        }
        case N_BIN: {
            /* short circuit operators */
            if ((TokKind)n->op == T_KW_AND) {
                Value *a = eval_node(n->a, env);
                if (!v_truthy(a)) return a;
                return eval_node(n->b, env);
            }
            if ((TokKind)n->op == T_KW_OR) {
                Value *a = eval_node(n->a, env);
                if (v_truthy(a)) return a;
                return eval_node(n->b, env);
            }
            Value *a = eval_node(n->a, env);
            Value *b = eval_node(n->b, env);
            return do_binop(n->op, a, b, n->line);
        }
        case N_UN: {
            Value *a = eval_node(n->a, env);
            switch ((TokKind)n->op) {
                case T_MINUS: return v_num(-v_number(a));
                case T_PLUS: return v_num(v_number(a));
                default: return v_bool(!v_truthy(a));
            }
        }
        case N_ASSIGN: return do_assign(n, env);
        case N_FUN: {
            Value *f = v_fun(n, env);
            if (n->str) env_set(env, n->str, f);
            return f;
        }
        case N_SAY:
            eval_say(n, env);
            return nilv();
        case N_EXPR: {
            Value *v = eval_node(n->a, env);
            return v;
        }
        case N_BLOCK:
            eval_block_node(n, env);
            return nilv();
        case N_IF: {
            if (v_truthy(eval_node(n->a, env))) {
                eval_block_node(n->b, env);
            } else if (n->c) {
                if (n->c->kind == N_IF) return eval_node(n->c, env); /* elif chain */
                eval_block_node(n->c, env);
            }
            return nilv();
        }
        case N_WHILE: eval_while(n, env); return nilv();
        case N_FOR: eval_for(n, env); return nilv();
        case N_FOREACH: eval_foreach(n, env); return nilv();
        case N_RETURN:
            sig_value = n->a ? eval_node(n->a, env) : nilv();
            sig = SIG_RETURN;
            return sig_value;
        case N_BREAK: sig = SIG_BREAK; return nilv();
        case N_CONTINUE: sig = SIG_CONTINUE; return nilv();
        case N_TRY: eval_try(n, env); return nilv();
    }
    return nilv();
}

/* ------------------------------------------------------------------ */
/* native builtins                                                     */
/* ------------------------------------------------------------------ */

#define ARG(i) ((i) < nargs ? args[i] : NULL)

static const char *EMPTY = "";

static Value *bi_len(Value **args, int nargs, void *ctx) {
    (void)ctx;
    if (!ARG(0)) return v_num(0);
    return v_num((double)v_length(ARG(0)));
}

static Value *bi_type(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return vstr(v_kind_name(ARG(0) ? ARG(0) : nilv()));
}

static Value *bi_str(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return vstr(v_display(ARG(0) ? ARG(0) : nilv()));
}

static Value *bi_num(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_num(v_number(ARG(0) ? ARG(0) : nilv()));
}

static Value *bi_int(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_num(trunc(v_number(ARG(0) ? ARG(0) : nilv())));
}

static Value *bi_bool(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_bool(v_truthy(ARG(0) ? ARG(0) : nilv()));
}

static Value *bi_push(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) {
        bs_warn(0, "push() expects a list as first argument");
        return l ? l : nilv();
    }
    list_push(l, ARG(1) ? ARG(1) : nilv());
    return l;
}

static Value *bi_pop(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) return nilv();
    if (l->as.list.n == 0) {
        if (bs_soft) return nilv();
        bs_throw(0, "pop() on an empty list");
    }
    return list_pop(l);
}

static Value *bi_insert(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) return l ? l : nilv();
    long idx = (long)v_number(ARG(1));
    Value *item = ARG(2) ? ARG(2) : nilv();
    if (idx < 0) idx += (long)l->as.list.n;
    if (idx < 0) idx = 0;
    if (idx > (long)l->as.list.n) idx = (long)l->as.list.n;
    list_push(l, nilv());
    for (long i = (long)l->as.list.n - 1; i > idx; i--) l->as.list.items[i] = l->as.list.items[i - 1];
    l->as.list.items[idx] = item;
    return l;
}

static Value *bi_remove(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) return nilv();
    long idx = (long)v_number(ARG(1));
    long n = (long)l->as.list.n;
    if (idx < 0) idx += n;
    if (idx < 0 || idx >= n) return nilv();
    Value *item = l->as.list.items[idx];
    for (long i = idx; i < n - 1; i++) l->as.list.items[i] = l->as.list.items[i + 1];
    l->as.list.n--;
    return item;
}

static Value *bi_reverse(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) return l ? l : nilv();
    size_t n = l->as.list.n;
    for (size_t i = 0; i < n / 2; i++) {
        Value *t = l->as.list.items[i];
        l->as.list.items[i] = l->as.list.items[n - 1 - i];
        l->as.list.items[n - 1 - i] = t;
    }
    return l;
}

static Value *sort_ctx_fn = NULL;

static int sort_compare(const void *pa, const void *pb) {
    const Value *a = *(const Value *const *)pa;
    const Value *b = *(const Value *const *)pb;
    if (sort_ctx_fn) {
        Value *args[2] = {(Value *)a, (Value *)b};
        Value *r = eval_call(sort_ctx_fn, args, 2, 0);
        return v_number(r) < 0 ? -1 : (v_number(r) > 0 ? 1 : 0);
    }
    return v_cmp(a, b);
}

static Value *bi_sort(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) return l ? l : nilv();
    Value *saved = sort_ctx_fn;
    sort_ctx_fn = (nargs > 1 && args[1] && args[1]->k == V_FUN) ? args[1] : NULL;
    if (l->as.list.n > 1) qsort(l->as.list.items, l->as.list.n, sizeof(Value *), sort_compare);
    sort_ctx_fn = saved;
    return l;
}

static Value *bi_index(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *hay = ARG(0);
    Value *needle = ARG(1) ? ARG(1) : nilv();
    if (!hay) return v_num(-1);
    if (hay->k == V_LIST) return v_num(v_index_of(hay, needle));
    if (hay->k == V_STR) {
        char *n = v_display(needle);
        char *h = bs_strndup(hay->as.str.s, hay->as.str.len);
        char *found = strstr(h, n);
        int r = found ? (int)v_length(v_strn(h, (size_t)(found - h))) : -1;
        free(h);
        free(n);
        return v_num(r);
    }
    return v_num(-1);
}

static Value *bi_keys(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *m = ARG(0);
    Value *out = v_list();
    if (!m || m->k != V_MAP) return out;
    for (size_t i = 0; i < m->as.map.n; i++) list_push(out, vstr(m->as.map.keys[i]));
    return out;
}

static Value *bi_values(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *m = ARG(0);
    Value *out = v_list();
    if (!m || m->k != V_MAP) return out;
    for (size_t i = 0; i < m->as.map.n; i++) list_push(out, m->as.map.vals[i]);
    return out;
}

static Value *bi_has(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *obj = ARG(0);
    if (!obj) return v_bool(0);
    if (obj->k == V_MAP) {
        char *k = v_display(ARG(1) ? ARG(1) : nilv());
        int r = map_get(obj, k) != NULL;
        free(k);
        return v_bool(r);
    }
    if (obj->k == V_LIST) {
        char *k = v_display(ARG(1) ? ARG(1) : nilv());
        int r = 0;
        for (size_t i = 0; i < obj->as.list.n && !r; i++) {
            char *e = v_display(obj->as.list.items[i]);
            if (strcmp(e, k) == 0) r = 1;
            free(e);
        }
        free(k);
        return v_bool(r);
    }
    return v_bool(0);
}

static Value *bi_del(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *m = ARG(0);
    if (!m || m->k != V_MAP) return v_bool(0);
    char *k = v_display(ARG(1) ? ARG(1) : nilv());
    int r = map_del(m, k);
    free(k);
    return v_bool(r);
}

static Value *bi_range(Value **args, int nargs, void *ctx) {
    (void)ctx;
    double a = v_number(ARG(0) ? ARG(0) : nilv());
    double b = v_number(ARG(1) ? ARG(1) : nilv());
    double step = nargs > 2 && args[2] ? v_number(args[2]) : (b >= a ? 1.0 : -1.0);
    if (step == 0.0) {
        bs_warn(0, "range() step cannot be 0");
        return v_list();
    }
    Value *out = v_list();
    for (double i = a; (step > 0) ? (i <= b) : (i >= b); i += step) {
        if ((long)out->as.list.n > BS_MAX_ITERATIONS) {
            bs_warn(0, "range() result truncated");
            break;
        }
        list_push(out, v_num(i));
    }
    return out;
}

static Value *bi_join(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    char *sep = ARG(1) ? v_display(ARG(1)) : bs_strdup(EMPTY);
    Str s;
    str_init(&s);
    if (l && l->k == V_LIST) {
        for (size_t i = 0; i < l->as.list.n; i++) {
            if (i) str_add(&s, sep);
            char *e = v_display(l->as.list.items[i]);
            str_add(&s, e);
            free(e);
        }
    } else if (l) {
        char *e = v_display(l);
        str_add(&s, e);
        free(e);
    }
    free(sep);
    return v_strn(s.data, s.len);
}

static Value *bi_split(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *str = ARG(0);
    char *sep = ARG(1) ? v_display(ARG(1)) : bs_strdup(",");
    Value *out = v_list();
    if (!str) return out;
    char *s = str->k == V_STR ? bs_strndup(str->as.str.s, str->as.str.len) : v_display(str);
    if (strlen(sep) == 0) { /* split into characters */
        size_t n = v_length(v_str(s));
        for (size_t i = 0; i < n; i++) {
            size_t off = bs_utf8_offset(s, strlen(s), i);
            size_t nx = bs_utf8_offset(s, strlen(s), i + 1);
            list_push(out, v_strn(s + off, nx - off));
        }
        free(s);
        free(sep);
        return out;
    }
    char *p = s;
    for (;;) {
        char *hit = strstr(p, sep);
        if (!hit) {
            list_push(out, vstr(p));
            break;
        }
        list_push(out, v_strn(p, (size_t)(hit - p)));
        p = hit + strlen(sep);
    }
    free(s);
    free(sep);
    return out;
}

static Value *bi_upper(Value **args, int nargs, void *ctx) {
    (void)ctx;
    char *s = v_display(ARG(0) ? ARG(0) : nilv());
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    Value *r = vstr(s);
    free(s);
    return r;
}

static Value *bi_lower(Value **args, int nargs, void *ctx) {
    (void)ctx;
    char *s = v_display(ARG(0) ? ARG(0) : nilv());
    for (char *p = s; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    Value *r = vstr(s);
    free(s);
    return r;
}

static Value *bi_trim(Value **args, int nargs, void *ctx) {
    (void)ctx;
    char *s = v_display(ARG(0) ? ARG(0) : nilv());
    char *p = s;
    while (*p && strchr(" \t\r\n", *p)) p++;
    char *e = p + strlen(p);
    while (e > p && strchr(" \t\r\n", e[-1])) e--;
    Value *r = v_strn(p, (size_t)(e - p));
    free(s);
    return r;
}

static Value *bi_replace(Value **args, int nargs, void *ctx) {
    (void)ctx;
    char *s = v_display(ARG(0) ? ARG(0) : nilv());
    char *from = v_display(ARG(1) ? ARG(1) : nilv());
    char *to = ARG(2) ? v_display(ARG(2)) : bs_strdup(EMPTY);
    Str out;
    str_init(&out);
    if (strlen(from) == 0) {
        str_add(&out, s);
    } else {
        char *p = s;
        for (;;) {
            char *hit = strstr(p, from);
            if (!hit) {
                str_add(&out, p);
                break;
            }
            str_addn(&out, p, (size_t)(hit - p));
            str_add(&out, to);
            p = hit + strlen(from);
        }
    }
    free(s);
    free(from);
    free(to);
    return v_strn(out.data, out.len);
}

static Value *bi_starts(Value **args, int nargs, void *ctx) {
    (void)ctx;
    char *s = v_display(ARG(0) ? ARG(0) : nilv());
    char *p = v_display(ARG(1) ? ARG(1) : nilv());
    int r = strncmp(s, p, strlen(p)) == 0;
    free(s);
    free(p);
    return v_bool(r);
}

static Value *bi_ends(Value **args, int nargs, void *ctx) {
    (void)ctx;
    char *s = v_display(ARG(0) ? ARG(0) : nilv());
    char *p = v_display(ARG(1) ? ARG(1) : nilv());
    size_t ls = strlen(s), lp = strlen(p);
    int r = lp <= ls && strcmp(s + ls - lp, p) == 0;
    free(s);
    free(p);
    return v_bool(r);
}

static Value *bi_abs(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_num(fabs(v_number(ARG(0) ? ARG(0) : nilv())));
}

static Value *bi_floor(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_num(floor(v_number(ARG(0) ? ARG(0) : nilv())));
}

static Value *bi_ceil(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_num(ceil(v_number(ARG(0) ? ARG(0) : nilv())));
}

static Value *bi_round(Value **args, int nargs, void *ctx) {
    (void)ctx;
    double d = v_number(ARG(0) ? ARG(0) : nilv());
    long places = nargs > 1 && args[1] ? (long)v_number(args[1]) : 0;
    double m = pow(10.0, (double)places);
    return v_num(round(d * m) / m);
}

static Value *bi_sqrt(Value **args, int nargs, void *ctx) {
    (void)ctx;
    double d = v_number(ARG(0) ? ARG(0) : nilv());
    if (d < 0) {
        bs_warn(0, "sqrt() of a negative number, result is 0");
        return v_num(0);
    }
    return v_num(sqrt(d));
}

static Value *bi_pow(Value **args, int nargs, void *ctx) {
    (void)ctx;
    return v_num(pow(v_number(ARG(0) ? ARG(0) : nilv()), v_number(ARG(1) ? ARG(1) : nilv())));
}

static Value *bi_min(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *best = NULL;
    for (int i = 0; i < nargs; i++) {
        if (!args[i]) continue;
        if (!best || v_cmp(args[i], best) < 0) best = args[i];
    }
    return best ? best : nilv();
}

static Value *bi_max(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *best = NULL;
    for (int i = 0; i < nargs; i++) {
        if (!args[i]) continue;
        if (!best || v_cmp(args[i], best) > 0) best = args[i];
    }
    return best ? best : nilv();
}

static Value *bi_sum(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    double total = 0;
    if (l && l->k == V_LIST) {
        for (size_t i = 0; i < l->as.list.n; i++) total += v_number(l->as.list.items[i]);
    } else if (l) {
        total = v_number(l);
    }
    return v_num(total);
}

static Value *bi_input(Value **args, int nargs, void *ctx) {
    (void)ctx;
    if (ARG(0)) {
        char *p = v_display(ARG(0));
        fputs(p, stdout);
        free(p);
        fflush(stdout);
    }
    Str s;
    str_init(&s);
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') str_addc(&s, (char)c);
    if (c == EOF && s.len == 0) {
        str_free(&s);
        return vstr("");
    }
    return v_strn(s.data, s.len);
}

static Value *bi_count(Value **args, int nargs, void *ctx) {
    (void)ctx;
    Value *l = ARG(0);
    if (!l || l->k != V_LIST) return v_num(0);
    Value *needle = ARG(1) ? ARG(1) : nilv();
    int total = 0;
    for (size_t i = 0; i < l->as.list.n; i++)
        if (v_eq(l->as.list.items[i], needle)) total++;
    return v_num(total);
}

static const Native NATIVES[] = {
    {"len", bi_len},       {"type", bi_type},   {"str", bi_str},
    {"num", bi_num},       {"int", bi_int},     {"bool", bi_bool},
    {"push", bi_push},     {"pop", bi_pop},     {"insert", bi_insert},
    {"remove", bi_remove}, {"reverse", bi_reverse}, {"sort", bi_sort},
    {"index", bi_index},   {"count", bi_count}, {"keys", bi_keys},
    {"values", bi_values}, {"has", bi_has},     {"del", bi_del},
    {"range", bi_range},   {"join", bi_join},   {"split", bi_split},
    {"upper", bi_upper},   {"lower", bi_lower}, {"trim", bi_trim},
    {"replace", bi_replace}, {"starts", bi_starts}, {"ends", bi_ends},
    {"abs", bi_abs},       {"floor", bi_floor}, {"ceil", bi_ceil},
    {"round", bi_round},   {"sqrt", bi_sqrt},   {"pow", bi_pow},
    {"min", bi_min},       {"max", bi_max},     {"sum", bi_sum},
    {"input", bi_input},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

Env *eval_global_env(void) {
    Env *g = env_new(NULL);
    for (int i = 0; NATIVES[i].name; i++) env_set(g, NATIVES[i].name, v_native(NATIVES[i].name, NATIVES[i].fn));
    return g;
}

Value *eval_run(Node *prog, Env *env) {
    sig = SIG_NONE;
    sig_value = NULL;
    frame_depth = 0;
    Value *last = nilv();
    if (prog) {
        for (int i = 0; i < prog->nitems; i++) {
            last = eval_node(prog->items[i], env);
            if (sig == SIG_RETURN) {
                /* soft: a `return` outside of a function is a no-op */
                bs_warn(prog->items[i]->line, "return outside of a function, ignored");
                sig = SIG_NONE;
                sig_value = NULL;
            }
            if (sig != SIG_NONE) break;
        }
    }
    fflush(stdout);
    return last;
}

Value *eval_source(const char *src, Env *env) {
    TokenStream *ts = lex(src);
    Node *prog = parse_program(ts);
    Value *out = eval_run(prog, env);
    tokens_free(ts);
    ast_free(prog);
    return out;
}

char *eval_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Str s;
    str_init(&s);
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) str_addn(&s, buf, got);
    fclose(f);
    return str_release(&s);
}
