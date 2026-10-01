#include "gc.h"

#include "value.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_BYTES (64u * 1024u)

typedef struct Page {
    struct Page *next;
    unsigned char mem[PAGE_BYTES];
} Page;

static Page *g_pages;
static size_t g_bump;
static Value **g_heap;
static size_t g_heap_len, g_heap_cap;
static Value **g_mark_stack;
static size_t g_mark_len, g_mark_cap;
static Value *g_free_list;
static void **g_roots;
static size_t g_roots_len, g_roots_cap;
static void **g_envs;
static size_t g_envs_len, g_envs_cap;
static unsigned char *g_stack_top;
static size_t g_alloc_since;
static size_t g_limit = 262144;
static int g_pending;
static int g_enabled = 1;
static GCStats g_stats;

static void oom(void) {
    fputs("bobshit: out of memory\n", stderr);
    exit(1);
}

void *gc_alloc(size_t n) {
    if (n != sizeof(Value)) {
        void *p = malloc(n ? n : 1);
        if (!p) oom();
        return p;
    }

    Value *v = g_free_list;
    if (v) {
        g_free_list = (Value *)(v->as.list.items);
    } else {
        if (!g_pages || g_bump + sizeof(Value) > PAGE_BYTES) {
            Page *p = malloc(sizeof(Page));
            if (!p) oom();
            p->next = g_pages;
            g_pages = p;
            g_stats.bytes += PAGE_BYTES;
            g_bump = 0;
        }
        v = (Value *)(g_pages->mem + g_bump);
        g_bump += sizeof(Value);
    }

    memset(v, 0, sizeof(Value));

    if (g_heap_len == g_heap_cap) {
        size_t cap = g_heap_cap ? g_heap_cap * 2 : 8192;
        Value **h = realloc(g_heap, cap * sizeof(Value *));
        if (!h) oom();
        g_heap = h;
        g_heap_cap = cap;
    }
    g_heap[g_heap_len++] = v;
    if (g_enabled && ++g_alloc_since > g_limit) g_pending = 1;
    return v;
}

static void value_release(Value *v) {
    switch (v->k) {
        case V_STR: free(v->as.str.s); break;
        case V_LIST: free(v->as.list.items); break;
        case V_MAP:
            for (size_t i = 0; i < v->as.map.n; i++) free(v->as.map.keys[i]);
            free(v->as.map.keys);
            free(v->as.map.vals);
            break;
        default: break;
    }
}

void gc_add_root(void **slot) {
    if (g_roots_len == g_roots_cap) {
        size_t cap = g_roots_cap ? g_roots_cap * 2 : 16;
        void **r = realloc(g_roots, cap * sizeof(void *));
        if (!r) oom();
        g_roots = r;
        g_roots_cap = cap;
    }
    g_roots[g_roots_len++] = slot;
}

void gc_push_env(void *env) {
    if (g_envs_len == g_envs_cap) {
        size_t cap = g_envs_cap ? g_envs_cap * 2 : 64;
        void **e = realloc(g_envs, cap * sizeof(void *));
        if (!e) oom();
        g_envs = e;
        g_envs_cap = cap;
    }
    g_envs[g_envs_len++] = env;
}

void gc_pop_env(void *env) {
    if (g_envs_len > 0 && g_envs[g_envs_len - 1] == env) g_envs_len--;
}

static int is_ours(const void *p) {
    const unsigned char *c = (const unsigned char *)p;
    for (Page *pg = g_pages; pg; pg = pg->next) {
        if (c >= pg->mem && c < pg->mem + PAGE_BYTES)
            return ((size_t)(c - pg->mem) % sizeof(Value)) == 0;
    }
    return 0;
}

static void push_mark(Value *v) {
    if (g_mark_len == g_mark_cap) {
        size_t cap = g_mark_cap ? g_mark_cap * 2 : 1024;
        Value **m = realloc(g_mark_stack, cap * sizeof(Value *));
        if (!m) oom();
        g_mark_stack = m;
        g_mark_cap = cap;
    }
    g_mark_stack[g_mark_len++] = v;
}

