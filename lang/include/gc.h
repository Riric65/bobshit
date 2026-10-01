#ifndef BOBSHIT_GC_H
#define BOBSHIT_GC_H

#include <stddef.h>

typedef struct GCStats {
    size_t live;
    size_t freed;
    size_t runs;
    size_t bytes;
    size_t skipped;
    int enabled;
} GCStats;

void gc_init(int enabled);
void gc_shutdown(void);

void *gc_alloc(size_t n);
int gc_pending(void);
void gc_maybe_collect(void);
void gc_collect(void);

void gc_push_env(void *env);
void gc_pop_env(void *env);
void gc_add_root(void **slot);

void gc_set_enabled(int on);
int gc_enabled(void);
void gc_stats(GCStats *out);

#endif
