/* common.h — shared primitives for the BobShit runtime.
 *
 * Memory helpers, a growable string buffer, diagnostics and the
 * non-local-exit machinery used by try/catch.
 */
#ifndef BOBSHIT_COMMON_H
#define BOBSHIT_COMMON_H

#include <setjmp.h>
#include <stddef.h>

#define BS_VERSION "0.1.0"

/* 1 = soft mode (default): the interpreter forgives. 0 = strict (-s). */
extern int bs_soft;

/* When 1, diagnostics are swallowed: used by the REPL to speculatively
 * parse an input that may simply be incomplete. */
extern int bs_quiet;

/* Number of soft-mode warnings emitted so far. */
extern unsigned long bs_warn_count;

void *bs_malloc(size_t n);
void *bs_calloc(size_t n, size_t size);
void *bs_realloc(void *p, size_t n);
char *bs_strdup(const char *s);
char *bs_strndup(const char *s, size_t n);

/* ------------------------------------------------------------------ */
/* growable string                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Str;

void str_init(Str *s);
void str_free(Str *s);
void str_clear(Str *s);
void str_reserve(Str *s, size_t extra);
void str_addn(Str *s, const char *p, size_t n);
void str_add(Str *s, const char *p);
void str_addc(Str *s, char c);
void str_addf(Str *s, const char *fmt, ...);
char *str_release(Str *s); /* NUL-terminated buffer, caller owns it */

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */
void bs_warn(int line, const char *fmt, ...);
void bs_error(int line, const char *fmt, ...); /* prints + exit(1) */
void bs_fatal(const char *fmt, ...);           /* prints + exit(1) */

/* ------------------------------------------------------------------ */
/* non local exit (try / catch)                                       */
/* ------------------------------------------------------------------ */
typedef struct BsCatch BsCatch;
extern BsCatch *bs_catch_top;
extern char *bs_error_msg;

struct BsCatch {
    BsCatch *prev;
    jmp_buf jb;
    int frame_depth; /* call depth when the handler was installed */
};

/* Raise a runtime error: unwinds to the innermost `try` handler, or
 * aborts the program with `runtime error: ...` when there is none. */
void bs_throw(int line, const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* misc string / char helpers                                         */
/* ------------------------------------------------------------------ */
int bs_streq_ci(const char *a, const char *b);
int bs_is_space(int c);
int bs_is_digit(int c);
int bs_is_ident_start(int c);
int bs_is_ident_char(int c);
char *bs_dtoa(double d); /* shortest sane representation, malloc'd */
size_t bs_utf8_seqlen(unsigned char c);
size_t bs_utf8_offset(const char *s, size_t len, size_t char_index);

#endif /* BOBSHIT_COMMON_H */