static void mark_ptr(void *p) {
    if (!p || (uintptr_t)p < 4096) return;
    if (!is_ours(p)) return;
    Value *v = (Value *)p;
    if (v->gc_mark) return;
    v->gc_mark = 1;
    push_mark(v);
}

static void mark_env(void *p);

static void mark_value(Value *v) {
    switch (v->k) {
        case V_LIST:
            for (size_t i = 0; i < v->as.list.n; i++) mark_ptr(v->as.list.items[i]);
            break;
        case V_MAP:
            for (size_t i = 0; i < v->as.map.n; i++) mark_ptr(v->as.map.vals[i]);
            break;
        case V_FUN: mark_env(v->as.fun.env); break;
        default: break;
    }
}

static void mark_env(void *p) {
    for (Env *e = (Env *)p; e; e = e->parent) {
        for (Binding *b = e->vars; b; b = b->next) mark_ptr(b->v);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize_address))
#endif
static void scan_memory(void *lo, void *hi) {
    unsigned char *p = (unsigned char *)lo;
    unsigned char *end = (unsigned char *)hi;
    p = (unsigned char *)((uintptr_t)p & ~(uintptr_t)7);
    for (; p < end; p += 8) {
        void *cand;
        memcpy(&cand, p, sizeof(cand));
        mark_ptr(cand);
    }
}

void gc_collect(void) {
    if (!g_enabled) return;
    g_mark_len = 0;

    mark_env(bs_global_env());
    for (size_t i = 0; i < g_envs_len; i++) mark_env(g_envs[i]);
    for (size_t i = 0; i < g_roots_len; i++) mark_ptr(*(void **)g_roots[i]);

    jmp_buf regs;
    setjmp(regs);
    scan_memory((void *)&regs, (void *)((char *)&regs + sizeof(regs)));
    if (g_stack_top) {
        uintptr_t probe[2];
        probe[0] = (uintptr_t)&probe;
        size_t span = (size_t)(g_stack_top - (unsigned char *)&probe);
        if (g_stack_top > (unsigned char *)&probe && span < 64u * 1024u * 1024u) {
            scan_memory((void *)&probe, (void *)g_stack_top);
        } else {
            g_stats.skipped++;
            return;
        }
    }

    while (g_mark_len) {
        Value *v = g_mark_stack[--g_mark_len];
        mark_value(v);
    }

    size_t live = 0;
    for (size_t i = 0; i < g_heap_len; i++) {
        Value *v = g_heap[i];
        if (v->gc_mark) {
            v->gc_mark = 0;
            g_heap[live++] = v;
        } else {
            value_release(v);
            v->k = V_NIL;
            v->as.list.items = (Value **)g_free_list;
            g_free_list = v;
            g_stats.freed++;
        }
    }
    g_heap_len = live;
    g_stats.live = live;
    g_stats.runs++;
    g_stats.skipped = 0;
    g_alloc_since = 0;
    g_limit = live * 2 + 262144;
}

int gc_pending(void) {
    if (!g_enabled) return 0;
    if (g_pending) {
        g_pending = 0;
        return 1;
    }
    return 0;
}

void gc_maybe_collect(void) {
    if (gc_pending()) gc_collect();
}

void gc_init(int enabled) {
    g_stack_top = (unsigned char *)__builtin_frame_address(0);
    g_enabled = enabled ? 1 : 0;
}

void gc_shutdown(void) {
    for (size_t i = 0; i < g_heap_len; i++) value_release(g_heap[i]);
    g_heap_len = 0;
    Page *pg = g_pages;
    while (pg) {
        Page *next = pg->next;
        free(pg);
        pg = next;
    }
    g_pages = NULL;
    free(g_heap);
    g_heap = NULL;
    free(g_mark_stack);
    g_mark_stack = NULL;
    free(g_roots);
    g_roots = NULL;
    free(g_envs);
    g_envs = NULL;
}

void gc_set_enabled(int on) {
    if (on == g_enabled) return;
    g_enabled = on;
    if (on) gc_collect();
}

int gc_enabled(void) { return g_enabled; }

void gc_stats(GCStats *out) {
    *out = g_stats;
    out->live = g_heap_len;
    out->enabled = g_enabled;
}
