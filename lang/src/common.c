/* common.c — memory, strings, diagnostics, error unwinding. */
#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bs_soft = 1;
int bs_quiet = 0;
unsigned long bs_warn_count = 0;

BsCatch *bs_catch_top = NULL;
char *bs_error_msg = NULL;

static void oom(void) {
    fputs("bobshit: out of memory\n", stderr);
    exit(1);
}

void *bs_malloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *bs_calloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *bs_realloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}

char *bs_strdup(const char *s) {
    size_t n = strlen(s);
    char *p = bs_malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *bs_strndup(const char *s, size_t n) {
    char *p = bs_malloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ------------------------------------------------------------------ */
/* Str                                                                 */
/* ------------------------------------------------------------------ */

void str_init(Str *s) {
    s->cap = 32;
    s->len = 0;
    s->data = bs_malloc(s->cap);
    s->data[0] = '\0';
}

void str_free(Str *s) {
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

void str_clear(Str *s) {
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

void str_reserve(Str *s, size_t extra) {
    if (s->len + extra + 1 <= s->cap) return;
    size_t cap = s->cap ? s->cap : 32;
    while (cap < s->len + extra + 1) cap *= 2;
    s->data = bs_realloc(s->data, cap);
    s->cap = cap;
}

void str_addn(Str *s, const char *p, size_t n) {
    str_reserve(s, n);
    memcpy(s->data + s->len, p, n);
    s->len += n;
    s->data[s->len] = '\0';
}

void str_add(Str *s, const char *p) { str_addn(s, p, strlen(p)); }

void str_addc(Str *s, char c) {
    str_reserve(s, 1);
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

void str_addf(Str *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    str_reserve(s, 64);
    int n = vsnprintf(s->data + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    if (n >= 0) {
        if ((size_t)n < s->cap - s->len) {
            s->len += (size_t)n;
        } else {
            str_reserve(s, (size_t)n + 1);
            vsnprintf(s->data + s->len, (size_t)n + 1, fmt, ap2);
            s->len += (size_t)n;
        }
    }
    va_end(ap2);
}

char *str_release(Str *s) {
    char *p = s->data ? s->data : bs_strdup("");
    s->data = NULL;
    s->len = s->cap = 0;
    return p;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */

void bs_warn(int line, const char *fmt, ...) {
    va_list ap;
    if (bs_quiet) return;
    bs_warn_count++;
    fputs("bobshit: warning: ", stderr);
    if (line > 0) fprintf(stderr, "line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void bs_error(int line, const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fputs("runtime error: ", stderr);
    if (line > 0) fprintf(stderr, "line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void bs_fatal(const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fputs("bobshit: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void bs_throw(int line, const char *fmt, ...) {
    Str s;
    va_list ap;
    str_init(&s);
    if (line > 0) str_addf(&s, "line %d: ", line);
    va_start(ap, fmt);
    {
        va_list ap2;
        va_copy(ap2, ap);
        str_reserve(&s, 64);
        int n = vsnprintf(s.data + s.len, s.cap - s.len, fmt, ap);
        if (n >= 0) {
            if ((size_t)n < s.cap - s.len) {
                s.len += (size_t)n;
            } else {
                str_reserve(&s, (size_t)n + 1);
                vsnprintf(s.data + s.len, (size_t)n + 1, fmt, ap2);
                s.len += (size_t)n;
            }
        }
        va_end(ap2);
    }
    va_end(ap);
    free(bs_error_msg);
    bs_error_msg = str_release(&s);

    if (bs_catch_top) {
        BsCatch *c = bs_catch_top;
        bs_catch_top = c->prev;
        longjmp(c->jb, 1);
    }
    fflush(stdout);
    fputs("runtime error: ", stderr);
    fputs(bs_error_msg, stderr);
    fputc('\n', stderr);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

int bs_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

int bs_is_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
int bs_is_digit(int c) { return c >= '0' && c <= '9'; }

int bs_is_ident_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 0x80;
}

int bs_is_ident_char(int c) { return bs_is_ident_start(c) || bs_is_digit(c); }

char *bs_dtoa(double d) {
    if (d != d) return bs_strdup("nan");
    if (d == d / 2 && d != 0) return bs_strdup(d > 0 ? "inf" : "-inf"); /* +/-inf */
    if (d == (double)(long long)d && d < 1e15 && d > -1e15) {
        char buf[32];
        snprintf(buf, sizeof buf, "%lld", (long long)d);
        return bs_strdup(buf);
    }
    char buf[40];
    for (int prec = 15; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, d);
        double back = strtod(buf, NULL);
        if (back == d) break;
    }
    return bs_strdup(buf);
}

size_t bs_utf8_seqlen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; /* lone junk byte: treat as its own "character" */
}

size_t bs_utf8_offset(const char *s, size_t len, size_t char_index) {
    size_t i = 0, count = 0;
    while (i < len && count < char_index) {
        size_t step = bs_utf8_seqlen((unsigned char)s[i]);
        if (i + step > len) step = 1;
        i += step;
        count++;
    }
    return i;
}
